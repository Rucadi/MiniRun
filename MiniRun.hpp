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
#include <iostream>
#include <algorithm>
#include <type_traits>
#include <thread>
#include <functional>
#include <unordered_map>
#include <queue>
#include <atomic>
#include <set>
#include <csignal>
#include <mutex>
#include <cassert>

class MiniRun
{
    class  SpinLock;
    class  lock_guard;
    class  ThreadPool;
    struct sentinel_access_type_counter;
    struct Task;

    using task_fun_t        =  std::function<void()>;
    using task_fin_t        =  std::function<bool()>;
    using task_fun_fin_t    =  std::function<task_fin_t()>; //To support an activator function that generates it's own asynchronous finalization
    using dep_t             =  uintptr_t;                   //we track pointers, this is the size of a pointer type.
    using dep_list_t        =  std::vector<dep_t>;          
    using group_t           =  uint32_t;                    //this decides the type of the groups, value 0 is reserved to default
    using num_tasks_t       =  intptr_t;                    //this limits the number of tasks that can be run at the same time
    using sentinel_map_type = std::unordered_map<uintptr_t, sentinel_access_type_counter>;//type of the map where we store the tracking
    static constexpr group_t defaultGroup = 0;
    static constexpr group_t maxGroup = (group_t) -1;

    class SpinLock 
    {
        std::atomic_flag locked = ATOMIC_FLAG_INIT ;
        public:
            inline void lock() {while (locked.test_and_set(std::memory_order_acquire)) {;}}
            inline void unlock() {locked.clear(std::memory_order_release);}
    };

    class lock_guard
    {
        SpinLock &_lock;
        public:
            inline lock_guard(SpinLock& lock):_lock(lock)
            {
                _lock.lock();
            }
            inline ~lock_guard()
            {
                _lock.unlock();
            }
    };

    class ThreadPool
    {
        const int _processor_count = std::thread::hardware_concurrency();
        bool _alive;
        bool _minirunDisabled = std::getenv("DISABLE_MINIRUN")!=nullptr;
        std::queue<Task*>     _runnable_tasks;
        std::vector<std::thread> _threads;
        SpinLock              _thread_pool_spinlock;

        inline void worker()
        {

            Task *task_to_run = nullptr;
            {
                lock_guard guard(_thread_pool_spinlock);
                if(!_runnable_tasks.empty())
                {
                    task_to_run = _runnable_tasks.front();
                    _runnable_tasks.pop();
                }
            }
            if(task_to_run != nullptr) (*task_to_run)();

        }


       inline void spawnThreads(size_t number)
        {
            for(size_t i = 0; i < number; ++i)
                _threads.push_back(std::thread([&]{while(_alive) worker();}));
            
        }

        public:

        inline void runTaskExternalThread()
        {
            worker();
        }

        inline void addTask(Task* task)
        {
            lock_guard guard(_thread_pool_spinlock);
            _runnable_tasks.emplace(task);
        }

        ThreadPool(): _alive(true)
        {
            if(!_minirunDisabled) spawnThreads(_processor_count-1);
        }

        ThreadPool(int numThreads): _alive(true)
        {
           if(!_minirunDisabled) spawnThreads(numThreads);
        }

        ~ThreadPool()
        {
            _alive = false;
            for(auto& thread : _threads) thread.join();
        }

    };
  
    struct sentinel_access_type_counter
        {
            struct block
            {
                Task* outTask;
                num_tasks_t countdownToOut;
                std::queue<Task*> _blockedTasks;
                bool satisfied = false;
                void increaseCountdown(Task* task = nullptr)
                {
                    countdownToOut++;
                }

                void decreaseCountdown(Task* task = nullptr)
                {
                    countdownToOut--;
                }

            };

            std::deque<block>       _blocks;
            SpinLock                _sentinel_mtx;

            inline void _eraseBlock()
            {
                if(_blocks.at(0).countdownToOut != 0)
                    abort();
                _blocks.pop_front();
            }

            inline void _processNext()
            {
                if(_blocks.at(0).countdownToOut==0)
                {

                    if(_blocks.size()>1 && _blocks.at(0).outTask==nullptr && _blocks.at(1).outTask != nullptr && !_blocks.at(1).satisfied)
                    {
                            _blocks.at(1).satisfied = true;
                            _blocks.at(1).outTask->decreaseCountdown();
                            
                    }
                 }      

            }
            inline void decreaseIn(Task* task = nullptr)
            {
                lock_guard guard(_sentinel_mtx);
                _blocks.front().decreaseCountdown(task);
                _processNext();
            }

