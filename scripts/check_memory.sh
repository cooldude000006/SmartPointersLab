#!/usr/bin/env bash
# Проверка на утечки и ошибки памяти. Запускать в Linux / WSL из корня проекта:
#     bash scripts/check_memory.sh
#
#  1. valgrind memcheck: функциональные тесты и бенчмарк (сборка Debug)
#  2. AddressSanitizer + UndefinedBehaviorSanitizer: тесты и бенчмарк
#  3. Контроль: LeakDemo с намеренной утечкой - инструменты обязаны её найти
#
# Логи сохраняются в results/memcheck/. Код возврата 0 - ошибок нет.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BUILD_DIR:-$ROOT/build-linux}"
LOGS="$ROOT/results/memcheck"
SCRATCH="$BUILD/scratch-results"   # CSV бенчмарка при проверке не нужны

VALGRIND_OPTIONS=(--leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all
                  --track-origins=yes --error-exitcode=1)
export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"

mkdir -p "$LOGS" "$SCRATCH"

echo "=== [1/3] valgrind ==="
cmake -S "$ROOT" -B "$BUILD/debug" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSANITIZE=OFF > /dev/null
cmake --build "$BUILD/debug" --target SmartPointersTests SmartPointersBenchmark LeakDemo

valgrind "${VALGRIND_OPTIONS[@]}" "$BUILD/debug/SmartPointersTests" 2>&1 | tee "$LOGS/valgrind_tests.txt"
valgrind "${VALGRIND_OPTIONS[@]}" "$BUILD/debug/SmartPointersBenchmark" --max 1000 --quick --out "$SCRATCH" \
    2>&1 | tee "$LOGS/valgrind_benchmark.txt"

echo "=== [2/3] AddressSanitizer + UndefinedBehaviorSanitizer ==="
cmake -S "$ROOT" -B "$BUILD/sanitize" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSANITIZE=ON > /dev/null
cmake --build "$BUILD/sanitize" --target SmartPointersTests SmartPointersBenchmark LeakDemo

"$BUILD/sanitize/SmartPointersTests" 2>&1 | tee "$LOGS/sanitizers_tests.txt"
"$BUILD/sanitize/SmartPointersBenchmark" --max 10000 --quick --out "$SCRATCH" 2>&1 | tee "$LOGS/sanitizers_benchmark.txt"

echo "=== [3/3] control: LeakDemo must be reported ==="
# Здесь ошибка - ожидаемый результат, поэтому код возврата инвертирован
if valgrind "${VALGRIND_OPTIONS[@]}" "$BUILD/debug/LeakDemo" > "$LOGS/leak_demo_valgrind.txt" 2>&1; then
    echo "FAIL: valgrind did not detect the intentional leak"
    exit 1
fi
if "$BUILD/sanitize/LeakDemo" > "$LOGS/leak_demo_sanitizers.txt" 2>&1; then
    echo "FAIL: AddressSanitizer did not detect the intentional leak"
    exit 1
fi
echo "OK: intentional leak detected by valgrind and AddressSanitizer"

echo
echo "All checks passed. Logs: $LOGS"
