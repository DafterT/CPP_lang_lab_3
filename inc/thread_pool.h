#pragma once

#include <functional>
#include <future>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <type_traits>

/**
 * @brief Пул потоков для асинхронного выполнения задач.
 *
 * Класс предоставляет интерфейс для выполнения задач в отдельных потоках.
 * Задачи помещаются в очередь и выполняются доступными потоками.
 * Деструктор гарантирует завершение всех поставленных задач.
 */
class ThreadPool {
public:
    /**
     * @brief Конструктор пула потоков.
     *
     * @param num_threads Количество рабочих потоков.
     *                   Если равно 0, будет создано количество потоков
     *                   равное количеству аппаратных ядер.
     */
    explicit ThreadPool(size_t num_threads = 0);

    /**
     * @brief Запрет копирования и присваивания.
     */
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /**
     * @brief Деструктор пула потоков.
     *
     * Гарантирует завершение всех поставленных задач перед уничтожением пула.
     */
    ~ThreadPool();

    /**
     * @brief Добавляет задачу в очередь на выполнение.
     *
     * @tparam Fn Тип функции задачи.
     * @tparam T Тип возвращаемого значения функции.
     * @param f Функция для выполнения.
     * @return std::future<T> Объект для получения результата выполнения.
     */
    template<typename Fn, typename T = typename std::invoke_result_t<Fn>>
    std::future<T> dispatch_task(Fn&& f);

    /**
     * @brief Возвращает количество рабочих потоков.
     */
    size_t get_thread_count() const;

    /**
     * @brief Возвращает количество задач в очереди.
     */
    size_t get_queue_size() const;

private:
    /**
     * @brief Структура для хранения задачи в очереди.
     *
     * Использует std::function для хранения любого callable объекта
     * и std::promise для возврата результата.
     */
    struct Task {
        std::function<void()> func;  ///< Функция для выполнения

        /**
         * @brief Конструктор задачи.
         * @param f Функция для выполнения.
         */
        explicit Task(std::function<void()> f) : func(std::move(f)) {}
    };

    /**
     * @brief Основной цикл рабочего потока.
     *
     * Ожидает задачи из очереди и выполняет их.
     */
    void worker_thread();

    /**
     * @brief Останавливает все рабочие потоки.
     *
     * Устанавливает флаг остановки и уведомляет все ожидающие потоки.
     */
    void stop_all_threads();

    std::vector<std::thread> m_workers;        ///< Вектор рабочих потоков
    std::queue<Task> m_tasks;                  ///< Очередь задач
    mutable std::mutex m_queue_mutex;          ///< Мьютекс для синхронизации доступа к очереди
    std::condition_variable m_condition;       ///< Условная переменная для уведомления потоков
    std::atomic<bool> m_stop{false};           ///< Флаг остановки пула потоков
};

