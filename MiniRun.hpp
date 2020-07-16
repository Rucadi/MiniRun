/*
MIT License

Copyright (c) [2020] [Ruben Cano Diaz]

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/
#pragma once

#include <type_traits>
#include <thread>
#include <functional>
#include <unordered_map>
#include <queue>
#include <atomic>

class MiniRun
{
    class  SpinLock;
    class  ThreadPool;
    struct sentinel_access_type_counter;
    struct Task;

    using task_fun_t        =  std::function<void()>; //enforcement of const&
    using dep_t             =  uintptr_t;             //we track pointers, this is the size of a pointer type.
    using dep_list_t        =  std::vector<dep_t>;    //enforcement of const&
    using group_t           =  uint32_t;              //this decides the type of the groups, value 0 is reserved to default
    using num_tasks_t       =  uintptr_t;             //this limits the number of tasks that can be run at the same time
    using sentinel_map_type = std::unordered_map<uintptr_t, sentinel_access_type_counter>;//type of the map where we store the tracking
    static constexpr group_t defaultGroup = 0;

    class SpinLock 
    {
        std::atomic_flag locked = ATOMIC_FLAG_INIT ;
        public:
            inline void lock() {while (locked.test_and_set(std::memory_order_acquire)) {;}}
            inline void unlock() {locked.clear(std::memory_order_release);}
    };

    class ThreadPool
    {
        const int processor_count = std::thread::hardware_concurrency();
        bool alive;
        std::queue<Task*>     _runnable_tasks;
        SpinLock              _thread_pool_spinlock;
        inline void worker()
        {
            _thread_pool_spinlock.lock();
            if(_runnable_tasks.size() > 0)
            {
                Task& task_to_run = *_runnable_tasks.front();
                _runnable_tasks.pop();
                _thread_pool_spinlock.unlock();
                task_to_run();
            }
            else _thread_pool_spinlock.unlock();

        }


       inline void spawnThreads(size_t number)
        {
            for(size_t i = 0; i < number; ++i)
            {
                std::thread thread([&]()
                {
                    while(alive) worker();
                });
                thread.detach();
            }
        }

        public:

        inline void runTaskExternalThread()
        {
            _thread_pool_spinlock.lock();
            if(!_runnable_tasks.empty())
            {
                Task& task_to_run = *_runnable_tasks.front();
                _runnable_tasks.pop();
                _thread_pool_spinlock.unlock();
                task_to_run();
            }
            else _thread_pool_spinlock.unlock();
        }

        inline void addTask(Task* task)
        {
            _thread_pool_spinlock.lock();
            _runnable_tasks.emplace(task);
            _thread_pool_spinlock.unlock();
        }

        ThreadPool(): alive(true)
        {
            spawnThreads(processor_count-1);
        }

        ThreadPool(int numThreads): alive(true)
        {
            spawnThreads(numThreads);
        }

        ~ThreadPool()
        {
            alive = false;
        }

    };
  
    struct sentinel_access_type_counter
        {
            std::deque<Task*>       _out_waiting_to_0;
            std::deque<num_tasks_t> _in_counter_for_out;
            SpinLock                _sentinel_mtx;

            inline void decreaseIn()
            {
                _sentinel_mtx.lock();
                _in_counter_for_out.front()--;

                if(_in_counter_for_out.front() == 0 && _out_waiting_to_0.front() != nullptr)
                    _out_waiting_to_0.front()->decreaseCountdown();
                
                _sentinel_mtx.unlock();
            }

            inline void increaseIn()
            {
                _sentinel_mtx.lock();
                _in_counter_for_out.back()++;
                _sentinel_mtx.unlock();
            }

            inline void processSingleOut()
            {
                _sentinel_mtx.lock();
                _out_waiting_to_0.pop_front();
                _in_counter_for_out.pop_front();
                if(_in_counter_for_out.size() == 0)  _in_counter_for_out.push_back(0);
                if(_out_waiting_to_0.size() == 0)    _out_waiting_to_0.push_back(nullptr);

                if(_in_counter_for_out.front() == 0 && _out_waiting_to_0.front()!=nullptr) 
                    _out_waiting_to_0.front()->decreaseCountdown();

                _sentinel_mtx.unlock();
            }
            inline void addTaskDep(Task* task, bool read)
            {
                _sentinel_mtx.lock();

                if(_out_waiting_to_0.size() == 0) 
                    _out_waiting_to_0.push_back(nullptr);

                if(_in_counter_for_out.size() == 0) 
                    _in_counter_for_out.push_back(0);

                const bool write = !read;
                if(read)
                {
                    task->decreaseAfterExecution(this);
                    _in_counter_for_out.back()++;
                    if(_out_waiting_to_0.back() != nullptr) //1 means that this dependence is satisfied!
                    {
                        task->increaseCountdown();
                        _out_waiting_to_0.back()->addNotify(task);
                    }
                                }

                if(write)
                {
                    
                    if(_out_waiting_to_0.front() != nullptr)
                    {
                        task->increaseCountdown();
                        _out_waiting_to_0.push_back(task);
                        _in_counter_for_out.push_back(0);
                    }
                    else
                    {
                        _out_waiting_to_0.front() = task;
                        if(_in_counter_for_out.front() != 0)
                        {
                            //can't run task yet! but that's ok!
                            task->increaseCountdown();
                        }
                    }
                    task->outAfterExecution(this);
                }
                _sentinel_mtx.unlock();
            }
        
        };

    struct Task
    {
        MiniRun& _targetRuntime;
        std::vector<Task*> _taskNotify;
        std::vector<sentinel_access_type_counter*> _increaseInCounterBeforeExecution;
        std::vector<sentinel_access_type_counter*> _decreaseInCounterAfterExecution;
        std::vector<sentinel_access_type_counter*> _processFinishOutAfterExecution;

        task_fun_t           _fun;
        bool                 _taskHasFinished;
        num_tasks_t          _countdownToRelease;
        SpinLock             _countdownMtx;
        group_t              _group;


        Task(MiniRun& ref) : _targetRuntime(ref), _taskHasFinished(false), _countdownToRelease(0)
        {
            _taskNotify.reserve(100);
            _increaseInCounterBeforeExecution.reserve(10);
            _decreaseInCounterAfterExecution.reserve(10);
            _processFinishOutAfterExecution.reserve(10);
        }

        inline void reinitialize()
        {
            _taskNotify.clear();
            _increaseInCounterBeforeExecution.clear();
            _decreaseInCounterAfterExecution.clear();
            _processFinishOutAfterExecution.clear();
            _countdownToRelease = 0;
            _taskHasFinished = false;
        }

        inline Task* prepare(const task_fun_t& async_fun, group_t group)
        {
            reinitialize();
            _fun = std::move(async_fun);
            _group = group;
            increaseCountdown();
            return this;
        }

        inline void activate()
        {
            decreaseCountdown();
        }
        inline void operator()()
        {
            _fun();
            onFinish();
            _targetRuntime.releaseTask(this);
            _targetRuntime.decreaseRunningTasks(_group);

        }

        inline void setGroup(group_t group)
        {
            _group = group;
        }
        inline void decreaseAfterExecution(sentinel_access_type_counter* sentinel)
        {
            _decreaseInCounterAfterExecution.push_back(sentinel);
        }

        inline void increaseBeforeExecution(sentinel_access_type_counter* sentinel)
        {
            _increaseInCounterBeforeExecution.push_back(sentinel);
        }
        
        inline void outAfterExecution(sentinel_access_type_counter* sentinel)
        {
            _processFinishOutAfterExecution.push_back(sentinel);
        }

        inline void addNotify(Task* task)
        {
            //If a task tries to add a notify when this task has finished, decrease the countdown
            //this is because that task has not seen 
            if(!_taskHasFinished) _taskNotify.push_back(task);
            else task->decreaseCountdown(); 
        }

        inline void increaseCountdown()
        {
            _countdownMtx.lock();
            _countdownToRelease++;
            _countdownMtx.unlock();
        }

        inline void decreaseCountdown()
        {
            _countdownMtx.lock();
            
            _countdownToRelease--;
            if(_countdownToRelease == 0)
            {
                for(auto sentinel_access:_increaseInCounterBeforeExecution) sentinel_access->increaseIn();
                //enqueue into ready task queue
                _targetRuntime.addTask(this);
            } 
            _countdownMtx.unlock();
        }
        inline void onFinish()
        {
            _taskHasFinished = true;
            for(auto task     : _taskNotify) task->decreaseCountdown();
            for(auto decrease : _decreaseInCounterAfterExecution) decrease->decreaseIn();
            for(auto post : _processFinishOutAfterExecution) post->processSingleOut();
        }
    };
  
    private: 

    inline void releaseTask(Task* task)
    {
        _preallocTasksMtx.lock();
        _preallocatedTasks.push(task);
        _preallocTasksMtx.unlock();
    }

    inline Task* getPreallocatedTask()
    {
        _preallocTasksMtx.lock();
        if(_preallocatedTasks.size() == 0)
            for(int i = 0; i < 100; ++i)
                _preallocatedTasks.push(new Task(*this));
        
        Task* task = _preallocatedTasks.front();
        _preallocatedTasks.pop();
        _preallocTasksMtx.unlock();
        return task;
    }

    inline void groupLock(const group_t group)
    {
        _structure_locks[0].lock();
        _group_lock[group].lock();
        _structure_locks[0].unlock();
    }
    inline void groupUnlock(const group_t group)
    {
        _structure_locks[0].lock();
        _group_lock[group].unlock();
        _structure_locks[0].unlock();
    }

    inline  sentinel_map_type& getSentinelMapForGroup(group_t group)
    {
        _structure_locks[2].lock();
        sentinel_map_type& svm = _sentinel_value_map[group];
        _structure_locks[2].unlock();
        return svm;
    }


    inline std::atomic<num_tasks_t>& getGroupRunningTasksCounter(group_t group)
    {
        _structure_locks[1].lock();
        std::atomic<num_tasks_t>& ref = *(&_running_tasks[group]);
        _structure_locks[1].unlock();
        return ref;
    }

    inline void increaseRunningTasks(group_t group)
    {
        _global_running_tasks++;
        _structure_locks[1].lock();
        _running_tasks[group]++;
        _structure_locks[1].unlock();
    }

    inline void decreaseRunningTasks(group_t group)
    {
        _global_running_tasks--;
        _structure_locks[1].lock();
        _running_tasks[group]--;
        _structure_locks[1].unlock();
    } 

    inline void addTask(Task* task)
    {
        _pool.addTask(task);
    }
    public:

    inline void createTask(const task_fun_t&  async_fun, group_t group)
    {
        createTask(async_fun, {}, {}, group);
    }

    inline void createTask(const task_fun_t& async_fun)
    {
        createTask(async_fun, {}, {});
    }

    inline void createTask(const task_fun_t& async_fun, const dep_list_t& in, const dep_list_t& out, group_t group = defaultGroup)
    {
        increaseRunningTasks(group);

        Task* task = getPreallocatedTask()->prepare(async_fun, group);

        groupLock(group);
        for(const uintptr_t i : in)  
            getSentinelMapForGroup(group)[i].addTaskDep(task,true);
        for(const uintptr_t i : out) 
          getSentinelMapForGroup(group)[i].addTaskDep(task,false); 
       
        groupUnlock(group);

        task->activate();
    }

    inline void taskwait(group_t group)
    {

        auto& runningTasksGroup = getGroupRunningTasksCounter(group);
        while(runningTasksGroup != 0)
            _pool.runTaskExternalThread();
    }

    inline void taskwait()
    {
        while( _global_running_tasks != 0)
            _pool.runTaskExternalThread();
    }


    public:
    template<typename... T> static dep_list_t deps(const T&... params) { return {(uintptr_t) std::is_pointer<T>::value?(uintptr_t)params:(uintptr_t)&params...}; }
    MiniRun() :_pool(), _global_running_tasks(0) {}
    MiniRun(int numThreads) : _pool(numThreads), _global_running_tasks(0) {}
    ~MiniRun(){taskwait(); while(!_preallocatedTasks.empty()){delete _preallocatedTasks.front();_preallocatedTasks.pop();}}


    private:     
    
    ThreadPool _pool;
    SpinLock _structure_locks[3];
    std::atomic<num_tasks_t>                               _global_running_tasks;
    std::unordered_map<group_t, sentinel_map_type>         _sentinel_value_map;
    std::unordered_map<group_t, std::atomic<num_tasks_t> > _running_tasks;
    std::unordered_map<group_t, SpinLock>                  _group_lock;

    //tasks
    SpinLock          _preallocTasksMtx;
    std::queue<Task*> _preallocatedTasks;

};
