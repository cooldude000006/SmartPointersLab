#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "AllocationCounter.h"
#include "BenchmarkTools.h"
#include "ShrdPtr.h"
#include "UnqPtr.h"

// Нагрузочные тесты: сравнение сырого указателя, наших UnqPtr/ShrdPtr и STL
// по времени (несколько сценариев) и по памяти. Результаты печатаются таблицами
// и сохраняются в CSV (по ним строятся графики). Запускать в сборке Release.

namespace
{

// Объект, на который указывают указатели в замерах
struct Payload
{
    explicit Payload(int initialValue) : value(initialValue)
    {
    }

    int value;
};

// Объект для сценариев с сырым указателем, где удалять ничего не нужно
Payload sharedPayload(1);

const std::size_t kArrayLength = 16;            // длина массива в сценариях с массивами
const std::size_t kDefaultMaxCount = 10000000;   // 10^7
const std::size_t kMaxAllowedCount = 1000000000; // 10^9
const std::size_t kMemoryMaxCount = 1000000;     // замер памяти держит все объекты живыми

// Роль варианта: «ускорение» в таблице считается как время Std / время Ours
enum class Role
{
    Raw,
    Ours,
    Std,
    Other
};

struct Variant
{
    std::string name;
    Role role;
    std::function<void(std::size_t)> run;
};

struct Scenario
{
    std::string name;
    std::string description;
    std::vector<Variant> variants;
};

struct TimeResult
{
    std::string scenario;
    std::string variant;
    std::size_t count;
    Measurement measurement;
};

struct MemoryResult
{
    std::string variant;
    std::size_t count;
    std::size_t totalBytes;
    std::size_t allocationCount;
};

struct Options
{
    std::size_t maxCount;
    std::filesystem::path outputDirectory;
    bool isQuick; // один прогон без повторов - для valgrind и санитайзеров
};

// ---------------------------------------------------------------------------
// Сценарии замера времени
// ---------------------------------------------------------------------------

// Создание и удаление объекта через new/delete
void RawCreateDestroy(std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        Payload* pointer = new Payload(static_cast<int>(i));
        DoNotOptimize(pointer);
        delete pointer;
    }
}

void RawArrayCreateDestroy(std::size_t count)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        int* array = new int[kArrayLength]();
        DoNotOptimize(array);
        delete[] array;
    }
}

// Создание и удаление объекта через умный указатель (удаляет деструктор)
template <typename Factory>
void SmartCreateDestroy(std::size_t count, Factory make)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        auto pointer = make(static_cast<int>(i));
        DoNotOptimize(pointer);
    }
}

// Копирование указателя и уничтожение копии (у разделяемых: ++ и -- счётчика)
template <typename Pointer>
void CopyAndDestroy(std::size_t count, const Pointer& source)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        Pointer copy(source);
        DoNotOptimize(copy);
    }
}

template <typename Pointer>
BENCHMARK_NOINLINE int ReadByValue(Pointer pointer)
{
    return pointer->value;
}

// Передача указателя в функцию по значению
template <typename Pointer>
void PassByValue(std::size_t count, const Pointer& source)
{
    int sum = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        sum += ReadByValue<Pointer>(source);
        DoNotOptimize(sum);
    }
}

