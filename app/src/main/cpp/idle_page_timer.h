//
// idle_page_timer.h
// 高精度定时器 - timerfd + epoll，支持动态频率调整
//

#ifndef DEMO_SO_IDLE_PAGE_TIMER_H
#define DEMO_SO_IDLE_PAGE_TIMER_H

#include <functional>
#include <atomic>
#include <thread>

namespace idle_page {

// 采样频率档位
enum class SampleRate {
    SLOW = 0,    // 1000ms - 用于检测冷数据
    MEDIUM = 1,  // 100ms  - 默认/过渡
    FAST = 2     // 10ms   - 用于捕捉热数据
};

class IdlePageTimer {
public:
    using Callback = std::function<void()>;

    IdlePageTimer();
    ~IdlePageTimer();

    // 初始化定时器
    // interval_ms: 初始周期（毫秒）
    bool init(int interval_ms, Callback callback);

    // 启动/停止
    void start();
    void stop();

    // 动态调整频率
    void set_rate(SampleRate rate);
    SampleRate get_rate() const { return current_rate_; }

    // 获取当前周期（毫秒）
    int get_interval_ms() const { return interval_ms_; }

    // 根据访问活跃度自动调整频率
    // access_ratio: 被访问页的比例 (0.0 - 1.0)
    void auto_adjust_rate(float access_ratio);

private:
    void timer_thread();
    bool apply_interval();

    int timerfd_ = -1;
    int epollfd_ = -1;
    int interval_ms_ = 100;

    std::atomic<SampleRate> current_rate_{SampleRate::MEDIUM};
    std::atomic<bool> running_{false};
    std::atomic<bool> interval_changed_{false};

    Callback callback_;
    std::thread thread_;
};

} // namespace idle_page

#endif // DEMO_SO_IDLE_PAGE_TIMER_H