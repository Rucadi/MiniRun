//CPU Task parallelism header-only 
#pragma once

#include <thread>
#include <functional>
#include <atomic>

#include <unordered_map>
#include <list>
#include <deque>
#include <queue>

class MiniRun
{
    using depend_limit = uint32_t;
    struct Task;
    struct sentinel_access_type_counter;
    class SpinLock {
        std::atomic_flag locked = ATOMIC_FLAG_INIT ;
    public:
        inline void lock() {while (locked.test_and_set(std::memory_order_acquire)) { ; }        }
        inline void unlock() {locked.clear(std::memory_order_release);}
    };
    struct Task
    {
        MiniRun& targetRuntime;
        std::vector<Task*> _taskNotify;
        std::vector<sentinel_access_type_counter*> _increaseInCounterBeforeExecution;
        std::vector<sentinel_access_type_counter*> _decreaseInCounterAfterExecution;
        std::vector<sentinel_access_type_counter*> _processFinishOutAfterExecution;

        std::function<void()> _fun;
        bool _taskHasFinished;
        depend_limit _countdownToRelease;
        SpinLock countdownMtx;


        Task(MiniRun& ref) : targetRuntime(ref), _taskHasFinished(false), _countdownToRelease(0)
        {
            _taskNotify.reserve(100);
            _increaseInCounterBeforeExecution.reserve(100);
            _decreaseInCounterAfterExecution.reserve(100);
            _processFinishOutAfterExecution.reserve(100);
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
        inline void operator()()
        {
            _fun();
            onFinish();
            reinitialize();
            targetRuntime.releaseTask(this);
            targetRuntime._running_tasks--;
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
            countdownMtx.lock();
            _countdownToRelease++;
            countdownMtx.unlock();
        }

        inline void decreaseCountdown()
        {
            countdownMtx.lock();
            
            _countdownToRelease--;
            if(_countdownToRelease == 0)
            {
                for(auto sentinel_access:_increaseInCounterBeforeExecution) sentinel_access->increaseIn();
                //enqueue into ready task queue
                targetRuntime._pool.AddTask(this);
            } 
            countdownMtx.unlock();
        }
        inline void onFinish()
        {
            _taskHasFinished = true;
            for(auto task     : _taskNotify) task->decreaseCountdown();
            for(auto decrease : _decreaseInCounterAfterExecution) decrease->decreaseIn();
            for(auto post : _processFinishOutAfterExecution) post->processSingleOut();
        }
    };
    class ThreadPool
    {
        const int processor_count = std::thread::hardware_concurrency();
        bool alive;
        std::queue<Task*>     _runnable_tasks;
        SpinLock              _thread_pool_spinlock;
        inline void Worker()
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
                    while(alive) Worker();
                });
                thread.detach();
            }
        }

        public:

        inline void AddTask(Task* task)
        {
            _thread_pool_spinlock.lock();
           // printf("Task: %p added\n", task);
            _runnable_tasks.emplace(task);
            _thread_pool_spinlock.unlock();
        }

        ThreadPool(): alive(true)
        {
            spawnThreads(processor_count);
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
        std::deque<Task*>    _out_waiting_to_0;
        std::deque<depend_limit> _in_counter_for_out;
        SpinLock sentinel_mtx;

        inline void decreaseIn()
        {
            sentinel_mtx.lock();
            _in_counter_for_out.front()--;

            if(_in_counter_for_out.front() == 0 && _out_waiting_to_0.front() != nullptr)
                _out_waiting_to_0.front()->decreaseCountdown();
            
            sentinel_mtx.unlock();
        }

        inline void increaseIn()
        {
            sentinel_mtx.lock();
            _in_counter_for_out.back()++;
            sentinel_mtx.unlock();
        }

        inline void processSingleOut()
        {
            sentinel_mtx.lock();
            _out_waiting_to_0.pop_front();
            _in_counter_for_out.pop_front();
            if(_in_counter_for_out.size() == 0)  _in_counter_for_out.push_back(0);
            if(_out_waiting_to_0.size() == 0)    _out_waiting_to_0.push_back(nullptr);

            if(_in_counter_for_out.front() == 0 && _out_waiting_to_0.front()!=nullptr) 
                _out_waiting_to_0.front()->decreaseCountdown();

            sentinel_mtx.unlock();
        }
        inline void addTaskDep(Task* task, bool read)
        {
            sentinel_mtx.lock();

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
            sentinel_mtx.unlock();

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

    public:

    inline void createTask(const std::function<void()>& asyncTask, const std::vector<uintptr_t>& in, const std::vector<uintptr_t>& out)
    {
        _running_tasks++;
        Task* task = getPreallocatedTask();
        task->increaseCountdown();
        task->_fun = std::move(asyncTask);

        for(const uintptr_t i : in)  _sentinel_value[i].addTaskDep(task,true);
        for(const uintptr_t i : out) _sentinel_value[i].addTaskDep(task,false);

        task->decreaseCountdown();
    }

    inline void taskWait()
    {
        while(_running_tasks != 0)
        { 
            std::this_thread::yield(); 
        }
    }


    public:
    template<typename... T> static std::vector<uintptr_t> deps(const T... params){return {(uintptr_t) params...};}
    MiniRun() :_pool(), _running_tasks(0) {}
    MiniRun(int numThreads) : _pool(numThreads), _running_tasks(0) {}
    ~MiniRun(){}

    private:     
    
    ThreadPool _pool;
    std::atomic<int> _running_tasks;
    std::unordered_map<uintptr_t, sentinel_access_type_counter> _sentinel_value;
    SpinLock _preallocTasksMtx;
    std::queue<Task*> _preallocatedTasks;

};