std::vector<Scenario> MakeScenarios()
{
    std::vector<Scenario> scenarios;

    scenarios.push_back({"unique_create", "create + destroy one object, unique ownership", {
        {"raw new/delete", Role::Raw, RawCreateDestroy},
        {"UnqPtr", Role::Ours, [](std::size_t count)
            {
                SmartCreateDestroy(count, [](int value) { return MakeUnq<Payload>(value); });
            }},
        {"std::unique_ptr", Role::Std, [](std::size_t count)
            {
                SmartCreateDestroy(count, [](int value) { return std::make_unique<Payload>(value); });
            }},
    }});

    scenarios.push_back({"shared_create", "create + destroy one object, shared ownership", {
        {"raw new/delete", Role::Raw, RawCreateDestroy},
        {"ShrdPtr", Role::Ours, [](std::size_t count)
            {
                SmartCreateDestroy(count, [](int value) { return MakeShrd<Payload>(value); });
            }},
        {"std::shared_ptr", Role::Std, [](std::size_t count)
            {
                SmartCreateDestroy(count, [](int value) { return std::shared_ptr<Payload>(new Payload(value)); });
            }},
        {"std::make_shared", Role::Other, [](std::size_t count)
            {
                SmartCreateDestroy(count, [](int value) { return std::make_shared<Payload>(value); });
            }},
    }});

    // Исходный указатель создаётся один раз (захват в лямбду), чтобы его
    // создание не попадало в замер при малых N
    scenarios.push_back({"shared_copy", "copy pointer + destroy copy", {
        {"raw pointer", Role::Raw, [](std::size_t count)
            {
                CopyAndDestroy<Payload*>(count, &sharedPayload);
            }},
        {"ShrdPtr", Role::Ours, [source = MakeShrd<Payload>(1)](std::size_t count)
            {
                CopyAndDestroy(count, source);
            }},
        {"std::shared_ptr", Role::Std, [source = std::make_shared<Payload>(1)](std::size_t count)
            {
                CopyAndDestroy(count, source);
            }},
    }});

    scenarios.push_back({"pass_by_value", "pass pointer to function by value", {
        {"raw pointer", Role::Raw, [](std::size_t count)
            {
                PassByValue<Payload*>(count, &sharedPayload);
            }},
        {"ShrdPtr", Role::Ours, [source = MakeShrd<Payload>(1)](std::size_t count)
            {
                PassByValue(count, source);
            }},
        {"std::shared_ptr", Role::Std, [source = std::make_shared<Payload>(1)](std::size_t count)
            {
                PassByValue(count, source);
            }},
    }});

    scenarios.push_back({"unique_array_create", "create + destroy int[16], unique ownership", {
        {"raw new[]/delete[]", Role::Raw, RawArrayCreateDestroy},
        {"UnqPtr<T[]>", Role::Ours, [](std::size_t count)
            {
                SmartCreateDestroy(count, [](int) { return MakeUnq<int[]>(kArrayLength); });
            }},
        {"std::unique_ptr<T[]>", Role::Std, [](std::size_t count)
            {
                SmartCreateDestroy(count, [](int) { return std::make_unique<int[]>(kArrayLength); });
            }},
    }});

    scenarios.push_back({"shared_array_create", "create + destroy int[16], shared ownership", {
        {"raw new[]/delete[]", Role::Raw, RawArrayCreateDestroy},
        {"ShrdPtr<T[]>", Role::Ours, [](std::size_t count)
            {
                SmartCreateDestroy(count, [](int) { return MakeShrd<int[]>(kArrayLength); });
            }},
        {"std::shared_ptr<T[]>", Role::Std, [](std::size_t count)
            {
                SmartCreateDestroy(count, [](int) { return std::shared_ptr<int[]>(new int[kArrayLength]()); });
            }},
    }});

    return scenarios;
}

// ---------------------------------------------------------------------------
// Замер памяти: count объектов живут одновременно, указатели хранятся в векторе
// ---------------------------------------------------------------------------
template <typename Pointer, typename Factory>
MemoryResult MeasureMemory(const std::string& name, std::size_t count, Factory make)
{
    std::vector<Pointer> pointers;
    pointers.reserve(count);

    AllocationCounter::Start();
    for (std::size_t i = 0; i < count; ++i)
    {
        pointers.push_back(make(static_cast<int>(i)));
    }
    AllocationCounter::Stop();

    // Память = сами указатели + всё, что они выделили в куче (объекты, счётчики)
    const MemoryResult result = {name, count,
                                 count * sizeof(Pointer) + AllocationCounter::GetAllocatedBytes(),
                                 AllocationCounter::GetAllocationCount()};

    if constexpr (std::is_pointer<Pointer>::value)
    {
        for (Pointer pointer : pointers)
        {
            delete pointer;
        }
    }
    return result;
}

std::vector<MemoryResult> MeasureAllMemory(std::size_t count)
{
    return {
        MeasureMemory<Payload*>("raw pointer", count, [](int value) { return new Payload(value); }),
        MeasureMemory<UnqPtr<Payload>>("UnqPtr", count, [](int value) { return MakeUnq<Payload>(value); }),
        MeasureMemory<std::unique_ptr<Payload>>("std::unique_ptr", count,
                                                [](int value) { return std::make_unique<Payload>(value); }),
        MeasureMemory<ShrdPtr<Payload>>("ShrdPtr", count, [](int value) { return MakeShrd<Payload>(value); }),
        MeasureMemory<std::shared_ptr<Payload>>("std::shared_ptr", count,
                                                [](int value) { return std::shared_ptr<Payload>(new Payload(value)); }),
        MeasureMemory<std::shared_ptr<Payload>>("std::make_shared", count,
                                                [](int value) { return std::make_shared<Payload>(value); }),
    };
}

