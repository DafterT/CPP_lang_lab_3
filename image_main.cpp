#include <benchmark/benchmark.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

#include "image_convolver.h" // Твой заголовочный файл

namespace {
constexpr int64_t kMinBenchmarkIterations = 10;
constexpr double kMinBenchmarkSeconds = 3.0;
}  // namespace

// Вспомогательная функция для генерации ядра Гаусса
// Нам не важна математическая точность значений для теста скорости, главное размер
std::vector<float> generateKernel(int dim) {
    std::vector<float> k(dim * dim);
    float sigma = std::max(dim / 6.0f, 1.0f); // Эвристика для сигмы
    float sum = 0.0f;
    int half = dim / 2;

    for (int y = -half; y <= half; ++y) {
        for (int x = -half; x <= half; ++x) {
            float val = std::exp(-(x * x + y * y) / (2 * sigma * sigma));
            // Корректировка индексов для вектора
            int idx = (y + half) * dim + (x + half);
            if(idx >= 0 && idx < dim*dim) {
               k[idx] = val;
               sum += val;
            }
        }
    }
    // Нормализация
    for (float& v : k) v /= sum;
    return k;
}

// Вспомогательная функция для генерации случайной картинки
std::vector<unsigned char> generateRandomImage(int w, int h) {
    std::vector<unsigned char> img(w * h * 4); // RGBA
    // Заполняем псевдослучайными числами. 
    // Для скорости используем простой LCG или memset, так как содержимое 
    // не влияет на скорость работы алгоритма (нет ветвлений от данных).
    // Но для честности заполним чем-то похожим на шум.
    for(size_t i = 0; i < img.size(); ++i) {
        img[i] = static_cast<unsigned char>(i % 256);
    }
    return img;
}

// Класс-фикстура, чтобы подготовить данные один раз перед серией замеров
class BlurFixture : public benchmark::Fixture {
public:
    std::vector<unsigned char> input_img;
    std::vector<float> kernel;
    ImageConvolver* convolver = nullptr;
    int w, h, kDim;

    void SetUp(const ::benchmark::State& state) {
        // Параметры приходят из state.range(i)
        // range(0) -> размер картинки (сторона квадрата)
        // range(1) -> размер ядра (сторона квадрата)
        int size = state.range(0);
        kDim = state.range(1);
        w = size;
        h = size;

        // Генерируем данные один раз
        if (input_img.empty()) {
            input_img = generateRandomImage(w, h);
            kernel = generateKernel(kDim);
            convolver = new ImageConvolver(kernel, kDim, kDim);
        }
    }

    void TearDown(const ::benchmark::State& state) {
        delete convolver;
        convolver = nullptr;
        input_img.clear();
        kernel.clear();
    }
};

// 1. Бенчмарк для DEFAULT (обычный C++)
BENCHMARK_DEFINE_F(BlurFixture, BM_ProcessDefault)(benchmark::State& state) {
    const int64_t batch = kMinBenchmarkIterations;
    while (state.KeepRunningBatch(batch)) {
        for (int64_t i = 0; i < batch; ++i) {
            // Код, который замеряем
            std::vector<unsigned char> res = convolver->process_default(input_img.data(), w, h);

            // clobber memory, чтобы компилятор не выкинул код
            benchmark::DoNotOptimize(res.data());
        }
    }

    // Устанавливаем метрику "Обработано байт в секунду"
    // 4 байта на пиксель * ширина * высота * кол-во итераций
    const int64_t total_iters = static_cast<int64_t>(state.iterations());
    state.SetBytesProcessed(total_iters * int64_t(w) * int64_t(h) * 4);
}

// 2. Бенчмарк для SIMD (AVX-512)
BENCHMARK_DEFINE_F(BlurFixture, BM_ProcessSIMD)(benchmark::State& state) {
    const int64_t batch = kMinBenchmarkIterations;
    while (state.KeepRunningBatch(batch)) {
        for (int64_t i = 0; i < batch; ++i) {
            std::vector<unsigned char> res = convolver->process_SIMD(input_img.data(), w, h);
            benchmark::DoNotOptimize(res.data());
        }
    }
    const int64_t total_iters = static_cast<int64_t>(state.iterations());
    state.SetBytesProcessed(total_iters * int64_t(w) * int64_t(h) * 4);
}

