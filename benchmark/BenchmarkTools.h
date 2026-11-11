#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <vector>

// Запрет встраивания функции: иначе компилятор может «склеить» вызов
// с циклом и выбросить измеряемую работу (noclone - против клонирования в GCC)
#if defined(__clang__)
#define BENCHMARK_NOINLINE __attribute__((noinline))
#elif defined(__GNUC__)
#define BENCHMARK_NOINLINE __attribute__((noinline, noclone))
#elif defined(_MSC_VER)
#define BENCHMARK_NOINLINE __declspec(noinline)
#else
#define BENCHMARK_NOINLINE
#endif

// Сообщает компилятору, что value «используется», и сбрасывает все изменения
// в память. Без этого оптимизатор вправе удалить пару new/delete или пару
// ++счётчик/--счётчик, и замер покажет ноль.
template <typename T>
inline void DoNotOptimize(T& value)
{
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : "+m"(value) : : "memory");
#else
    static const void* volatile sink = nullptr;
    sink = &value;
#endif
}

struct Measurement
{
    double totalMilliseconds;       // время на count операций
    double nanosecondsPerOperation; // время одной операции
};

// Замер run(count). При малых count прогон повторяется, пока суммарно не наберётся
// kMinOperations операций (иначе время меньше точности таймера). Делается несколько
// попыток, результат - медиана: она устойчива к случайным выбросам (другие процессы, ОС).
// isQuick - один прогон без повторов и прогрева (проверка под valgrind, где важна
// не точность времени, а то, что код выполняется).
template <typename Function>
Measurement Measure(std::size_t count, Function&& run, bool isQuick = false)
{
    const std::size_t kMinOperations = 300000;
    const std::size_t kLargeCount = 10000000;
    const int kTrials = 5;
    const int kLargeTrials = 3;

    if (count == 0)
    {
        return {0.0, 0.0};
    }

    const std::size_t repetitions = isQuick || count >= kMinOperations ? 1 : kMinOperations / count;
    const int trials = isQuick ? 1 : (count >= kLargeCount ? kLargeTrials : kTrials);

    // Прогрев: кэши, страницы памяти аллокатора
    if (!isQuick && count < kLargeCount)
    {
        run(count);
    }

    std::vector<double> nanosecondsPerOperation;
    for (int trial = 0; trial < trials; ++trial)
    {
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t repetition = 0; repetition < repetitions; ++repetition)
        {
            run(count);
        }
        const auto finish = std::chrono::steady_clock::now();

        const double elapsed = std::chrono::duration<double, std::nano>(finish - start).count();
        nanosecondsPerOperation.push_back(elapsed / (static_cast<double>(repetitions) * static_cast<double>(count)));
    }

    std::sort(nanosecondsPerOperation.begin(), nanosecondsPerOperation.end());
    const double median = nanosecondsPerOperation[nanosecondsPerOperation.size() / 2];
    return {median * static_cast<double>(count) / 1e6, median};
}