// ---------------------------------------------------------------------------
// Вывод таблиц
// ---------------------------------------------------------------------------
int ColumnWidth(const std::string& title)
{
    const int kMinWidth = 12;
    return std::max(kMinWidth, static_cast<int>(title.size()) + 2);
}

const Variant* FindVariant(const Scenario& scenario, Role role)
{
    for (const Variant& variant : scenario.variants)
    {
        if (variant.role == role)
        {
            return &variant;
        }
    }
    return nullptr;
}

// Таблица одного сценария: строки - N, столбцы - варианты (нс на операцию)
std::vector<TimeResult> RunScenario(const Scenario& scenario, const std::vector<std::size_t>& counts, bool isQuick)
{
    const Variant* ours = FindVariant(scenario, Role::Ours);
    const Variant* standard = FindVariant(scenario, Role::Std);

    std::cout << "=== " << scenario.name << ": " << scenario.description << " (ns per operation) ===\n";
    std::cout << std::setw(12) << "N";
    for (const Variant& variant : scenario.variants)
    {
        std::cout << std::setw(ColumnWidth(variant.name)) << variant.name;
    }
    std::cout << std::setw(ColumnWidth("std/ours")) << "std/ours" << '\n';

    std::vector<TimeResult> results;
    for (std::size_t count : counts)
    {
        std::cout << std::setw(12) << count << std::flush;
        double oursTime = 0.0;
        double standardTime = 0.0;

        for (const Variant& variant : scenario.variants)
        {
            const Measurement measurement = Measure(count, variant.run, isQuick);
            results.push_back({scenario.name, variant.name, count, measurement});

            if (&variant == ours)
            {
                oursTime = measurement.nanosecondsPerOperation;
            }
            if (&variant == standard)
            {
                standardTime = measurement.nanosecondsPerOperation;
            }
            std::cout << std::setw(ColumnWidth(variant.name)) << std::fixed << std::setprecision(2)
                      << measurement.nanosecondsPerOperation << std::flush;
        }

        std::cout << std::setw(ColumnWidth("std/ours"));
        if (oursTime > 0.0)
        {
            std::cout << standardTime / oursTime;
        }
        else
        {
            std::cout << "-";
        }
        std::cout << '\n';
    }
    std::cout << '\n';
    return results;
}

void PrintMemoryTable(const std::vector<MemoryResult>& results)
{
    std::cout << "=== memory: N objects alive at once (pointer + requested heap bytes, without malloc overhead) ===\n";
    std::cout << std::setw(20) << "variant" << std::setw(12) << "N" << std::setw(14) << "total, MB"
              << std::setw(16) << "bytes/object" << std::setw(16) << "allocs/object" << '\n';

    for (const MemoryResult& result : results)
    {
        if (result.count != results.back().count)
        {
            continue; // в консоль - только наибольшее N, все N - в CSV
        }
        const double count = static_cast<double>(result.count);
        std::cout << std::setw(20) << result.variant << std::setw(12) << result.count << std::fixed
                  << std::setprecision(2) << std::setw(14) << static_cast<double>(result.totalBytes) / (1024.0 * 1024.0)
                  << std::setw(16) << static_cast<double>(result.totalBytes) / count << std::setw(16)
                  << static_cast<double>(result.allocationCount) / count << '\n';
    }
    std::cout << '\n';
}

// ---------------------------------------------------------------------------
// CSV
// ---------------------------------------------------------------------------
void WriteTimeCsv(const std::filesystem::path& file, const std::vector<TimeResult>& results)
{
    std::ofstream output(file);
    if (!output)
    {
        throw std::runtime_error("cannot open " + file.string());
    }
    output << "scenario,variant,count,total_ms,ns_per_op\n";
    output << std::setprecision(6);
    for (const TimeResult& result : results)
    {
        output << result.scenario << ',' << result.variant << ',' << result.count << ','
               << result.measurement.totalMilliseconds << ',' << result.measurement.nanosecondsPerOperation << '\n';
    }
}

