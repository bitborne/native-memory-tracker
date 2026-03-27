//
// idle_page_log.h
// IdlePage 模块的日志宏
//

#ifndef DEMO_SO_IDLE_PAGE_LOG_H
#define DEMO_SO_IDLE_PAGE_LOG_H

#include <android/log.h>

#define IDLE_PAGE_LOG_TAG "SO2_IdlePage"
#define IDLE_LOGI(...) __android_log_print(ANDROID_LOG_INFO, IDLE_PAGE_LOG_TAG, __VA_ARGS__)
#define IDLE_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, IDLE_PAGE_LOG_TAG, __VA_ARGS__)
#define IDLE_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, IDLE_PAGE_LOG_TAG, __VA_ARGS__)

#endif // DEMO_SO_IDLE_PAGE_LOG_H
