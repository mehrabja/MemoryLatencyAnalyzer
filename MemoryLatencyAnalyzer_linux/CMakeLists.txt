cmake_minimum_required(VERSION 3.15)

project(MemoryMicroarchitectureAnalyzer
    VERSION 1.0.0
    LANGUAGES CXX
)

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES
   "x86_64|AMD64|amd64|i.86")
    message(FATAL_ERROR
        "MemoryMicroarchitectureAnalyzer requires an x86/x86-64 CPU.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(latency_analyzer
    src/main.cpp
    src/timer.cpp
    src/statistics.cpp
    src/cpu_info.cpp
    src/latency_measurer.cpp
    src/bandwidth_measurer.cpp
    src/shared_memory_measurer.cpp
    src/reporter.cpp
    src/platform_utils.cpp
    src/spectre_v1.cpp
    src/defensive_lab.cpp
    src/cpu_capability.cpp
    src/evaluation_lab.cpp
    src/operational_lab.cpp
)

target_include_directories(latency_analyzer PRIVATE
    include
    config
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(latency_analyzer PRIVATE
        -O2
        -march=native
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wformat=2
        -Wundef
    )
endif()

find_package(Threads REQUIRED)
target_link_libraries(
    latency_analyzer PRIVATE
    Threads::Threads
    rt
)

include(CTest)

if(BUILD_TESTING)
    add_executable(
        statistics_test
        tests/statistics_test.cpp
        src/statistics.cpp
    )

    target_include_directories(
        statistics_test PRIVATE include
    )

    add_test(
        NAME statistics_test
        COMMAND statistics_test
    )

    add_executable(
        evaluation_test
        tests/evaluation_test.cpp
        src/evaluation_lab.cpp
        src/cpu_capability.cpp
        src/cpu_info.cpp
        src/timer.cpp
    )

    target_include_directories(
        evaluation_test PRIVATE
        include
        config
    )

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(evaluation_test PRIVATE
            -O2
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()

    target_link_libraries(
        evaluation_test PRIVATE
        Threads::Threads
        rt
    )

    add_test(
        NAME evaluation_test
        COMMAND evaluation_test
    )
endif()
