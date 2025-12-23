#include "thread_pool.h"
#include <iostream>
#include <utility>

ThreadPool::ThreadPool(size_t num_threads) : m_stop(false) {
    // Если количество потоков не указано, используем количество аппаратных ядер
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        // Если hardware_concurrency() вернул 0, устанавливаем минимум 1 поток
        if (num_threads == 0) {
            num_threads = 1;
        }
    }

    // Создаем рабочие потоки
    m_workers.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        m_workers.emplace_back(&ThreadPool::worker_thread, this);
    }
}

ThreadPool::~ThreadPool() {
    stop_all_threads();

    // Ожидаем завершения всех рабочих потоков
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::stop_all_threads() {
    {
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        m_stop = true;
    }
    // Уведомляем все ожидающие потоки о необходимости завершения
    m_condition.notify_all();
}

template<typename Fn, typename T>
std::future<T> ThreadPool::dispatch_task(Fn&& f) {
    // Создаем promise для возврата результата
    auto promise = std::make_shared<std::promise<T>>();
    std::future<T> future = promise->get_future();

    // Создаем задачу, которая выполнит функцию и установит результат в promise
    auto task_func = [func = std::forward<Fn>(f), promise]() mutable {
        try {
            if constexpr (std::is_void_v<T>) {
                // Для функций без возвращаемого значения
                std::invoke(std::forward<Fn>(func));
                promise->set_value();
            } else {
                // Для функций с возвращаемым значением
                T result = std::invoke(std::forward<Fn>(func));
                promise->set_value(std::move(result));
            }
        } catch (...) {
            // Если функция бросила исключение, передаем его через promise
            try {
                promise->set_exception(std::current_exception());
            } catch (...) {
                // Если не удалось установить исключение, просто игнорируем
                // (это может произойти если future уже был уничтожен)
            }
        }
    };

    // Добавляем задачу в очередь
    {
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        if (m_stop) {
            throw std::runtime_error("Cannot dispatch task: ThreadPool is stopped");
        }
        m_tasks.emplace(std::move(task_func));
    }

    // Уведомляем один из ожидающих потоков
    m_condition.notify_one();

    return future;
}

void ThreadPool::worker_thread() {
    while (true) {
        Task task(nullptr);

        // Получаем задачу из очереди
        {
            std::unique_lock<std::mutex> lock(m_queue_mutex);

            // Ожидаем задачу или сигнал остановки
            m_condition.wait(lock, [this]() {
                return m_stop || !m_tasks.empty();
            });

            // Если пул остановлен и задач нет, выходим
            if (m_stop && m_tasks.empty()) {
                return;
            }

            // Извлекаем задачу из очереди
            if (!m_tasks.empty()) {
                task = std::move(m_tasks.front());
                m_tasks.pop();
            }
        }

        // Выполняем задачу вне критической секции
        if (task.func) {
            task.func();
        }
    }
}

size_t ThreadPool::get_thread_count() const {
    return m_workers.size();
}

size_t ThreadPool::get_queue_size() const {
    std::unique_lock<std::mutex> lock(m_queue_mutex);
    return m_tasks.size();
}
