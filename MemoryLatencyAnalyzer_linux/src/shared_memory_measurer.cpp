#include "shared_memory_measurer.hpp"
#include "timer.hpp"
#include "statistics.hpp"
#include "platform_utils.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <x86intrin.h>

#include <vector>
#include <sstream>
#include <ctime>
#include <new>
#include <cstdint>

namespace {

// هر پرچمِ همگام‌سازی روی cache line جداگانه قرار می‌گیرد تا false sharing
// بین request و response خودِ نتیجه‌ی latency را منحرف نکند.
struct alignas(64) PingPongSlot {
    volatile int32_t  ready;
    volatile uint64_t timestamp;
    char pad[64 - sizeof(int32_t) - sizeof(uint64_t)];
};

struct SharedControl {
    PingPongSlot     request;        // producer -> consumer
    PingPongSlot     response;       // consumer -> producer
    volatile int32_t consumer_ready; // consumer پس از map کردن حافظه این را ست می‌کند
    volatile int32_t stop;           // producer برای پایان کار ست می‌کند
    char pad[64 - 2 * sizeof(int32_t)];
};

constexpr int SPIN_ITERATIONS   = 2'000'000; // چرخه‌ی busy-spin قبل از رفتن به حالت خواب کوتاه
constexpr int SLEEP_FALLBACK_MS = 5000;      // حداکثر زمان انتظار با خواب 1ms قبل از timeout نهایی

// منتظر می‌ماند تا *flag برابر expected شود. اگر spin_limit چرخه طول بکشد،
// به‌جای مصرف کامل CPU چند ثانیه با خواب 1ms هم صبر می‌کند (معادل
// Sleep(1) ویندوز، اینجا با usleep). اگر stop_flag داده شود، با فعال
// شدنش هم زودتر خارج می‌شود (برای این‌که consumer در صورت درخواست توقف
// producer معطل نماند).
bool spin_wait(volatile int32_t* flag, int32_t expected, const volatile int32_t* stop_flag = nullptr) {
    for (int i = 0; i < SPIN_ITERATIONS; ++i) {
        if (*flag == expected) return true;
        if (stop_flag && *stop_flag) return false;
        _mm_pause();
    }
    for (int i = 0; i < SLEEP_FALLBACK_MS; ++i) {
        if (*flag == expected) return true;
        if (stop_flag && *stop_flag) return false;
        usleep(1000);
    }
    return false;
}

std::string make_unique_mapping_name() {
    std::ostringstream oss;
    // نام POSIX shm باید با یک '/' شروع شود و اسلش دیگری نداشته باشد؛
    // معادل عملکردیِ پیشوند "Local\" ویندوز (فقط به‌جای session، بین
    // پروسه‌های همین pid+timestamp یکتا می‌شود).
    oss << "/mla_shm_" << getpid() << "_" << static_cast<unsigned long>(time(nullptr));
    return oss.str();
}

// منطق مشترک consumer: چه از طریق fork() به ارث رسیده باشد (مسیر پیش‌فرض)
// چه از طریق --shm-consumer با نام باز شده باشد (اجرای دستی)، این تابع
// روی درخواست‌های producer اسپین می‌زند و echo می‌کند.
void consumer_loop(SharedControl* ctrl) {
    // producer از قبل روی هسته‌ی منطقی 0 پین شده؛ consumer را روی هسته‌ی
    // دیگری پین می‌کنیم تا آزمایش واقعاً ترافیک coherency بین هسته‌ها را
    // بسنجد، نه فقط latency داخل یک هسته. اگر سیستم تک‌هسته باشد این
    // فراخوانی صرفاً بی‌اثر شکست می‌خورد.
    PlatformUtils::pin_current_thread(1);

    ctrl->consumer_ready = 1;
    while (true) {
        if (!spin_wait(&ctrl->request.ready, 1, &ctrl->stop)) break; // یا stop شد یا timeout خورد
        ctrl->request.ready  = 0;
        ctrl->response.ready = 1;
    }
}

// چند صد میلی‌ثانیه با WNOHANG صبر می‌کند تا فرزند خودش تمام شود؛ اگر در
// این بازه‌ی زمانی تمام نشد، به‌زور می‌کشدش. معادل WaitForSingleObject +
// TerminateProcess ویندوز.
void wait_or_kill(pid_t pid, int timeout_ms) {
    const int step_ms = 40;
    int waited = 0;
    while (waited < timeout_ms) {
        if (waitpid(pid, nullptr, WNOHANG) == pid) return;
        usleep(step_ms * 1000);
        waited += step_ms;
    }
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
}

} // namespace

