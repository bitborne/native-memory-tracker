---
title: 'ByteHook 原理与使用笔记'
description: 'PLT Hook 技术原理介绍，基于 ByteHook 官方示例的代码剖析，以及在实际业务场景中的应用思路。'
pubDate: '2026-03-13'
---

# ByteHook 原理与使用笔记

## 一、PLT Hook 技术原理

### 1.1 什么是 PLT

PLT（Procedure Linkage Table，过程链接表）是 ELF 格式可执行文件中用于动态链接的跳转表。程序对外部函数的调用会先经过 PLT，再由 PLT 跳转到实际的函数地址。

```
调用方 -> PLT -> GOT -> 实际函数地址
```

GOT（Global Offset Table）中存储着实际地址。PLT Hook 的核心思想就是**修改 GOT 表中的地址**，使其指向我们的 Hook 函数。

### 1.2 ByteHook 的工作流程

1. **初始化**：遍历进程已加载的 ELF 文件，找到目标函数的 PLT 项
2. **Hook**：修改 GOT 表，将原始地址替换为 Hook 函数地址，同时保存原始地址
3. **调用**：程序调用被 Hook 函数时，先执行我们的代码，再通过保存的原始地址调用原函数
4. **Unhook**：恢复 GOT 表的原始地址

### 1.3 Automatic vs Manual 模式

| 模式 | 原理 | 适用场景 |
|------|------|---------|
| Automatic | 通过 TLS 存储原始地址，调用时自动获取 | 通用场景，多线程安全 |
| Manual | 开发者自行保存和调用原始函数 | 需要精细控制的场景 |

Automatic 模式的优点在于无需关心原始函数的保存和恢复，框架内部处理了这些细节。

## 二、官方示例代码剖析

ByteHook 官方示例位于 `bytehook_sample` 目录，提供了一个完整的 `strlen` Hook 实现。

### 2.1 Native 层代码结构

**hacker_bytehook.cpp** 核心实现：

```cpp
#include "bytehook.h"

// Hook 函数（Automatic 模式）
static size_t hacker_bytehook_strlen_automatic(const char* const s)
{
    BYTEHOOK_STACK_SCOPE();
    
    LOG("bytehook pre strlen");
    size_t ret = BYTEHOOK_CALL_PREV(hacker_bytehook_strlen_automatic, s);
    LOG("bytehook post strlen, ret=%zu", ret);
    
    return ret;
}

// Hook 函数（Manual 模式）
static size_t hacker_bytehook_strlen_manual(const char* const s)
{
    LOG("bytehook pre strlen");
    size_t ret = hacker_orig_strlen(s);  // 直接调用保存的原始函数
    LOG("bytehook post strlen, ret=%zu", ret);
    return ret;
}
```

**关键点解析**：

- `BYTEHOOK_STACK_SCOPE()`：标记当前栈帧，防止递归 Hook 导致的栈溢出
- `BYTEHOOK_CALL_PREV`：宏展开后通过 TLS 获取原始地址并调用
- 函数名作为参数：编译期类型检查，确保参数传递正确

**Hook 注册**：

```cpp
int hacker_bytehook_hook(void)
{
    // 根据当前模式选择实现
    void* hook_func = (BYTEHOOK_MODE_MANUAL == bytehook_get_mode()) 
        ? (void*)hacker_bytehook_strlen_manual 
        : (void*)hacker_bytehook_strlen_automatic;
    
    // 单库 Hook
    hacker_stub_strlen = bytehook_hook_single(
        "libsample.so",           // 目标库名
        NULL,                     // 调用者过滤（NULL=不限制）
        "strlen",                 // 目标函数名
        hook_func,                // Hook 实现
        hacker_bytehook_strlen_hooked,  // 回调函数
        NULL                      // 用户数据
    );
    
    return 0;
}
```

### 2.2 Java 层封装

**NativeHacker.java** 提供 JNI 接口：

```java
public class NativeHacker {
    public static void bytehookHook() {
        nativeBytehookHook();
    }
    
    public static void bytehookUnhook() {
        nativeBytehookUnhook();
    }
    
    private static native int nativeBytehookHook();
    private static native int nativeBytehookUnhook();
}
```

**MainActivity.java** 交互逻辑：

```java
public class MainActivity extends AppCompatActivity {
    boolean isHooked = false;
    
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Hook 按钮
        findViewById(R.id.unitTestHook).setOnClickListener(v -> {
            if (!isHooked) {
                NativeHacker.bytehookHook();
                isHooked = true;
            }
        });
        
        // 触发 strlen 调用
        findViewById(R.id.unitTestRun).setOnClickListener(v -> {
            Log.i(TAG, "onClick pre strlen()");
            NativeHacker.doRun();  // 内部调用 strlen
            Log.i(TAG, "onClick post strlen()");
        });
    }
}
```