void WriteMemoryCsv(const std::filesystem::path& file, const std::vector<MemoryResult>& results)
{
    std::ofstream output(file);
    if (!output)
    {
        throw std::runtime_error("cannot open " + file.string());
    }
    output << "variant,count,total_bytes,bytes_per_object,allocations_per_object\n";
    output << std::setprecision(6);
    for (const MemoryResult& result : results)
    {
        const double count = static_cast<double>(result.count);
        output << result.variant << ',' << result.count << ',' << result.totalBytes << ','
               << static_cast<double>(result.totalBytes) / count << ','
               << static_cast<double>(result.allocationCount) / count << '\n';
    }
}

// ---------------------------------------------------------------------------
// Параметры командной строки
// ---------------------------------------------------------------------------
void PrintUsage(const char* programName)
{
    std::cout << "Usage: " << programName << " [--max N] [--out DIR] [--quick]\n"
              << "  --max N    largest number of operations, 1..1e9 (default 1e7)\n"
              << "  --out DIR  directory for CSV results (default: <project>/results)\n"
              << "  --quick    single run per measurement (for valgrind / sanitizers)\n";
}

// Принимает и целые числа, и запись вида 1e8
bool ParseCount(const std::string& text, std::size_t& count)
{
    try
    {
        std::size_t parsedLength = 0;
        const double value = std::stod(text, &parsedLength);
        if (parsedLength != text.size() || value < 1.0 || value > static_cast<double>(kMaxAllowedCount))
        {
            return false;
        }
        count = static_cast<std::size_t>(value);
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

bool ParseOptions(int argc, char* argv[], Options& options)
{
    options.maxCount = kDefaultMaxCount;
    options.isQuick = false;
    // Путь к проекту в UTF-8 (может содержать кириллицу) - u8path
    options.outputDirectory = std::filesystem::u8path(RESULTS_DIR);

    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        const bool hasValue = i + 1 < argc;

        if (argument == "--max" && hasValue)
        {
            if (!ParseCount(argv[++i], options.maxCount))
            {
                return false;
            }
        }
        else if (argument == "--quick")
        {
            options.isQuick = true;
        }
        else if (argument == "--out" && hasValue)
        {
            options.outputDirectory = argv[++i];
        }
        else
        {
            return false;
        }
    }
    return true;
}

// 1, 10, 100, ... до maxCount (и сам maxCount, если он не степень 10)
std::vector<std::size_t> MakeCounts(std::size_t maxCount)
{
    const std::size_t kBase = 10;
    std::vector<std::size_t> counts;
    for (std::size_t count = 1; count <= maxCount; count *= kBase)
    {
        counts.push_back(count);
        if (count > maxCount / kBase)
        {
            break;
        }
    }
    if (counts.back() != maxCount)
    {
        counts.push_back(maxCount);
    }
    return counts;
}

} // namespace

int main(int argc, char* argv[])
{
    Options options;
    if (!ParseOptions(argc, argv, options))
    {
        PrintUsage(argv[0]);
        return 1;
    }

#ifndef NDEBUG
    std::cout << "WARNING: this is a Debug build, the timings are not representative. Use Release.\n\n";
#endif

    try
    {
        const std::vector<std::size_t> counts = MakeCounts(options.maxCount);

        std::vector<TimeResult> timeResults;
        for (const Scenario& scenario : MakeScenarios())
        {
            const std::vector<TimeResult> scenarioResults = RunScenario(scenario, counts, options.isQuick);
            timeResults.insert(timeResults.end(), scenarioResults.begin(), scenarioResults.end());
        }

        std::vector<MemoryResult> memoryResults;
        for (std::size_t count : MakeCounts(std::min(options.maxCount, kMemoryMaxCount)))
        {
            const std::vector<MemoryResult> countResults = MeasureAllMemory(count);
            memoryResults.insert(memoryResults.end(), countResults.begin(), countResults.end());
        }
        PrintMemoryTable(memoryResults);

        std::filesystem::create_directories(options.outputDirectory);
        WriteTimeCsv(options.outputDirectory / "time.csv", timeResults);
        WriteMemoryCsv(options.outputDirectory / "memory.csv", memoryResults);
        std::cout << "CSV saved to " << (options.outputDirectory / "time.csv").string() << " and memory.csv\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
