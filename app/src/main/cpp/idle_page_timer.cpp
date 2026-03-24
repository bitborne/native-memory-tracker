//
// idle_page_timer.cpp
// 高精度定时器实现
//

#include "idle_page_timer.h"
#include "idle_page_log.h"

#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <cstring>

namespace idle_page {

IdlePageTimer::IdlePageTimer() = default;

IdlePageTimer::~IdlePageTimer() {
    stop();
}

bool IdlePageTimer::init(int interval_ms, Callback callback) {
    interval_ms_ = interval_ms;
    callback_ = callback;

    // 创建 timerfd
    timerfd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timerfd_ < 0) {
        IDLE_LOGE("timerfd_create failed: %s", strerror(errno));
        return false;
    }

    // 创建 epoll 实例
    epollfd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epollfd_ < 0) {
        IDLE_LOGE("epoll_create1 failed: %s", strerror(errno));
        close(timerfd_);
        timerfd_ = -1;
        return false;
    }

    // 添加 timerfd 到 epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = timerfd_;
    if (epoll_ctl(epollfd_, EPOLL_CTL_ADD, timerfd_, &ev) < 0) {
        IDLE_LOGE("epoll_ctl failed: %s", strerror(errno));
        close(epollfd_);
        close(timerfd_);
        epollfd_ = -1;
        timerfd_ = -1;
        return false;
    }

    IDLE_LOGI("Timer initialized: %dms interval", interval_ms_);
    return true;
}

bool IdlePageTimer::apply_interval() {
    struct itimerspec its;
    memset(&its, 0, sizeof(its));

    // 首次触发
    its.it_value.tv_sec = interval_ms_ / 1000;
    its.it_value.tv_nsec = (interval_ms_ % 1000) * 1000000LL;

    // 周期触发
    its.it_interval.tv_sec = interval_ms_ / 1000;
    its.it_interval.tv_nsec = (interval_ms_ % 1000) * 1000000LL;

    if (timerfd_settime(timerfd_, 0, &its, nullptr) < 0) {
        IDLE_LOGE("timerfd_settime failed: %s", strerror(errno));
        return false;
    }

    return true;
}

void IdlePageTimer::start() {
    if (timerfd_ < 0 || running_) return;

    running_ = true;
    interval_changed_ = false;

    apply_interval();

    thread_ = std::thread(&IdlePageTimer::timer_thread, this);
    IDLE_LOGI("Timer started");
}

void IdlePageTimer::stop() {
    running_ = false;

    if (thread_.joinable()) {
        thread_.join();
    }

    // 停止定时器
    if (timerfd_ >= 0) {
        struct itimerspec its;
        memset(&its, 0, sizeof(its));
        timerfd_settime(timerfd_, 0, &its, nullptr);
    }

    IDLE_LOGI("Timer stopped");
}

void IdlePageTimer::set_rate(SampleRate rate) {
    int new_interval = 100;

    switch (rate) {
        case SampleRate::SLOW:
            new_interval = 1000;  // 1秒 - 检测冷数据
            break;
        case SampleRate::MEDIUM:
            new_interval = 100;   // 100ms - 默认
            break;
        case SampleRate::FAST:
            new_interval = 10;    // 10ms - 捕捉热数据
            break;
    }

    if (new_interval != interval_ms_) {
        interval_ms_ = new_interval;
        current_rate_ = rate;
        interval_changed_ = true;

        // 立即应用新间隔
        if (running_) {
            apply_interval();
            IDLE_LOGI("Rate changed to %dms", interval_ms_);
        }
    }
}

void IdlePageTimer::auto_adjust_rate(float access_ratio) {
    // 动态频率调整策略
    // > 10% 页被访问 -> 快速模式 (热数据)
    // 1% - 10% -> 中速模式
    // < 1% -> 慢速模式 (冷数据)

    if (access_ratio > 0.10f) {
        if (current_rate_ != SampleRate::FAST) {
            set_rate(SampleRate::FAST);
            IDLE_LOGI("Auto-switch to FAST mode (%.1f%% pages accessed)", access_ratio * 100);
        }
    } else if (access_ratio < 0.01f) {
        if (current_rate_ != SampleRate::SLOW) {
            set_rate(SampleRate::SLOW);
            IDLE_LOGI("Auto-switch to SLOW mode (%.1f%% pages accessed)", access_ratio * 100);
        }
    } else {
        if (current_rate_ != SampleRate::MEDIUM) {
            set_rate(SampleRate::MEDIUM);
            IDLE_LOGI("Auto-switch to MEDIUM mode (%.1f%% pages accessed)", access_ratio * 100);
        }
    }
}

void IdlePageTimer::timer_thread() {
    struct epoll_event events[1];
    uint64_t expirations;

    while (running_) {
        // 等待事件，100ms 超时以便响应 interval_changed_
        int nfds = epoll_wait(epollfd_, events, 1, 100);

        if (nfds > 0 && events[0].data.fd == timerfd_) {
            // 读取到期次数
            if (read(timerfd_, &expirations, sizeof(expirations)) > 0) {
                // 执行回调
                if (callback_) {
                    callback_();
                }
            }
        }

        // 检查是否需要调整间隔
        if (interval_changed_.exchange(false)) {
            apply_interval();
        }
    }
}

} // namespace idle_page
