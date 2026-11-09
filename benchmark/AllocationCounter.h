#pragma once

#include <cstddef>

// Подсчёт динамических выделений памяти. Глобальный operator new заменён
// (AllocationCounter.cpp) и, пока подсчёт включён, суммирует число вызовов
// и запрошенные байты. Служебные заголовки аллокатора (malloc) не учитываются.
namespace AllocationCounter
{

// Обнуляет счётчики и включает подсчёт
void Start();

void Stop();

std::size_t GetAllocationCount();

std::size_t GetAllocatedBytes();

} // namespace AllocationCounter
