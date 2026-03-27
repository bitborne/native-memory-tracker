//
// idle_page_log.h
// IdlePage 模块的日志宏
//

#ifndef DEMO_SO_IDLE_PAGE_LOG_H
#define DEMO_SO_IDLE_PAGE_LOG_H

#include <android/log.h>

#define IDLE_PAGE_LOG_TAG "SO2_IdlePage"

// 日志级别: 0=禁用, 1=ERROR, 2=INFO, 3=DEBUG
#ifndef IDLE_PAGE_LOG_LEVEL
#define IDLE_PAGE_LOG_LEVEL 2  // 默认 INFO 级别
#endif

#define IDLE_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, IDLE_PAGE_LOG_TAG, __VA_ARGS__)

#if IDLE_PAGE_LOG_LEVEL >= 2
#define IDLE_LOGI(...) __android_log_print(ANDROID_LOG_INFO, IDLE_PAGE_LOG_TAG, __VA_ARGS__)
#else
#define IDLE_LOGI(...) ((void)0)
#endif

#if IDLE_PAGE_LOG_LEVEL >= 3
#define IDLE_LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, IDLE_PAGE_LOG_TAG, __VA_ARGS__)
#else
#define IDLE_LOGD(...) ((void)0)
#endif

#endif // DEMO_SO_IDLE_PAGE_LOG_H
