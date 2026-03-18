cmake_minimum_required(VERSION 3.14)
project(orderbook_tests)

include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/heads/main.zip
)

# Prevents overriding the parent project's compiler/linker settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# The test executable re-compiles Orderbook.cpp so the test binary is self-contained
add_executable(
    Google_Tests_run
    test.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../Source Files/Orderbook.cpp"
)

# Expose the header directory (contains spaces — must be quoted)
target_include_directories(
    Google_Tests_run PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Header Files"
)

target_compile_features(Google_Tests_run PRIVATE cxx_std_20)

target_link_libraries(
    Google_Tests_run
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(Google_Tests_run)
