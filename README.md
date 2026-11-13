# Лабораторная работа №1. Умные указатели

Курс «Информатика (организация и поиск данных)», 3 семестр, НИЯУ МИФИ.

Реализация идиомы «умный указатель» без использования STL-указателей
(вариант 3 ТЗ: децентрализованное хранение, счётчик ссылок `size_t* referenceCount`
только в `ShrdPtr`), функциональные и нагрузочные тесты, проверка отсутствия утечек
valgrind и санитайзерами.

Техническое задание (сущности, связи, функциональные и нефункциональные требования):
[docs/TZ.md](docs/TZ.md). Теория к защите: [docs/THEORY.md](docs/THEORY.md).

## Что реализовано

| Класс | Файл | Назначение |
|---|---|---|
| `UnqPtr<T>` | [include/UnqPtr.h](include/UnqPtr.h) | единоличный владелец объекта (аналог `std::unique_ptr`), только перемещение |
| `UnqPtr<T[]>` | [include/UnqPtr.h](include/UnqPtr.h) | то же для массива: `delete[]`, `operator[]` |
| `ShrdPtr<T>` | [include/ShrdPtr.h](include/ShrdPtr.h) | разделяемое владение (аналог `std::shared_ptr`), счётчик `size_t*` |
| `ShrdPtr<T[]>` | [include/ShrdPtr.h](include/ShrdPtr.h) | то же для массива |
| `DynamicArray<T>` | [include/DynamicArray.h](include/DynamicArray.h) | контейнер поверх `UnqPtr<T[]>` - проверка указателей на практике |

Возможности:
- **подтипизация**: `ShrdPtr<Base> = ShrdPtr<Derived>` и `UnqPtr<Base> = UnqPtr<Derived>&&`
  работают, для несвязанных типов - ошибка компиляции (SFINAE через `std::is_convertible`);
  для массивов подтипизация запрещена;
- фабрики `MakeUnq<T>(args...)`, `MakeUnq<T[]>(n)`, `MakeShrd<T>(args...)`, `MakeShrd<T[]>(n)`;
- `ShrdPtr` из `UnqPtr&&` (передача владения);
- корректное присваивание самому себе (`p = p`, `p = std::move(p)`);
- разыменование пустого указателя бросает `std::logic_error`;
- защита от исключений: если не удалось выделить счётчик, объект удаляется, утечки нет.

## Структура проекта

```
include/      умные указатели и DynamicArray (header-only: шаблоны)
tests/        GoogleTest: TestUnqPtr, TestShrdPtr, TestDynamicArray;
              Tracked.h - счётчик объектов; LeakDemo - намеренная утечка
benchmark/    нагрузочные тесты, подсчёт памяти (замена operator new)
scripts/      plot_results.py - графики; check_memory.sh - valgrind и санитайзеры
results/      таблицы, CSV, графики (plots/), логи проверки памяти (memcheck/)
docs/         TZ.md - техническое задание; THEORY.md - теория к защите
```

## Сборка и запуск

Требования: CMake ≥ 3.14, компилятор C++17 (GCC, Clang, MinGW). GoogleTest скачивается
автоматически при настройке CMake.

В CLion: открыть папку проекта, выбрать конфигурацию `SmartPointersTests` или
`SmartPointersBenchmark`. Для бенчмарка нужен профиль **Release**
(Settings → Build, Execution, Deployment → CMake → `+`).

Из командной строки:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/SmartPointersTests
./build/SmartPointersBenchmark --max 1e8
python scripts/plot_results.py
```

Параметры бенчмарка: `--max N` - наибольшее N (по умолчанию 1e7), `--out DIR` - папка
для CSV (по умолчанию `results/`), `--quick` - один прогон без повторов (для valgrind).

### Проверка памяти (Linux / WSL)

```bash
bash scripts/check_memory.sh
```

Скрипт собирает проект в Debug и с `-DSANITIZE=ON` (AddressSanitizer +
UndefinedBehaviorSanitizer), прогоняет тесты и бенчмарк под valgrind и санитайзерами,
затем проверяет, что намеренная утечка в `LeakDemo` обнаруживается. Логи - `results/memcheck/`.

## Результаты

Windows 10, AMD Ryzen 5 3550H, MinGW GCC 13.1, Release (`-O3`). Таблицы:
[results/tables.txt](results/tables.txt), данные: [results/time.csv](results/time.csv),
[results/memory.csv](results/memory.csv), графики: [results/plots](results/plots).

Время одной операции, нс, N = 10⁸:

| Сценарий | сырой | наш | STL | STL / наш |
|---|---|---|---|---|
| копирование `ShrdPtr` + уничтожение копии | 0,30 | 3,95 | 15,11 | **3,8×** |
| передача `ShrdPtr` в функцию по значению | 2,25 | 3,94 | 15,64 | **4,0×** |
| создание + удаление, `UnqPtr` | 59,0 | 52,2 | 51,6 | ≈1 |
| создание + удаление, `ShrdPtr` | 51,5 | 106,3 | 107,3 | ≈1 |
| то же, `std::make_shared` | | 106,3 | 57,6 | 0,54 |
| массив `int[16]`, `UnqPtr<T[]>` | 54,0 | 54,9 | 55,6 | ≈1 |
| массив `int[16]`, `ShrdPtr<T[]>` | 53,8 | 109,3 | 117,6 | 1,08× |

Память на объект (указатель + запрошенная куча): `UnqPtr` - 12 Б (как сырой указатель),
`ShrdPtr` - 28 Б, `std::shared_ptr` - 44 Б, `std::make_shared` - 40 Б.

Выводы:
- операции со счётчиком у `ShrdPtr` в ~4 раза быстрее: обычный `++`/`--` вместо атомарных
  операций `std::shared_ptr` (цена - нет потокобезопасности);
- время создания определяется `malloc`/`free` (~50 нс на выделение); `ShrdPtr` выделяет
  память дважды (объект + счётчик), поэтому `std::make_shared` (один блок) быстрее при создании;
- `UnqPtr` не добавляет накладных расходов ни по времени, ни по памяти;
- суммарное время линейно по N на всём диапазоне 1..10⁸.

Проверка памяти (Ubuntu 26.04 / WSL2, GCC 15.2, valgrind 3.26): 58 тестов и бенчмарк -
`All heap blocks were freed -- no leaks are possible`, `0 errors`; ASan + UBSan - без ошибок;
контрольная утечка в `LeakDemo` обнаружена обоими инструментами.
