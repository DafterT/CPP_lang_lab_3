#include "thread_pool.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <numeric>

int main() {
    std::cout << "=== ThreadPool Test ===\n\n";

    // Создаем пул с 4 потоками
    ThreadPool pool(4);
    std::cout << "Created ThreadPool with " << pool.get_thread_count() << " threads\n";

    // Тест 1: Функции с возвращаемыми значениями
    std::cout << "\n--- Test 1: Functions with return values ---\n";

    std::vector<std::future<int>> futures;

    for (int i = 0; i < 10; ++i) {
        futures.push_back(pool.dispatch_task([i]() -> int {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return i * i;
        }));
    }

    std::cout << "Dispatched 10 tasks, queue size: " << pool.get_queue_size() << "\n";

    for (size_t i = 0; i < futures.size(); ++i) {
        int result = futures[i].get();
        std::cout << "Task " << i << " result: " << result << "\n";
    }

    // Тест 2: Функции без возвращаемых значений
    std::cout << "\n--- Test 2: Functions without return values ---\n";

    std::vector<std::future<void>> void_futures;

    for (int i = 0; i < 5; ++i) {
        void_futures.push_back(pool.dispatch_task([i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            std::cout << "Void task " << i << " completed\n";
        }));
    }

    // Ждем завершения всех void задач
    for (auto& future : void_futures) {
        future.get();
    }

    // Тест 3: Исключения в задачах
    std::cout << "\n--- Test 3: Exception handling ---\n";

    auto exception_future = pool.dispatch_task([]() -> int {
        throw std::runtime_error("Test exception");
        return 42;
    });

    try {
        int result = exception_future.get();
        std::cout << "Unexpected: got result " << result << "\n";
    } catch (const std::runtime_error& e) {
        std::cout << "Caught expected exception: " << e.what() << "\n";
    }

    // Тест 4: Производительность
    std::cout << "\n--- Test 4: Performance comparison ---\n";

    const int num_tasks = 100;
    std::vector<int> data(num_tasks);
    std::iota(data.begin(), data.end(), 0);

    // Последовательное выполнение
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<int> sequential_results;
    for (int val : data) {
        std::this_thread::sleep_for(std::chrono::microseconds(100)); // Имитация работы
        sequential_results.push_back(val * val);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto sequential_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Параллельное выполнение
    start = std::chrono::high_resolution_clock::now();
    std::vector<std::future<int>> parallel_futures;
    for (int val : data) {
        parallel_futures.push_back(pool.dispatch_task([val]() -> int {
            std::this_thread::sleep_for(std::chrono::microseconds(100)); // Имитация работы
            return val * val;
        }));
    }

    std::vector<int> parallel_results;
    for (auto& future : parallel_futures) {
        parallel_results.push_back(future.get());
    }
    end = std::chrono::high_resolution_clock::now();
    auto parallel_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Sequential time: " << sequential_time.count() << " ms\n";
    std::cout << "Parallel time: " << parallel_time.count() << " ms\n";
    std::cout << "Speedup: " << static_cast<double>(sequential_time.count()) / parallel_time.count() << "x\n";

    // Проверяем корректность результатов
    if (sequential_results == parallel_results) {
        std::cout << "Results are identical ✓\n";
    } else {
        std::cout << "Results differ ✗\n";
    }

    std::cout << "\n=== Test completed successfully ===\n";

    return 0;
}

