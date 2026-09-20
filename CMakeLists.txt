cmake_minimum_required(VERSION 3.14)
project(SmartPointersLab)

# Устанавливаем стандарт C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Автоматически скачиваем и подключаем Google Test
include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        release-1.12.1
)
FetchContent_MakeAvailable(googletest)

# Создаем исполняемый файл из наших файлов с тестами
add_executable(SmartPointersLab
        test_unq_ptr.cpp
        test_shrd_ptr.cpp
        test_performance.cpp
        main.cpp
)

# Линкуем библиотеку GTest с встроенным main
target_link_libraries(SmartPointersLab PRIVATE gtest_main)