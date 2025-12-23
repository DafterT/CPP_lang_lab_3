#include "inc/thread_pool.h"
#include <iostream>
#include <thread>
#include <chrono>

namespace {

int sleep_and_return() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return 42;
}

void sleep_and_print() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::cout << "Task completed!\n";
}

}  // namespace

int main() {
    std::cout << "ThreadPool Simple Test\n";

    ThreadPool pool(2);
    std::cout << "Created pool with " << pool.get_thread_count() << " threads\n";

    // Тест с возвращаемым значением
    auto future1 = pool.dispatch_task(sleep_and_return);

    // Тест без возвращаемого значения
    auto future2 = pool.dispatch_task(sleep_and_print);

    std::cout << "Result: " << future1.get() << "\n";
    future2.get();

    std::cout << "Test passed!\n";
    return 0;
}