            inline void increaseIn(Task* task = nullptr)
            {
                lock_guard guard(_sentinel_mtx);
                _blocks.back().increaseCountdown(task);
            }

            inline void processSingleOut()
            {
                lock_guard guard(_sentinel_mtx);
                _eraseBlock();
                _blocks.front().outTask = nullptr;
                while(!_blocks.front()._blockedTasks.empty())
                {
                    _blocks.front()._blockedTasks.front()->decreaseCountdown();
                    _blocks.front()._blockedTasks.pop();
                }
                _processNext();
            }
            inline void addTaskDep(Task* task, bool read)
            {
                lock_guard guard(_sentinel_mtx);
                if(_blocks.size()==0) _blocks.push_back({nullptr,0});

                if(read)
                {
                    task->decreaseAfterExecution(this);
                    _blocks.back().increaseCountdown(task);
                    if(_blocks.size()>1) 
                    {
                         task->increaseCountdown();
                        _blocks.back()._blockedTasks.push(task);
                    }
                }
                else 
                { 
                    _blocks.push_back({task,0});
                    task->increaseCountdown();
                    task->outAfterExecution(this);
                }

                _processNext();
            }
        
        };

    struct Task
    {
        MiniRun& _targetRuntime;
        std::vector<Task*> _taskNotify;
        std::vector<sentinel_access_type_counter*> _decreaseInCounterAfterExecution;
        std::vector<sentinel_access_type_counter*> _processFinishOutAfterExecution;

        task_fun_fin_t       _fun_fin;
        task_fun_t           _fun;
        task_fin_t           _fin;

        bool                 _taskHasFinished;
        bool                 _isFunFin;
        bool                 _isAwaitingForFinalization;
        bool                 _hasAsynchronousFinalization;
        num_tasks_t          _countdownToRelease;
        SpinLock             _countdownMtx;
        group_t              _group;


        Task(MiniRun& ref) : _targetRuntime(ref), _taskHasFinished(false), _countdownToRelease(0)
        {
            _taskNotify.reserve(10);
            _decreaseInCounterAfterExecution.reserve(10);
            _processFinishOutAfterExecution.reserve(10);
        }

        inline void reinitialize()
        {
            _taskNotify.clear();
            _decreaseInCounterAfterExecution.clear();
            _processFinishOutAfterExecution.clear();
            _countdownToRelease = 0;
            _taskHasFinished = false;
            _hasAsynchronousFinalization = false;
            _isAwaitingForFinalization = false;
            _isFunFin = false;
        }

        inline Task* prepare(const task_fun_t& async_fun, group_t group)
        {
            reinitialize();
            _fun = std::move(async_fun);
            _group = group;
            increaseCountdown();
            return this;
        }

        inline Task* prepare(const task_fun_t& async_fun, const task_fin_t& async_fin,  group_t group)
        {
            reinitialize();
            _fun = std::move(async_fun);
            _fin = std::move(async_fin);
            _hasAsynchronousFinalization = true;
            _group = group;
            increaseCountdown();
            return this;
        }

        inline Task* prepare(const task_fun_fin_t& async_fun_fin,  group_t group)
        {
            reinitialize();
            _fun_fin = std::move(async_fun_fin);
            _hasAsynchronousFinalization = true;
            _isFunFin = true;
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
            
            const auto finalizeTask =  [&]{
                onFinish();
                _targetRuntime.releaseTask(this);
                _targetRuntime.decreaseRunningTasks(_group);
            };

            if(!_hasAsynchronousFinalization)
            {
                _fun();
                finalizeTask();
            }
            else
            {
                
                if(!_isAwaitingForFinalization)
                {
                    _isAwaitingForFinalization = true;
                    
                    if(_isFunFin) _fin = _fun_fin();
                    else _fun();
                }

                if(_fin()) finalizeTask();
                else _targetRuntime.addTask(this);

            }

        }

        inline void setGroup(group_t group)
        {
            _group = group;
        }
        inline group_t getGroup() const
        {
            return _group;
        }
        inline void decreaseAfterExecution(sentinel_access_type_counter* sentinel)
        {
            _decreaseInCounterAfterExecution.push_back(sentinel);
        }
        
