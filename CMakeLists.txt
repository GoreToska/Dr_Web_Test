cmake_minimum_required(VERSION 3.31)
project(Dr_Web_Test)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Парсер отдельная библиотека, чтобы его подключали программа и тесты
add_library(archive_parser arc_parse/archive_parser.cpp)
target_include_directories(archive_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Поиск ключевых слов. Собирается с выключенными исключениями
add_library(keyword_search keyword_search/keyword_searcher.cpp)
target_include_directories(keyword_search PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (MSVC)
    # Перекрывает глобальный /EHsc только для этой библиотеки (cl выдаст предупреждение D9025)
    target_compile_options(keyword_search PRIVATE /EHs-c-)
else ()
    target_compile_options(keyword_search PRIVATE -fno-exceptions)
endif ()

# Функция, переписанная с ассемблера на C
add_library(asm_to_c asm_to_c/is_non_zero.c)
target_include_directories(asm_to_c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(Dr_Web_Test main.cpp)
target_link_libraries(Dr_Web_Test PRIVATE archive_parser keyword_search)

# GoogleTest
include(FetchContent)
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
)

# Для MSVC
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(archive_parser_tests tests/archive_parser_test.cpp)
target_link_libraries(archive_parser_tests PRIVATE archive_parser GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(archive_parser_tests)

add_executable(keyword_searcher_tests tests/keyword_searcher_test.cpp)
target_link_libraries(keyword_searcher_tests PRIVATE keyword_search GTest::gtest_main)
gtest_discover_tests(keyword_searcher_tests)
