#pragma once
#include <cstdint>
#include <string>

// نتیجه‌ی اندازه‌گیری latency حافظه‌ی مشترک بین دو پروسه (round-trip کامل:
// producer پیام می‌فرستد، consumer فوراً echo می‌کند، producer زمان رفت‌وبرگشت
// را با rdtsc محلی خودش حساب می‌کند؛ بنابراین به sync کردن TSC بین هسته‌ها
// نیازی نیست).
//
// نسخه‌ی لینوکس: به‌جای CreateFileMapping/CreateProcess ویندوز، از
// shm_open + mmap (POSIX shared memory) برای خودِ حافظه‌ی مشترک، و از
// fork() برای ساختن پروسه‌ی consumer استفاده می‌شود؛ fork() از exec دوباره‌ی
// خودِ باینری ساده‌تر و برای این منظور طبیعی‌تر است.
struct SharedMemoryResult {
    double      mean_cycles   = 0.0;
    double      stddev_cycles = 0.0;
    uint64_t    min_cycles    = 0;
    uint64_t    max_cycles    = 0;
    int         iterations    = 0;
    bool        success       = false;
    std::string error_message;
};

class SharedMemoryMeasurer {
public:
    // این پروسه نقش producer را بازی می‌کند: یک ناحیه‌ی حافظه‌ی مشترکِ
    // POSIX shm_open می‌سازد، با fork() یک پروسه‌ی فرزند consumer راه
    // می‌اندازد (که همان mapping را به ارث می‌برد)، و از طریق آن حافظه‌ی
    // مشترک با busy-wait spin ping-pong می‌کند.
    static SharedMemoryResult measure_cross_process(int iterations, int warmup);

    // نقش consumer، برای اجرای دستیِ جداگانه از طریق فلگ داخلی
    // --shm-consumer <mapping_name> (همان چیزی که main برای این حالت
    // فراخوانی می‌کند). shm را با نام باز می‌کند، نه با ارث‌بری از fork.
    static int run_consumer(const std::string& mapping_name);
};
