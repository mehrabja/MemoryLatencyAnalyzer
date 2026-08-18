cmake_minimum_required(VERSION 3.15)
project(MemoryLatencyAnalyzer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(latency_analyzer
    src/main.cpp
    src/statistics.cpp
    src/cpu_info.cpp
    src/latency_measurer.cpp
    src/bandwidth_measurer.cpp
    src/shared_memory_measurer.cpp
    src/reporter.cpp
    src/platform_utils.cpp
    src/spectre_v1.cpp          # <---- این خط را اضافه کنید
)

target_include_directories(latency_analyzer PRIVATE include config)

if(MSVC)
    target_compile_options(latency_analyzer PRIVATE /Od)
else()
    target_compile_options(latency_analyzer PRIVATE -O0 -march=native)
endif()

# لینوکس: shm_open/pthread روی بعضی توزیع‌ها/glibcهای قدیمی‌تر به لینک
# صریح librt و libpthread نیاز دارند؛ روی glibc جدید بی‌ضرر است.
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(latency_analyzer PRIVATE Threads::Threads rt)
endif()