#include "thread_pool.h"
#include <iostream>

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

void ThreadPool::worker_thread() {
    while (true) {
        Task task;

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
