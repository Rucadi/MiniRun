//CPU Task parallelism header-only 
#pragma once
#include <unordered_map>
#include <functional>
#include <atomic>
#include <thread>
#include <queue>
#include <mutex>


class MiniRun
{
   
    struct sentinel_access_type_counter
    {
        int in;
        int out;
    };

    std::unordered_map<uintptr_t, sentinel_access_type_counter> _sentinel_value;
    std::mutex _blocked_tasks_lock, map_lock;
    std::queue<std::function<bool()>> _blocked_task_queue;

    std::atomic<int> _running_tasks;

    class Thread_pool
    {
        const int processor_count = std::thread::hardware_concurrency();
        bool alive;
        std::queue<std::function<void()>> __runnable_tasks;
        std::function<void()> _idle_function;
        std::mutex _thread_pool_lock;

        void Worker()
        {
            if(_thread_pool_lock.try_lock())
            {
                if(__runnable_tasks.size() > 0)
                {
                    std::function<void()> task_to_run = std::move(__runnable_tasks.front());
                    __runnable_tasks.pop();
                    _thread_pool_lock.unlock();
                    task_to_run();
                }
                else 
                {
                    _thread_pool_lock.unlock();
                    _idle_function();
                }
            }
        }


        void spawnThreads(int number)
        {
            for(int i = 0; i < number; ++i)
            {
                std::thread t([&]()
                {
                    while(alive) Worker();
                    exit(0);
                });
                t.detach();
            }
        }

        public:

        void Add_job(const std::function<void()>& job)
        {
            _thread_pool_lock.lock();
            __runnable_tasks.emplace(std::move(job));
            _thread_pool_lock.unlock();
        }

        Thread_pool(const std::function<void()> idle):_idle_function(idle), alive(true)
        {
            spawnThreads(processor_count-2);
        }

        Thread_pool(const std::function<void()> idle, int numThreads):_idle_function(idle), alive(true)
        {
            spawnThreads(numThreads);
        }

        ~Thread_pool()
        {
            alive = false;
        }

    };

    Thread_pool pool;

   


    void doWork()
    {
        
        if(_blocked_tasks_lock.try_lock())
        {
            if(_blocked_task_queue.size()>0)
            {
                std::function<bool()> sentToRun = std::move(_blocked_task_queue.front());
                _blocked_task_queue.pop();
                _blocked_tasks_lock.unlock();
                
                if(!sentToRun())
                {
                    _blocked_tasks_lock.lock();
                    _blocked_task_queue.emplace(std::move(sentToRun));
                    _blocked_tasks_lock.unlock();
                }
            }
            else _blocked_tasks_lock.unlock();
      
        }
        
    }


    public:

    //c++20
    /*
        static std::vector<uintptr_t> deps(const auto ... params)
        {
            return {(uintptr_t) params...};
        }
    */
    template<typename... T>
    static std::vector<uintptr_t> deps(const T... params)
    {
        return {(uintptr_t) params...};
    }

    void createTask(std::function<void()> asyncTask, const std::vector<uintptr_t>& in, const std::vector<uintptr_t>& out)
    {
        _running_tasks++;
        std::vector<sentinel_access_type_counter*> in_a;
        std::vector<sentinel_access_type_counter*> out_a;

        in_a.reserve(in.size());
        out_a.reserve(out.size());

        map_lock.lock();
        for(const uintptr_t i : in)  in_a.push_back(&_sentinel_value[i]);
        for(const uintptr_t i : out) out_a.push_back(&_sentinel_value[i]);
        map_lock.unlock();


        auto magic =  [&, task = std::move(asyncTask), in_a = std::move(in_a), out_a = std::move(out_a)]()->bool
        {
            auto isReady = [&]()
            {
                std::lock_guard<std::mutex> guard(map_lock);
                for(sentinel_access_type_counter* t : in_a)
                    if(t->out != 0)
                        return false;
                        
                for(sentinel_access_type_counter* t : out_a)
                    if(t->out + t->in  != 0)
                        return false;
                
                //if it arrives here, it's ready to be executed!
                for(sentinel_access_type_counter* t : in_a)
                    t->in++;
                for(sentinel_access_type_counter* t : out_a)
                    t->out++;
                
                return true;
            };

            if(isReady())
            {
                pool.Add_job([&, task = std::move(task), in_a = std::move(in_a), out_a = std::move(out_a)](){
                    task();
                    //end task
                    std::lock_guard<std::mutex> guard(map_lock);
                    for(sentinel_access_type_counter* t : in_a) t->in--;
                    for(sentinel_access_type_counter* t : out_a) t->out--;
                    _running_tasks--;

                });
                return true;
            }
            return false;
        };

        if(!magic())
        {
            _blocked_tasks_lock.lock();
            _blocked_task_queue.emplace(magic);
            _blocked_tasks_lock.unlock();
        }
            
        }

    void taskWait()
    {
        while(_running_tasks != 0);
    }

    MiniRun() : _running_tasks(0), pool([&](){doWork();})
    {
    }

    MiniRun(int numThreads) : _running_tasks(0), pool([&](){doWork();},numThreads)
    {

    }
    ~MiniRun()
    {
    }
};
