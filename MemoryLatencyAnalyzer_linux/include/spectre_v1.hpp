#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

struct SpectreResult {
    uint8_t guessed_value;   // بایتی که حدس زده شده
    int score;               // امتیاز بایت برتر
    int second_score;        // امتیاز بایت دوم (برای تشخیص موفقیت)
    bool success;            // آیا با معیار مقاله موفق بوده؟
};

class SpectreV1 {
public:
    // ورودی: آدرس هدف (نسبت به ابتدای array1)، تعداد تکرار (پیش‌فرض 999 مثل مقاله)
    static SpectreResult read_byte(size_t malicious_x, int tries = 999);

    // تابع کمکی برای خواندن یک رشته کامل (مثلاً secret)
    static std::string read_string(size_t start_addr, size_t length, int tries = 999);

    // اجرای نمونهٔ کامل Spectre Variant 1 برای نمایش میزان لو رفتن رشتهٔ secret
    static void run_demo(int tries = 999);
};