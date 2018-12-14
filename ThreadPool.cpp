/*
 * Copyright 2018 Gong Cheng
 */

#include <sstream>

#include "ThreadPool.h"

ThreadPool::~ThreadPool() {
    std::unique_lock<std::mutex> lock(_mutex);
    if (!_stop) {
        throw std::runtime_error("thread pool not stopped, call stop() first");
    }
}

void
ThreadPool::start(uint32_t minNumWorkers) {
    std::unique_lock<std::mutex> lock(_mutex);
    if (!_stop) {
        throw std::runtime_error("thread pool not stopped");
    }

    if (minNumWorkers == 0 || minNumWorkers > _maxNumWorkers) {
        std::ostringstream oss;
        oss << "invalid thread pool minNumWorkers: "
            << minNumWorkers << ", maxNumWorkers: " << _maxNumWorkers;
        throw std::runtime_error(oss.str());
    }

    _minNumWorkers = minNumWorkers;

    _stop = false;

    for (uint32_t i = 0; i < _minNumWorkers; ++i) {
        ++_numWorkers;
        new ThreadPoolWorker(this);
    }

    _increasing = true;
    _increaseCv.wait(lock,
                     [this] () {
                        return this->_numIdleWorkers == this->_numWorkers;
                     });
    _increasing = false;
}

void
ThreadPool::stop() {
    std::unique_lock<std::mutex> lock(_mutex);
    if (_stop) return;
    _stop = true;
    _numIdleWorkers = 0;
    _cv.notify_all();
    _stopCv.wait(lock, [this] () { return !this->_numWorkers; });
    _retireWorkers(&_retiredWorkers);
}

void
ThreadPool::getStats(uint32_t *minNumWorkers, uint32_t *maxNumWorkers,
                     uint32_t *numWorkers, uint32_t *numIdleWorkers) {
    std::lock_guard<std::mutex> lock(_mutex);
    *minNumWorkers = _minNumWorkers;
    *maxNumWorkers = _maxNumWorkers;
    *numWorkers = _numWorkers;
    *numIdleWorkers = _numIdleWorkers;
}

ThreadPool::ThreadPoolWorker::ThreadPoolWorker(ThreadPool *pool)
    : _pool(pool),
      _worker(new std::thread(&ThreadPool::_runWorker, pool, this))
{}

ThreadPool::ThreadPoolWorker::~ThreadPoolWorker() {
    _worker->join();
}

ThreadPool::ThreadPool()
    : _stop(true), _maxNumWorkers(0), _minNumWorkers(0),
      _numIdleWorkers(0), _numWorkers(0) {
    // std::thread::hardware_concurrency() returns 0 if not well defined or
    // not computable
    if (!(_maxNumWorkers = std::thread::hardware_concurrency() * 100)) {
        _maxNumWorkers = 1000;
    }
}

void
ThreadPool::_runWorker(ThreadPoolWorker *worker) {
    std::unique_lock<std::mutex> lock(_mutex, std::defer_lock);
    for (;;) {
        lock.lock();
        if (!_retiredWorkers.empty()) {
            auto retiredWorkers = std::move(_retiredWorkers);
            lock.unlock();
            _retireWorkers(&retiredWorkers);
            lock.lock();
        }
        if (!_stop && _tasks.empty()) {
            if (_numIdleWorkers >= _numWorkers / 2
                    && _numWorkers > _minNumWorkers) {
                break;
            }
            ++_numIdleWorkers;
            if (_increasing) { _increaseCv.notify_one(); }
            _cv.wait(lock);
        }
        if (!_tasks.empty()) {
            auto task = _tasks.front();
            _tasks.pop();
            lock.unlock();
            (*task)();
            delete task;
        } else if (_stop) {
            break;
        } else {
            lock.unlock();
        }
    }
    _retiredWorkers.push_back(worker);
    --_numWorkers;
    if (_stop && !_numWorkers) {
        _stopCv.notify_one();
    }
}

void
ThreadPool::_retireWorkers(std::vector<ThreadPoolWorker *> *retiredWorkers) {
    for (auto worker : *retiredWorkers) {
        delete worker;
        worker = nullptr;
    }
    retiredWorkers->clear();
}
