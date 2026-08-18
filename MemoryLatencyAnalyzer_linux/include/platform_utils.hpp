#pragma once

// معادل لینوکسیِ SetThreadPriority(THREAD_PRIORITY_HIGHEST) +
// SetThreadAffinityMask روی ویندوز: ترد جاری را روی هسته‌ی منطقی
// مشخص‌شده پین می‌کند و تلاش می‌کند اولویت زمان‌بندی را بالا ببرد.
// اگر پروسه دسترسی کافی (root یا CAP_SYS_NICE) نداشته باشد، افزایش
// اولویت بی‌سروصدا شکست می‌خورد و فقط pin کردن هسته اعمال می‌شود
// (دقیقاً همان رفتار best-effort نسخه‌ی ویندوزی).
namespace PlatformUtils {
    void pin_current_thread(int logical_core_index);
}