        inline void outAfterExecution(sentinel_access_type_counter* sentinel)
        {
            _processFinishOutAfterExecution.push_back(sentinel);
        }

        inline void increaseCountdown()
        {
            lock_guard guard(_countdownMtx);
            _countdownToRelease++;
        }

        inline void decreaseCountdown()
        {
            lock_guard guard(_countdownMtx);            
            _countdownToRelease--;
            if(_countdownToRelease == 0) _targetRuntime.addTask(this);
        }

        inline void onFinish()
        {
            _taskHasFinished = true;
            for(auto decrease : _decreaseInCounterAfterExecution) decrease->decreaseIn(this);
            for(auto post : _processFinishOutAfterExecution) post->processSingleOut();

        }
    };
  
    private: 

    inline void releaseTask(Task* task)
    {
        lock_guard guard(_preallocTasksMtx);            
        _preallocatedTasks.push(task);
    }

    inline Task* getPreallocatedTask()
    {
        lock_guard guard(_preallocTasksMtx);            
        if(_preallocatedTasks.size() == 0)
            for(int i = 0; i < 100; ++i)
                _preallocatedTasks.push(new Task(*this));
        
        Task* task = _preallocatedTasks.front();
        _preallocatedTasks.pop();
        return task;
    }



    inline  std::pair<SpinLock, sentinel_map_type>& getSentinelPairForGroup(group_t group)
    {
        lock_guard guard(_sentinel_map_group_lock);            
        return _sentinel_value_map[group];
    }

    inline sentinel_access_type_counter& getSentinelForGroup(dep_t sentinel, group_t group)
    {
        auto& map = getSentinelPairForGroup(group);
        lock_guard guard(map.first);
        return map.second[sentinel];

    }

    inline std::atomic<num_tasks_t>& getGroupRunningTasksCounter(group_t group)
    {
        lock_guard guard(_running_tasks_group_lock);            
        std::atomic<num_tasks_t>& ref = *(&_running_tasks[group]);
        return ref;
    }

    inline void increaseRunningTasks(group_t group)
    {
        _global_running_tasks++;
        lock_guard guard(_running_tasks_group_lock);            
        _running_tasks[group]++;
    }

    inline void decreaseRunningTasks(group_t group)
    {
        _global_running_tasks--;
        lock_guard guard(_running_tasks_group_lock);            
        _running_tasks[group]--;
    } 

    inline void addTask(Task* task)
    {
        _pool.addTask(task);
    }
    public:

    inline void registerTask(Task* task,  const dep_list_t& in, const dep_list_t& out)
    {
        group_t group = task->getGroup();
       
        increaseRunningTasks(group);

        for(const uintptr_t i : in) getSentinelForGroup(i, group).addTaskDep(task,true);
        for(const uintptr_t i : out) getSentinelForGroup(i, group).addTaskDep(task,false); 

        task->activate();
    }
    
    //CONSTRUCTORS FOR TASKS WITH SYNCHRONOUS FINALIZATION
    
    inline void createTask(const task_fun_t&  async_fun, group_t group)
    {
        createTask(async_fun, deps(), deps(), group);
    }

    inline void createTask(const task_fun_t& async_fun)
    {
        createTask(async_fun, deps(), deps());
    }

    inline void createTask(const task_fun_t& async_fun, const dep_list_t& in, const dep_list_t& out, group_t group = defaultGroup)
    {
        if(!_minirunDisabled)
            return registerTask(getPreallocatedTask()->prepare(async_fun, group), in, out);
        else async_fun();
    }


    //CONSTRUCTORS FOR TASKS WITH ASYNCHRONOUS FINALIZATIONS
    
    inline void createTask(const task_fun_t&  async_fun, const task_fin_t& async_fin, group_t group)
    {
        createTask(async_fun, async_fin, deps(), deps(), group);
    }

    inline void createTask(const task_fun_t& async_fun, const task_fin_t& async_fin)
    {
        createTask(async_fun, async_fin, deps(), deps());
    }

    inline void createTask(const task_fun_t& async_fun, const task_fin_t& async_fin,   const dep_list_t& in, const dep_list_t& out, group_t group = defaultGroup)
    {
        if(!_minirunDisabled)  registerTask(getPreallocatedTask()->prepare(async_fun, async_fin, group), in, out);
        else
        {
            async_fun();
            while(async_fin());
        }
    }

