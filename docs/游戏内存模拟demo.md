---
title: 'DAY2：游戏内存模拟器'
description: '实现一个模拟游戏内存分配特征的测试工具，包含四种内存热度级别的模拟，为内存监控提供测试目标。'
pubDate: '2026-03-13'
---

# DAY2：游戏内存模拟器

native-lib.cpp 实现了游戏内存分配模拟器，用于生成已知的内存访问模式，验证 ByteHook 监控工具的有效性。

## 设计思路

真实游戏的内存访问有明确规律：渲染缓冲区每帧访问、对象池高频创建销毁、资源包大块低频加载。这些特征难以在受控环境复现，所以需要一个可编程的模拟器。

模拟器采用双 SO 架构：
- SO1 (native-lib)：纯粹的业务逻辑，模拟游戏内存行为
- SO2 (so2-hook)：监控层，通过 ByteHook 拦截 SO1 的内存操作

这种分离的好处是 SO1 完全不感知监控存在，模拟结果更真实。

## 核心实现

模拟器实现了四种内存热度级别，对应游戏中典型的内存使用场景：

**L1 Hot（渲染缓冲区）**
模拟 GPU 渲染数据的特征：每 16ms 访问一次（60 FPS），持续扩容但永不释放。初始分配 10 个 64KB 缓冲区，每 100 帧增加 32KB。

**L2 Warm（对象池）**
模拟游戏对象的创建销毁：随机分配 256B-8KB，存活 1-10ms 后释放。这种高频小额分配是内存碎片的主要来源。

**L3 Cool（配置缓存）**
模拟关卡配置数据的加载：分配 16-256KB，保持 5 秒后清理一半。体现"定期清理"的缓存策略。

**L4 Cold（资源包）**
模拟贴图/模型资源：使用 mmap 分配 4MB，延迟初始化（只写第一页），保持 30 秒后卸载。

## 技术细节

四个热度级别分别运行在独立线程中，通过 `atomic<bool> running` 统一控制启停。每个线程持有独立的互斥锁，减少线程竞争。

L1 线程模拟 60 FPS 帧率，每次循环睡眠 16ms。L4 线程使用 `mmap/munmap` 而非 `malloc/free`，更接近真实资源加载行为。

停止时采用 `detach` 而非 `join`，避免 L4 线程的 30 秒睡眠阻塞 UI 主线程。

## 与 SO2 的配合

SO2 通过 ByteHook 拦截 SO1 中的 `malloc/free/calloc/realloc/mmap/munmap`，记录每次分配的：时间戳、大小、调用栈、线程 ID。

CSV 日志格式示例：
```
timestamp,type,ptr,req_size,act_size,tid,bt0,bt1,bt2,bt3,bt4
```

SO1 生成内存行为，SO2 捕获并记录，两者通过 JNI 接口与 Java 层交互。MainActivity 控制启停，并指定日志文件路径。

## 技术笔记大纲

```
1. 背景
   - 为什么需要内存模拟器
   - 双 SO 架构的优势

2. 内存热度模型
   - L1 Hot：渲染缓冲区特征
   - L2 Warm：对象池特征
   - L3 Cool：配置缓存特征
   - L4 Cold：资源包特征

3. 实现要点
   - 四线程并发模型
   - 线程安全设计
   - 停止机制设计

4. 与监控层的配合
   - Hook 的目标函数
   - 日志格式设计
   - 数据流完整链路

5. 验证方法
   - 功能验证
   - 数据完整性检查
```

## 关键代码片段

**内存热度级别定义**

```cpp
enum class MemHotness {
    L1_HOT = 1,   // 极热：每帧访问
    L2_WARM = 2,  // 温热：高频分配释放
    L3_COOL = 3,  // 凉爽：定期清理
    L4_COLD = 4   // 冰冷：大块mmap
};
```

**L1 Hot 线程（渲染缓冲区模拟）**

```cpp
void hotThread() {
    // 初始分配10个64KB缓冲区
    for (int i = 0; i < 10; i++) {
        void* p = malloc(64 * 1024);
        hotMemory.emplace_back(p, 64*1024, MemHotness::L1_HOT);
    }
    
    int frame = 0;
    while (running) {
        // 每帧"访问"数据
        for (auto& block : hotMemory) {
            memset(block.ptr, frame % 256, 1024);
        }
        
        // 每100帧扩容32KB
        if (++frame % 100 == 0) {
            void* p = malloc(32 * 1024);
            hotMemory.emplace_back(p, 32*1024, MemHotness::L1_HOT);
        }
        
        this_thread::sleep_for(milliseconds(16)); // 60 FPS
    }
}
```

**L4 Cold 线程（资源包模拟）**

```cpp
void coldThread() {
    while (running) {
        // 映射3个4MB资源包
        for (int i = 0; i < 3; i++) {
            void* p = mmap(nullptr, 4*1024*1024, 
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            // 延迟初始化：只写第一页
            memset(p, 0xDD, 4096);
            coldMappings.emplace_back(p, 4*1024*1024);
        }
        
        this_thread::sleep_for(seconds(30));
        
        // 清理所有映射
        for (auto& [ptr, size] : coldMappings) {
            munmap(ptr, size);
        }
        coldMappings.clear();
    }
}
```

**启停控制**

```cpp
void stop() {
    running = false;
    
    // 分离线程，不阻塞UI主线程
    for (auto& t : workers) {
        if (t.joinable()) t.detach();
    }
    
    this_thread::sleep_for(milliseconds(200));
    cleanup();
}
```