SharedMemoryResult SharedMemoryMeasurer::measure_cross_process(int iterations, int warmup) {
    SharedMemoryResult result;
    result.iterations = iterations;

    std::string mapping_name = make_unique_mapping_name();

    // shm_open با O_CREAT|O_EXCL یک ناحیه‌ی حافظه‌ی مشترکِ POSIX می‌سازد که
    // در tmpfs نگه‌داری می‌شود، نه در یک فایل واقعی روی دیسک؛ این دقیقاً
    // معادل CreateFileMapping(INVALID_HANDLE_VALUE, ...) ویندوز است (که هم
    // pagefile-backed بود، نه file-backed). همین یک شیء، وقتی در دو پروسه
    // map می‌شود، به یک صفحه‌ی فیزیکی واحد اشاره می‌کند و خودِ این اشتراکِ
    // صفحه همان مکانیزمی است که memory deduplication هم بر پایه‌ی آن کار
    // می‌کند (به‌جای دو کپی، یک صفحه‌ی فیزیکی مشترک بین دو آدرس مجازی).
    int fd = shm_open(mapping_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        result.error_message = "shm_open failed";
        return result;
    }

    if (ftruncate(fd, sizeof(SharedControl)) != 0) {
        close(fd);
        shm_unlink(mapping_name.c_str());
        result.error_message = "ftruncate failed";
        return result;
    }

    void* view = mmap(nullptr, sizeof(SharedControl), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd); // بعد از mmap شدن دیگر به فایل‌دیسکریپتور نیازی نیست
    if (view == MAP_FAILED) {
        shm_unlink(mapping_name.c_str());
        result.error_message = "mmap failed";
        return result;
    }

    SharedControl* ctrl = new (view) SharedControl();
    ctrl->request.ready  = 0;
    ctrl->response.ready = 0;
    ctrl->consumer_ready = 0;
    ctrl->stop            = 0;

    // fork(): معادل CreateProcess برای راه‌اندازیِ پروسه‌ی consumer. چون
    // mapping از قبل MAP_SHARED است، فرزند بلافاصله بعد از fork همان
    // صفحات فیزیکی را می‌بیند؛ نیازی به اجرای مجدد باینری با یک فلگ مخفی
    // (آن‌طور که نسخه‌ی ویندوزی با CreateProcess انجام می‌داد) نیست.
    pid_t pid = fork();
    if (pid < 0) {
        munmap(view, sizeof(SharedControl));
        shm_unlink(mapping_name.c_str());
        result.error_message = "fork failed";
        return result;
    }

    if (pid == 0) {
        // پروسه‌ی فرزند: نقش consumer را بازی می‌کند و هرگز از این تابع
        // برنمی‌گردد به کد main.
        consumer_loop(ctrl);
        _exit(0);
    }

    // از اینجا به بعد فقط پروسه‌ی والد (producer) ادامه می‌دهد.
    bool consumer_up = spin_wait(&ctrl->consumer_ready, 1);
    if (!consumer_up) {
        ctrl->stop = 1;
        wait_or_kill(pid, 2000);
        munmap(view, sizeof(SharedControl));
        shm_unlink(mapping_name.c_str());
        result.error_message = "Consumer process did not become ready in time";
        return result;
    }

    // Warmup: چند ping-pong بدون ثبت زمان تا صفحات حافظه و مسیر کد گرم شوند
    bool warmup_ok = true;
    for (int i = 0; i < warmup && warmup_ok; ++i) {
        ctrl->request.timestamp = Timer::rdtsc();
        ctrl->request.ready = 1;
        warmup_ok = spin_wait(&ctrl->response.ready, 1);
        ctrl->response.ready = 0;
    }

    std::vector<uint64_t> times;
    if (warmup_ok) {
        times.reserve(iterations);
        for (int i = 0; i < iterations; ++i) {
            Timer::serialize();
            uint64_t t0 = Timer::rdtsc();
            ctrl->request.timestamp = t0;
            ctrl->request.ready = 1;

            if (!spin_wait(&ctrl->response.ready, 1)) {
                result.error_message = "Consumer stopped responding mid-run";
                break;
            }
            Timer::serialize();
            uint64_t t1 = Timer::rdtsc();
            ctrl->response.ready = 0;

            times.push_back(t1 - t0);
        }
    } else {
        result.error_message = "Consumer stopped responding during warmup";
    }

    ctrl->stop = 1;
    wait_or_kill(pid, 3000);

    munmap(view, sizeof(SharedControl));
    shm_unlink(mapping_name.c_str());

    if (times.empty()) {
        if (result.error_message.empty()) result.error_message = "No successful round-trips";
        return result;
    }

    double m = Statistics::mean(times);
    result.mean_cycles   = m;
    result.stddev_cycles = Statistics::stddev(times, m);
    result.min_cycles    = Statistics::min_value(times);
    result.max_cycles    = Statistics::max_value(times);
    result.iterations    = static_cast<int>(times.size());
    result.success        = true;
    return result;
}

int SharedMemoryMeasurer::run_consumer(const std::string& mapping_name) {
    // این مسیر فقط وقتی استفاده می‌شود که کاربر باینری را دستی با
    // --shm-consumer <name> اجرا کند؛ در مسیر معمولیِ measure_cross_process
    // دیگر لازم نیست، چون fork() خودش mapping را به ارث می‌برد.
    int fd = shm_open(mapping_name.c_str(), O_RDWR, 0600);
    if (fd < 0) return 1;

    void* view = mmap(nullptr, sizeof(SharedControl), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (view == MAP_FAILED) return 1;

    SharedControl* ctrl = static_cast<SharedControl*>(view);
    consumer_loop(ctrl);

    munmap(view, sizeof(SharedControl));
    return 0;
}