### 2.3 三种 Hook API 对比

官方示例展示了三种 Hook 方式，各有适用场景：

```cpp
// 方式1：单库 Hook（推荐）
// 只 Hook 特定 SO 中的调用，影响范围可控
bytehook_hook_single(
    "libsample.so",   // 目标库
    NULL,             // 调用者不过滤
    "strlen",         // 目标函数
    new_func,         // 新实现
    callback,         // 回调
    arg               // 用户数据
);

// 方式2：条件 Hook
// 通过过滤器决定哪些调用者的调用需要被 Hook
bytehook_hook_partial(
    filter_func,      // 过滤器：return true 才 Hook
    arg,
    "libc.so",        // 被 Hook 的库
    "strlen",
    new_func,
    callback,
    arg
);

// 方式3：全局 Hook（慎用）
// Hook 进程中所有对 strlen 的调用
// Android 11+ 可能导致死锁（Hook 到系统关键路径）
// bytehook_hook_all(NULL, "strlen", new_func, callback, arg);
```

实际项目中建议优先使用 `bytehook_hook_single`，影响范围最小，风险可控。

## 三、扩展到其他函数

基于官方示例的模式，可以扩展到其他函数的 Hook。

### 3.1 函数签名变化

`strlen` 是只读函数，相对简单。对于 `malloc` 这类有副作用的函数：

```cpp
void* my_malloc(size_t size)
{
    BYTEHOOK_STACK_SCOPE();
    
    // 先调用原始函数
    void* result = BYTEHOOK_CALL_PREV(my_malloc, size);
    
    // 记录信息
    LOG("malloc(%zu) = %p", size, result);
    
    return result;
}
```

关键点：
- 先调用原函数获取结果
- 再记录日志
- 最后返回结果给调用者

### 3.2 获取调用栈

在 Hook 中常需要知道"是谁调用了这个函数"，这需要栈回溯：

```cpp
#include <unwind.h>

struct BacktraceState {
    void** current;
    void** end;
};

static _Unwind_Reason_Code unwind_callback(
    struct _Unwind_Context* context, void* arg) 
{
    BacktraceState* state = static_cast<BacktraceState*>(arg);
    uintptr_t pc = _Unwind_GetIP(context);
    if (pc && state->current < state->end) {
        *state->current++ = reinterpret_cast<void*>(pc);
    }
    return _URC_NO_REASON;
}

// 使用示例
void* backtrace[5] = {0};
BacktraceState state = {backtrace, backtrace + 5};
_Unwind_Backtrace(unwind_callback, &state);
// backtrace[0-4] 现在包含 5 层调用地址
```

### 3.3 批量 Hook 的实现

对于一类函数（如内存分配家族），可以封装统一的注册逻辑：

```cpp
void init_hooks() {
    const char* target = "libtarget.so";
    
    bytehook_hook_single(target, NULL, "malloc", (void*)my_malloc, NULL, NULL);
    bytehook_hook_single(target, NULL, "calloc", (void*)my_calloc, NULL, NULL);
    bytehook_hook_single(target, NULL, "realloc", (void*)my_realloc, NULL, NULL);
    bytehook_hook_single(target, NULL, "free", (void*)my_free, NULL, NULL);
}
```

## 四、实际应用思路

结合业务场景，ByteHook 可以用于：

### 4.1 内存监控

Hook `malloc/free/mmap/munmap`，记录每一次内存分配：
- 分配大小和地址
- 调用栈（定位分配热点）
- 时间戳（分析分配时序）

后续可以分析：
- 哪些代码路径分配最多
- 是否存在内存泄漏（分配但未释放）
- 内存碎片情况

### 4.2 性能追踪

Hook 耗时函数，记录执行时间：
- 文件 IO 操作
- 网络请求
- 数据库查询

### 4.3 调用链路分析

通过栈回溯构建调用图谱：
- 函数调用频率
- 热点代码路径
- 递归深度检测

## 五、注意事项

1. **避免递归**：Hook 函数内不要调用被 Hook 的函数（如 `printf` 内部可能调用 `malloc`）
2. **线程安全**：多线程环境下注意同步问题
3. **性能开销**：高频函数（如 `malloc`）的 Hook 需要考虑优化
4. **稳定性**：Android 版本差异可能导致行为不一致，需充分测试

## 参考

- [ByteHook GitHub](https://github.com/bytedance/bhook)
- [ELF 格式规范](https://refspecs.linuxfoundation.org/elf/elf.pdf)
- [libunwind 文档](https://www.nongnu.org/libunwind/docs.html)
