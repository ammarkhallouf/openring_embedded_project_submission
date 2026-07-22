cmake_minimum_required(VERSION 3.14)
project(SmartRingFirmware CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ensure threading libraries are found for concurrency tests
find_package(Threads REQUIRED)

# ---------------------------------------------------------
# Core Library
# ---------------------------------------------------------
add_library(SensorLib 
    src/SensorManager.cpp
)
target_include_directories(SensorLib PUBLIC src/)
# Link threads to the library since RingBuffer utilizes std::mutex
target_link_libraries(SensorLib PRIVATE Threads::Threads)

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

add_executable(run_tests tests/test_sensor_manager.cpp)
target_link_libraries(run_tests PRIVATE 
    SensorLib 
    gtest_main 
    gmock
    Threads::Threads
)