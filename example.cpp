/*
 * Copyright 2018 Gong Cheng
 */

#include <atomic>
#include <chrono>
#include <iostream>

#include "ThreadPool.h"

#define N 1000

std::atomic<int> counter;

// dummy function
void f() {
    std::string s;
    for (int i = 0; i < 100000; ++i) {
        s += 'a';
    }
    ++counter;
}

int main(int argc, char **argv) {
    counter = 0;
    auto start = std::chrono::system_clock::now();
    for (int i = 0; i < N; ++i) {
        f();
    }
    std::cout << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - start).count() << "ms\n";

    std::vector<std::thread> threads;
    counter = 0;
    start = std::chrono::system_clock::now();
    for (int i = 0; i < N; ++i) {
        threads.emplace_back(f);
    }
    while (counter < N) {}
    std::cout << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - start).count() << "ms\n";
    for (int i = 0; i < N; ++i) {
        threads[i].join();
    }
    threads.clear();

    auto &pool = ThreadPool::Instance();
    counter = 0;
    pool.start(100);
    start = std::chrono::system_clock::now();
    for (int i = 0; i < N; ++i) {
        if(!pool.add(f).valid()) {
            f();
        }
    }
    while (counter < N) {}
    std::cout << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - start).count() << "ms\n";
    pool.stop();

    return 0;
}