    //CONSTRUCTOR FOR TASKS WITH DYNAMIC ASYNCHRONOUS FINALIZATION
    inline void createTask(const task_fun_fin_t& async_fun_fin, group_t group)
    {
       createTask(async_fun_fin, deps(), deps(), group);
    } 
    
     inline void createTask(const task_fun_fin_t& async_fun_fin)
    {
       createTask(async_fun_fin, deps(), deps());
    }

    inline void createTask(const task_fun_fin_t& async_fun_fin,  const dep_list_t& in, const dep_list_t& out, group_t group = defaultGroup)
    {
        if(!_minirunDisabled)  registerTask(getPreallocatedTask()->prepare(async_fun_fin, group), in, out);
        else
        {
            auto fin = async_fun_fin();
            while(fin());
        }
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

    template<typename T, typename ActionFunction>//In c++20 should use concepts..
    inline void parallel_for_each(T  begin, T end, const ActionFunction& fun, group_t group = maxGroup)
    {
        while(begin != end) 
        {
            createTask([fun, value = &(*begin)]{fun(*value);},group);
            ++begin;
        }

        if(group == maxGroup) taskwait(maxGroup);
    }


    template <typename  T, typename ActionFunction>
    inline void parallel_for_each(T& container, const ActionFunction& fun, group_t group = maxGroup)
    {
        for(auto& value : container) 
            createTask([fun, value = &value]{fun(*value);}, maxGroup);

        if(group == maxGroup) taskwait(maxGroup);
    }


    template <typename  T, typename ActionFunction>
    inline void parallel_for_each(T& container, size_t stepSize,  const ActionFunction& fun, group_t group = maxGroup)
    {

        size_t containerSize = container.size();
        size_t blockNum      = containerSize/stepSize;
        size_t rest          = containerSize%stepSize;

        for(int i = 0; i < blockNum; ++i)
            createTask([cp = &container, idx=i*stepSize, size = stepSize, fun]
            {
                std::for_each(std::begin(*cp)+idx, std::begin(*cp)+(idx+size),fun);
            }, group);

        if(rest!=0)
            createTask([cp = &container, idx=blockNum*stepSize, size = rest, fun]
            {
                std::for_each(std::begin(*cp)+idx, std::begin(*cp)+(idx+size),fun);
            }, group);
        
        if(group == maxGroup) taskwait(maxGroup);
    }

    template <typename T, typename ActionFunction>
    inline void parallel_for(T b, T e, const ActionFunction& fun, group_t group = maxGroup)
    {
        for(; b<=e; ++b)
            createTask([=]{fun(b);}, group);

        if(group == maxGroup) taskwait(maxGroup);
    }

    template <typename T, typename ActionFunction>
    inline void parallel_for(T b, T e, size_t stepSize, const ActionFunction& fun, group_t group = maxGroup)
    {
        for(; b<=e; b+=stepSize)
            createTask([=]{ for(size_t i = 0; i < stepSize; ++i) fun(b+i);}, group);
        if(b>e)
        {
            b = e-b;
            createTask([=, stepSize = b]{ for(size_t i = 0; i < stepSize; ++i) fun(b+i);}, group);

        }
        
        if(group == maxGroup) taskwait(maxGroup);
    }
    public:
    template<typename... T> static dep_list_t deps(const T&... params) { return {(uintptr_t) std::is_pointer<T>::value?(uintptr_t)params:(uintptr_t)&params...}; }
    MiniRun() :_pool(), _global_running_tasks(0) {}
    MiniRun(int numThreads) : _pool(numThreads), _global_running_tasks(0) {}
    ~MiniRun(){ taskwait(); while(!_preallocatedTasks.empty()){delete _preallocatedTasks.front();_preallocatedTasks.pop();}}


    private:     
    
    ThreadPool _pool;
    SpinLock _sentinel_map_group_lock, _running_tasks_group_lock;
    std::atomic<num_tasks_t>                               _global_running_tasks;
    std::unordered_map<group_t, std::pair<SpinLock, sentinel_map_type>>  _sentinel_value_map;
    std::unordered_map<group_t, std::atomic<num_tasks_t> > _running_tasks;
    std::unordered_map<group_t, SpinLock>                  _group_lock;
    bool _minirunDisabled = std::getenv("DISABLE_MINIRUN")!=nullptr;

    //tasks
    SpinLock          _preallocTasksMtx;
    std::queue<Task*> _preallocatedTasks;

};
