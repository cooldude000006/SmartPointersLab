#include <iostream>

#include "UnqPtr.h"

// Контрольная программа с НАМЕРЕННОЙ утечкой: Release() отдаёт владение,
// но объект никто не удаляет. valgrind и AddressSanitizer обязаны это найти -
// так проверяется, что сама проверка на утечки работает (scripts/check_memory.sh).
int main()
{
    UnqPtr<int> pointer = MakeUnq<int>(42);
    int* leaked = pointer.Release();
    std::cout << "leaked value: " << *leaked << '\n';
    return 0;
}