// 3. Бенчмарк для ThreadPool (многопоточная версия)
BENCHMARK_DEFINE_F(BlurFixture, BM_ProcessThreadPool)(benchmark::State& state) {
    size_t threads = static_cast<size_t>(state.range(2));
    const int64_t batch = kMinBenchmarkIterations;
    while (state.KeepRunningBatch(batch)) {
        for (int64_t i = 0; i < batch; ++i) {
            std::vector<unsigned char> res = convolver->process_thread_pool(input_img.data(), w, h, threads);
            benchmark::DoNotOptimize(res.data());
        }
    }
    const int64_t total_iters = static_cast<int64_t>(state.iterations());
    state.SetBytesProcessed(total_iters * int64_t(w) * int64_t(h) * 4);
}

// 4. Бенчмарк для ThreadPool (максимальная загрузка потоков)
BENCHMARK_DEFINE_F(BlurFixture, BM_ProcessThreadPoolFull)(benchmark::State& state) {
    size_t threads = static_cast<size_t>(state.range(2));
    const int64_t batch = kMinBenchmarkIterations;
    while (state.KeepRunningBatch(batch)) {
        for (int64_t i = 0; i < batch; ++i) {
            std::vector<unsigned char> res = convolver->process_thread_pool_full(input_img.data(), w, h, threads);
            benchmark::DoNotOptimize(res.data());
        }
    }
    const int64_t total_iters = static_cast<int64_t>(state.iterations());
    state.SetBytesProcessed(total_iters * int64_t(w) * int64_t(h) * 4);
}

// Регистрируем бенчмарки с аргументами
// ArgPair(ImageSize, KernelSize)

// Генерируем список аргументов
static std::vector<int> BuildThreadCounts() {
    unsigned int hw = std::thread::hardware_concurrency();
    if (hw == 0) {
        hw = 1;
    }

    std::vector<int> candidates = {
        1,
        static_cast<int>(hw / 2),
        static_cast<int>(hw),
        static_cast<int>(hw * 2)
    };

    std::vector<int> unique_counts;
    unique_counts.reserve(candidates.size());

    for (int count : candidates) {
        if (count < 1) {
            count = 1;
        }
        if (std::find(unique_counts.begin(), unique_counts.end(), count) == unique_counts.end()) {
            unique_counts.push_back(count);
        }
    }

    return unique_counts;
}

static void CustomArguments(benchmark::internal::Benchmark* b) {
    std::vector<int> imgSizes = {32, 64, 128, 256, 512, 1024, 2048, 4096};
    std::vector<int> kernelSizes = {3, 5, 7, 9};

    for (int is : imgSizes) {
        for (int ks : kernelSizes) {
            b->Args({is, ks});
        }
    }
}

static void CustomArgumentsThreadPool(benchmark::internal::Benchmark* b) {
    std::vector<int> imgSizes = {32, 64, 128, 256, 512, 1024, 2048, 4096};
    std::vector<int> kernelSizes = {3, 5, 7, 9};
    std::vector<int> threadCounts = BuildThreadCounts();

    for (int is : imgSizes) {
        for (int ks : kernelSizes) {
            for (int threads : threadCounts) {
                b->Args({is, ks, threads});
            }
        }
    }
}

BENCHMARK_REGISTER_F(BlurFixture, BM_ProcessDefault)
    ->Apply(CustomArguments)
    ->UseRealTime()
    ->Unit(benchmark::kMicrosecond) // Вывод времени
    ->MinTime(kMinBenchmarkSeconds);

BENCHMARK_REGISTER_F(BlurFixture, BM_ProcessSIMD)
    ->Apply(CustomArguments)
    ->UseRealTime()
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(kMinBenchmarkSeconds);

BENCHMARK_REGISTER_F(BlurFixture, BM_ProcessThreadPool)
    ->Apply(CustomArgumentsThreadPool)
    ->UseRealTime()
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(kMinBenchmarkSeconds);

BENCHMARK_REGISTER_F(BlurFixture, BM_ProcessThreadPoolFull)
    ->Apply(CustomArgumentsThreadPool)
    ->UseRealTime()
    ->Unit(benchmark::kMicrosecond)
    ->MinTime(kMinBenchmarkSeconds);

BENCHMARK_MAIN();
