cmake_minimum_required(VERSION 3.31)
project(Dr_Web_Test)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Парсер отдельная библиотека, чтобы его подключали программа и тесты
add_library(archive_parser arc_parse/archive_parser.cpp)
target_include_directories(archive_parser PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(Dr_Web_Test main.cpp)
target_link_libraries(Dr_Web_Test PRIVATE archive_parser)

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
