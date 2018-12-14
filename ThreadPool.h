/*
 * Copyright 2018 Gong Cheng
 */

#pragma once

#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <functional>

/* -------------------------------*/
/**
 * @Synopsis: ThreadPool
 */
/* ---------------------------------*/
class ThreadPool {
 public:
    /* -------------------------------*/
    /**
     * @Synopsis: Singleton, use auto &pool = ThreadPool::Instance() to
     *            get instance
     */
    /* ---------------------------------*/
    static ThreadPool &Instance() {
        static ThreadPool pool;
        return pool;
    }

    /* -------------------------------*/
    /**
     * @Synopsis: Make sure call stop before program exits
     */
    /* ---------------------------------*/
    ~ThreadPool();

    /* -------------------------------*/
    /**
     * @Synopsis: start the thread pool of @minNumWorkers threads.
     *
     * @Param: minNumWorkers should be > 0 && <= maxNumWorkers
     */
    /* ---------------------------------*/
    void start(uint32_t minNumWorkers = 1);

    /* -------------------------------*/
    /**
     * @Synopsis: add task to thread pool
     *
     * @Param: f
     * @Param: args
     *
     * @Returns: Future result of task f.
     *           By calling the result's get() method,
     *           user will receive the return result.
     *           If worker size exceed the max size, return a default future.
     *           Use future.valid() to check if added successfully.
     */
    /* ---------------------------------*/
    template<typename F, typename... Args>
    auto add(F&& f, Args&&... args)
    -> std::future<typename std::result_of<F(Args...)>::type> {
        typedef typename std::result_of<F(Args...)>::type return_type;
        using task_type = std::packaged_task<return_type()>;
        auto func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        auto task = std::make_shared<task_type>(func);
        auto returned_future = task->get_future();
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_stop) {
                throw std::runtime_error("thread pool is not running,"
                                         " call start() first");
            }

            if (_numIdleWorkers == 0 && _numWorkers == _maxNumWorkers) {
                return std::future<return_type>();
            }

            _tasks.emplace(new std::function<void()>([task] () { (*task)(); }));
            if (_numIdleWorkers == 0) {
                ++_numWorkers;
                new ThreadPoolWorker(this);
            } else {
                --_numIdleWorkers;
                _cv.notify_one();
            }
        }
        return returned_future;
    }

    /* -------------------------------*/
    /**
     * @Synopsis: stop All threads in thread pool,
     *            wait for all workers to stop,
     *            retire all workers
     */
    /* ---------------------------------*/
    void stop();

    void getStats(uint32_t *minNumWorkers, uint32_t *maxNumWorkers,
                  uint32_t *numWorkers, uint32_t *numIdleWorkers);

    uint32_t maxNumWorkers() { return _maxNumWorkers; }

 private:
    /* -------------------------------*/
    /**
     * @Synopsis: thread pool worker class
     */
    /* ---------------------------------*/
    class ThreadPoolWorker {
     public:
         /* -------------------------------*/
         /**
          * @Synopsis: construct worker in the pool
          *
          * @Param: pool
          */
         /* ---------------------------------*/
        explicit ThreadPoolWorker(ThreadPool *pool);

         /* -------------------------------*/
         /**
          * @Synopsis: join the thread
          */
         /* ---------------------------------*/
        ~ThreadPoolWorker();

     private:
        ThreadPool *_pool;
        std::unique_ptr<std::thread> _worker;
    };

    /* -------------------------------*/
    /**
     * @Synopsis: initial the maxNumWorkers by cpu counts.
     */
    /* ---------------------------------*/
    ThreadPool();

    /* -------------------------------*/
    /**
     * @Synopsis: deleted constructors.
     */
    /* ---------------------------------*/
    ThreadPool(ThreadPool const &other) = delete;
    ThreadPool(ThreadPool const &&other) = delete;
    ThreadPool& operator=(ThreadPool const &other) = delete;

    /* -------------------------------*/
    /**
     * @Synopsis: infinite loop to run tasks.
     */
    /* ---------------------------------*/
    void _runWorker(ThreadPoolWorker *worker);

    /* -------------------------------*/
    /**
     * @Synopsis: retire unused workers.
     */
    /* ---------------------------------*/
    void _retireWorkers(std::vector<ThreadPoolWorker *> *worker);

    std::queue<std::function<void()> *> _tasks;
    bool _stop;
    std::mutex _mutex;
    std::condition_variable _cv, _stopCv, _increaseCv;
    uint32_t _maxNumWorkers, _minNumWorkers;
    uint32_t _numIdleWorkers;
    uint32_t _numWorkers;
    std::vector<ThreadPoolWorker *> _retiredWorkers;
    bool _increasing;
};
