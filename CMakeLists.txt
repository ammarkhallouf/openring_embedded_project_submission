cmake_minimum_required(VERSION 3.14)

project(OpenRingSensorManager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ensure threading libraries are found for concurrency tests
find_package(Threads REQUIRED)

# ---------------------------------------------------------
# Core Library
# ---------------------------------------------------------
add_library(SensorLib 
    src/SensorManager.cpp
)

target_include_directories(SensorLib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(SensorLib
    PUBLIC
        Threads::Threads
)

# ---------------------------------------------------------
# Unit Testing configuration
# ---------------------------------------------------------
include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(run_tests
    tests/test_sensor_manager.cpp
)

target_link_libraries(run_tests
    PRIVATE
        SensorLib
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(run_tests)