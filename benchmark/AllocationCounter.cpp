#include "AllocationCounter.h"

#include <cstdlib>
#include <new>

namespace
{

// Замеры однопоточные, поэтому счётчики обычные, не атомарные
bool isCounting = false;
std::size_t allocationCount = 0;
std::size_t allocatedBytes = 0;

} // namespace

namespace AllocationCounter
{

void Start()
{
    allocationCount = 0;
    allocatedBytes = 0;
    isCounting = true;
}

void Stop()
{
    isCounting = false;
}

std::size_t GetAllocationCount()
{
    return allocationCount;
}

std::size_t GetAllocatedBytes()
{
    return allocatedBytes;
}

} // namespace AllocationCounter

// ---------------------------------------------------------------------------
// Замена глобальных operator new / operator delete.
// Массивные формы (new[] / delete[]) и nothrow-формы по стандарту вызывают эти.
// ---------------------------------------------------------------------------
void* operator new(std::size_t size)
{
    if (isCounting)
    {
        ++allocationCount;
        allocatedBytes += size;
    }

    void* memory = std::malloc(size == 0 ? 1 : size);
    if (memory == nullptr)
    {
        throw std::bad_alloc();
    }
    return memory;
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}
