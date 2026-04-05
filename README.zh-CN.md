# Android 原生内存追踪与分析 - 底层学习项目

[![Android](https://img.shields.io/badge/Android-API_24+-green.svg)](https://developer.android.com)
[![NDK](https://img.shields.io/badge/NDK-r29-blue.svg)](https://developer.android.com/ndk)
[![ByteHook](https://img.shields.io/badge/ByteHook-1.1.1-orange.svg)](https://github.com/bytedance/bhook)

[English](./README.md) | 中文

这是一个用于学习 Android 底层开发的综合性内存分析项目，展示了使用 **ByteDance 的 ByteHook PLT Hook 库** 进行内存分配追踪，结合 **Linux 内核页面空闲监控** 实现完整的内存访问模式分析。

- [Android 原生内存追踪与分析 - 底层学习项目](#android-原生内存追踪与分析---底层学习项目)
  - [概览](#概览)
  - [项目目标](#项目目标)
  - [架构](#架构)
  - [功能特性](#功能特性)
    - [1. 内存 Hook 库 (libso2.so)](#1-内存-hook-库-libso2so)
    - [2. 游戏内存模拟器 (libdemo\_so.so)](#2-游戏内存模拟器-libdemo_soso)
    - [3. 无锁日志系统](#3-无锁日志系统)
    - [4. 空闲页面监控器](#4-空闲页面监控器)
    - [5. ELF 解析器库](#5-elf-解析器库)
  - [快速开始](#快速开始)
    - [前置要求](#前置要求)
    - [构建](#构建)
    - [设备环境准备（设备启动后执行一次）](#设备环境准备设备启动后执行一次)
    - [安装 APK](#安装-apk)
    - [基本使用流程](#基本使用流程)
    - [拉取日志](#拉取日志)
    - [可视化分析](#可视化分析)
  - [使用方法](#使用方法)
    - [ELF 解析器 CLI](#elf-解析器-cli)
      - [构建与部署](#构建与部署)
      - [命令行参数](#命令行参数)
      - [使用示例](#使用示例)
  - [配置](#配置)
    - [空闲页面监控模式](#空闲页面监控模式)
    - [采样率](#采样率)
    - [日志缓冲区大小](#日志缓冲区大小)
  - [技术细节](#技术细节)
    - [ByteHook 集成](#bytehook-集成)
    - [无锁环形缓冲区](#无锁环形缓冲区)
    - [内核接口](#内核接口)
  - [故障排除](#故障排除)
    - [构建问题](#构建问题)
    - [运行时问题](#运行时问题)
  - [依赖](#依赖)
  - [致谢](#致谢)


---



## 概览

本项目演示了用于内存分析的高级 Android 原生开发技术：

- **内存分配 Hook**：使用 PLT Hook 拦截 `malloc`、`free`、`mmap` 等内存操作
- **无锁日志**：高性能多级缓冲系统，开销极小
- **页面访问追踪**：通过 `/sys/kernel/mm/page_idle/bitmap` 监控物理内存访问模式
- **游戏内存模拟**：模拟游戏引擎行为的逼真内存分配模式

## 项目目标

| 目标 | 方案 | 产出 |
|------|------|------|
| **溯源内存申请/释放堆栈** | App Hook 内存相关函数 + 堆栈 | `mem_reg.log` |
| **内存使用状态** | 手机 Root + 编译 Kernel 打开 IdlePage | `mem_visit.log` |
| **ELF 文件解析** | 模块化 ELF Reader 兼容 readelf | 命令行工具 `elf_reader` |

![监控日志展示](https://disk.0voice.com/u/EQ)

## 架构

![内存访问监控架构图](https://disk.0voice.com/u/EP)

![内存访问监控数据流向图](https://disk.0voice.com/u/ER)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          Android 应用程序                               │
├─────────────────────────────────────────────────────────────────────────┤
│  ┌──────────────┐      ┌──────────────┐      ┌──────────────────────┐   │
│  │   游戏模拟器  │      │   Hook 库    │      │     空闲页面监控     │   │
│  │ (libdemo_so) │◄────►│  (libso2)    │◄────►│    (内核接口)        │   │
│  └──────────────┘      └──────┬───────┘      └──────────────────────┘   │
│                               │                                         │
│                      ┌────────┴────────┐                                │
│                      ▼                 ▼                                │
│           ┌─────────────────┐  ┌───────────────┐                        │
│           │ ByteHook (bhook)│  │   无锁日志     │                       │
│           │   PLT Hook      │  │    缓冲区      │                       │
│           └─────────────────┘  └───────┬───────┘                        │
│                                        │                                │
│                                        ▼                                │
│                              ┌──────────────────┐                       │
│                              │   mem_reg.log    │                       │
│                              │  mem_visit.log   │                       │
│                              └──────────────────┘                       │
└─────────────────────────────────────────────────────────────────────────┘
```

## 功能特性

### 1. 内存 Hook 库 (libso2.so)

Hook 9 个内存分配函数，并捕获完整调用栈：

| 函数 | 描述 |
|------|------|
| `malloc` / `free` | 标准堆分配 |
| `calloc` | 零初始化分配 |
| `realloc` | 调整现有分配大小 |
| `mmap` / `munmap` / `mmap64` | 内存映射文件 |
| `posix_memalign` | 对齐分配 (POSIX) |
| `aligned_alloc` | 对齐分配 (C11) |

**日志格式** (CSV)：
```
时间戳_微秒,类型,地址,请求大小,实际大小,线程ID,0,0,调用栈地址...
```

### 2. 游戏内存模拟器 (libdemo_so.so)

在独立线程中模拟四种内存"热度"级别：

| 级别 | 名称 | 模式 | 大小 | 生命周期 |
|------|------|------|------|----------|
| L1 | **热区 (HOT)** | 渲染缓冲区 | 64KB (16 页) | 永不释放，60FPS 更新 |
| L2 | **温区 (WARM)** | 对象池 | 4KB-8KB | 1-10 毫秒生命周期 |
| L3 | **凉区 (COOL)** | 配置缓存 | 16KB-64KB | 每 5 秒清除 |
| L4 | **冷区 (COLD)** | 资源包 | 4MB mmap | 30 秒生命周期 |

### 3. 无锁日志系统

三级缓冲架构，实现零竞争日志：

```
线程本地缓冲区 (512B)
           │
           ▼
无锁环形缓冲区 (16MB)  ◄── 仅使用原子操作
           │
           ▼
后台写入线程
           │
           ▼
      磁盘 (fdatasync)
```

**关键优化**：
- `thread_local` 缓冲区消除线程竞争
- 使用 C++11 原子操作的无锁环形缓冲区
- 批处理：TLS 刷新 16 条记录，磁盘写入 64 条
- 每 16KB 执行 `fdatasync()` 确保持久性

### 4. 空闲页面监控器

使用 Linux 内核接口追踪物理内存页面访问模式：

**要求**：
- 需要 Root 权限访问 `/sys/kernel/mm/page_idle/bitmap`
- 内核需启用 `CONFIG_IDLE_PAGE_TRACKING`
- 需要 PFN Helper 辅助程序才能正确获取页面冷热信息

**双模式监控**：

| 模式 | 目标 | 来源 |
|------|------|------|
| SO 代码段 | SO 代码/数据段 | `/proc/self/maps` |
| 堆分配 | 动态分配 | malloc/mmap hooks |

**采样工作流程**：

1. `SAMPLE_START`：通过 `page_idle_set()` 清除 PTE Accessed 位
2. 等待采样间隔（自适应：10ms-1s）
3. `SAMPLE_END`：通过 `page_idle_get()` 检查访问状态
4. 记录时间戳、虚拟地址、PFN 和访问状态

### 5. ELF 解析器库

教育用途的模块化 ELF 解析库，兼容 GNU readelf：

| 模块 | 功能 |
|------|------|
| 核心 | ELF 头部解析（32/64 位） |
| 节区 | 节头表解析 |
| 符号 | `.dynsym` 和 `.symtab` 解析 |
| 重定位 | `.rela.plt` 和 `.rela.dyn` 处理 |
| PLT | PLT 条目反汇编和 GOT 验证 |
| 动态 | `.dynamic` 段解析 |
| 段 | 程序头（加载段） |
| DWARF | 调试行信息 |

## 快速开始

### 前置要求

- Android Studio Hedgehog (2023.1.1) 或更高版本
- Android NDK r29 或更高版本
- CMake 3.22.1+
- Android 设备/模拟器（API 24+，Android 7.0）
- **Root 权限**（用于页面访问追踪）
- 内核需启用 `CONFIG_IDLE_PAGE_TRACKING`

### 构建

```bash
# 构建 Debug APK
./gradlew :app:assembleDebug

# 构建 Release APK
./gradlew :app:assembleRelease

# 仅构建原生库
./gradlew :app:externalNativeBuildDebug
```

### 设备环境准备（设备启动后执行一次）

**注意**：以下步骤只需在设备启动后执行一次，设备重启后需要重新执行：

```bash
# 1. 连接到 Android 设备（如果是网络调试）
adb connect <设备IP>:<端口>

# 2. 获取 Root 权限
adb root

# 3. 推送 PFN Helper 到设备
adb push app/build/intermediates/cxx/Debug/*/obj/*/pfn_helper /data/local/tmp/

# 4. 赋予执行权限
adb shell chmod +x /data/local/tmp/pfn_helper

# 5. 修改 page_idle bitmap 权限（允许非特权访问）
adb shell chmod 666 /sys/kernel/mm/page_idle/bitmap

# 6. 关闭 SELinux（读取 bitmap 需要）
adb shell setenforce 0
```

### 安装 APK

```bash
# 安装到已连接设备
./gradlew :app:installDebug
```

### 基本使用流程

**每次点击"开始模拟"按钮前，需要执行以下完整流程：**

1. **启动应用**
   ```bash
   adb shell am start -n com.example.demo_so/.MainActivity
   ```

2. **启动 PFN Helper**（必须在点击"开始模拟"前执行）
   ```bash
   # 获取应用进程 ID 并启动 PFN Helper
   adb shell /data/local/tmp/pfn_helper $(adb shell pidof com.example.demo_so)
   ```
   **重要**：如果不执行此步骤，`mem_visit.log` 中将无法正确显示页面的冷热情况。

3. 点击 **"开始模拟"** 按钮开始内存模拟

4. 运行所需时长

5. 点击 **"停止模拟"** 按钮停止模拟

6. 拉取日志进行分析

### 拉取日志

```bash
# 拉取内存分配日志
adb pull /storage/emulated/0/Android/data/com.example.demo_so/files/mem_reg.log

# 拉取页面访问日志
adb pull /storage/emulated/0/Android/data/com.example.demo_so/files/mem_visit.log
```

### 可视化分析

使用 `lazy_visit.py` 工具对两份日志进行联合可视化分析：

```bash
# 安装依赖
pip install streamlit pandas numpy plotly pyelftools

# 启动可视化界面
streamlit run lazy_visit.py
```

**功能特性**：
- **内存页访问热力图**：直观展示每块内存的访问周期分布
- **冷热内存分类**：自动识别 Never Accessed、Cold、Warm、Hot 等类别
- **调用堆栈解析**：支持 ELF 符号解析，还原分配调用链
- **详细统计面板**：运行时长、扫描轮次、内存块数、总内存占用

![两份日志结合的可视化分析界面](https://disk.0voice.com/u/En)

## 使用方法

### ELF 解析器 CLI

本项目包含一个完整的 ELF 解析器（位于 `app/src/main/cpp/elf_reader/`），兼容 GNU readelf 的主要功能，用于学习 ELF 文件结构和动态链接机制。

#### 构建与部署

```bash
# 构建 ELF 解析器二进制文件
./gradlew :app:externalNativeBuildDebug

# 推送到设备
adb push app/build/intermediates/cxx/Debug/*/obj/*/elf_reader /data/local/tmp/
adb shell chmod +x /data/local/tmp/elf_reader
```

#### 命令行参数

| 参数 | 功能 | 对应 readelf 参数 |
|------|------|-------------------|
| `-h` | ELF 头部信息 | `-h` |
| `-S` | Section Header 表 | `-S` |
| `-s` | 动态符号表 (.dynsym) | `--dyn-syms` |
| `-r` | 重定位表 (.rela.plt + .rela.dyn) | `-r` |
| `-d` | 动态链接信息 (.dynamic) | `-d` |
| `-l` | PT_LOAD 段信息 | `-l` |
| `-D` | PLT 反汇编 | 无 |
| `-f` | .eh_frame 异常处理帧 | 无 |
| `-g` | DWARF 调试行号 (.debug_line) | 无 |
| `-R` | 只读数据段 (.rodata) | 无 |
| `-a` | 显示全部信息 | `-a` |

#### 使用示例

**1. 查看 ELF 头部信息**

```bash
adb shell /data/local/tmp/elf_reader -h /system/lib64/libc.so
```

输出示例：
```
ELF Header:
  Magic:   7f 45 4c 46 02 01 01 00 00 00 00 00 00 00 00 00
  Class:                             ELF64
  Data:                              2's complement, little endian
  Version:                           1 (current)
  OS/ABI:                            UNIX - System V
  ABI Version:                       0
  Type:                              DYN (Shared object file)
  Machine:                           AArch64
  Entry point address:               0x0
  Start of program headers:          64 (bytes into file)
  Start of section headers:          1408168 (bytes into file)
```

**2. 查看 Section Header 表**

```bash
adb shell /data/local/tmp/elf_reader -S /system/lib64/libc.so
```

输出示例：
```
Section Headers:
  [Nr] Name              Type            Address          Off    Size   ES Flg Lk Inf Al
  [ 0]                   NULL            0000000000000000 000000 000000 00      0   0  0
  [ 1] .note.android.ide NOTE            0000000000000238 000238 000098 00   A  0   0  4
  [ 2] .note.gnu.build-i NOTE            00000000000002d0 0002d0 000024 00   A  0   0  4
  [ 3] .dynsym           DYNSYM          00000000000002f8 0002f8 008b38 18   A  9   1  8
  [ 4] .gnu.version      VERSYM          0000000000088e30 088e30 000b9e 02   A  3   0  2
  [ 5] .gnu.version_r    VERNEED         00000000000899d0 0899d0 000100 00   A  9   3  8
  [ 6] .gnu.hash         GNU_HASH        0000000000089ad0 089ad0 004ccc 00   A  3   0  8
  [ 7] .hash             HASH            00000000000de79c 0de79c 003710 04   A  3   0  4
  [ 8] .rela.dyn         RELA            00000000000e1eb0 0e1eb0 012258 18   A  3   0  8
  [ 9] .dynstr           STRTAB          00000000000f4108 0f4108 0074df 00   A  0   0  1
```

**3. 查看动态符号表**

```bash
adb shell /data/local/tmp/elf_reader -s /system/lib64/libc.so
```

输出示例（部分）：
```
Dynamic symbol table (.dynsym):
  序号   值                大小   类型   绑定   可见性   索引   名称
     0 0000000000000000 00000000 NOTYPE  LOCAL  DEFAULT  UND
     1 0000000000000000 00000000 SECTION LOCAL  DEFAULT    1
   ...
  1129 00000000000550b0 00000504 FUNC   GLOBAL DEFAULT   13 malloc
  1130 00000000000555b4 000002a0 FUNC   GLOBAL DEFAULT   13 free
  1131 0000000000055854 000007d0 FUNC   GLOBAL DEFAULT   13 realloc
```

**4. 查看重定位表**

```bash
adb shell /data/local/tmp/elf_reader -r /system/lib64/libc.so
```

输出示例（部分）：
```
PLT relocations (function jumps):
  Entry  Offset           GOT Index  Symbol
  [   0] 00000000000f6e78 [  3]      __libc_init
  [   1] 00000000000f6e80 [  4]      __stack_chk_fail
  ...
  [  21] 00000000000f6f10 [ 24]      malloc
  [  22] 00000000000f6f18 [ 25]      free

说明：.rela.plt[n] 对应 GOT[n+3]
      rela[21] → GOT[24] (malloc)
```

**5. 查看动态链接信息**

```bash
adb shell /data/local/tmp/elf_reader -d /system/lib64/libc.so
```

输出示例：
```
Dynamic Section (.dynamic):
  依赖库列表 (DT_NEEDED):
    [无]

  SONAME: libc.so
  符号表地址 (DT_SYMTAB): 0x2f8
  字符串表地址 (DT_STRTAB): 0xf4108
  哈希表地址 (DT_HASH): 0xde79c
  PLT 重定位地址 (DT_JMPREL): 0xf6f20
  GNU 哈希表地址 (DT_GNU_HASH): 0x89ad0
```

**6. 查看 PT_LOAD 段信息**

```bash
adb shell /data/local/tmp/elf_reader -l /system/lib64/libc.so
```

输出示例：
```
PT_LOAD 段 (运行时内存映射):
  序号 | 虚拟地址           | 文件偏移 | 文件大小 | 内存大小 | 权限 | 用途
  -----|--------------------|----------|----------|----------|------|----------
  [ 0] | 0x0000000000000000 | 0x000000 | 0x0b3ca0 | 0x0b3ca0 | R E  | 代码段 (.text)
  [ 1] | 0x00000000000b4000 | 0x0b4000 | 0x002298 | 0x0022a0 | RW-  | 数据段 (.data/.bss)
  [ 2] | 0x00000000000d7000 | 0x0d62a0 | 0x00f5b8 | 0x00f5b8 | R--  | 只读数据 (.rodata)
```

**7. PLT 反汇编**

```bash
adb shell /data/local/tmp/elf_reader -D /system/lib64/libc.so
```

输出示例（部分）：
```
PLT Entries (disassembled):
  [  0] 0x00000000000ef890: adrp x16, #0xf6000
             ldr  x17, [x16, #0x8e0]
             add  x16, x16, #0x8e0
             br   x17
        → GOT[3] @ 0xf6e78 (rela[0] __libc_init)

  [ 21] 0x00000000000ef8e4: adrp x16, #0xf6000
             ldr  x17, [x16, #0x910]
             add  x16, x16, #0x910
             br   x17
        → GOT[24] @ 0xf6f10 (rela[21] malloc)
```

**8. 查看异常处理帧 (.eh_frame)**

```bash
adb shell /data/local/tmp/elf_reader -f /system/lib64/libc.so
```

输出示例：
```
.eh_frame 异常处理帧信息:
  CIE 数量: 1
  FDE 数量: 1523

  CIE [0] @ 0x0
    版本: 1
    扩展字符串: zPR
    代码对齐: 4
    数据对齐: -8
    返回寄存器: 30 (x30/LR)
    有 Personality 函数
    有 LSDA 指针

  FDE [0] @ 0x14
    函数地址: 0x54000
    函数大小: 0x100
    对应 CIE: 0
```

**9. 显示全部信息**

```bash
adb shell /data/local/tmp/elf_reader -a /system/lib64/libc.so
```

## 配置

### 空闲页面监控模式

编辑 `so2-hook.cpp` 在编译时选择监控模式：

```cpp
// so2-hook.cpp (约第 142 行)
// 模式选择：
//   0 = SO_CODE_SECTIONS (监控SO代码段，日志显示权限+文件名)
//   1 = HEAP_ALLOCATIONS (监控堆内存，日志显示(heap))
auto mode = idle_page::IdlePageMonitor::MonitorMode::HEAP_ALLOCATIONS;  // 改为 SO_CODE_SECTIONS 切换模式
```

### 采样率

在 `idle_page_timer.h` 中调整采样间隔：

```cpp
constexpr int FAST_MS = 100;    // >10% 页面被访问
constexpr int MEDIUM_MS = 500;  // 1-10% 页面被访问
constexpr int SLOW_MS = 2000;   // <1% 页面被访问
```

### 日志缓冲区大小

在 `log_buffer.h` 中配置缓冲区大小：

```cpp
constexpr size_t RING_BUFFER_SIZE = 16 * 1024 * 1024;  // 16MB
constexpr size_t BATCH_SIZE = 512;                     // TLS 缓冲区
constexpr uint32_t SAMPLE_RATE = 1;                    // 1/1 = 全采样
```

## 技术细节

### ByteHook 集成

ByteHook 通过 Maven 集成，支持 Prefab：

```kotlin
dependencies {
    implementation("com.bytedance:bytehook:1.1.1")
}

android {
    buildFeatures {
        prefab = true
    }
    packaging {
        jniLibs.pickFirsts.add("**/libbytehook.so")
    }
}
```

**Hook 实现模式**：

```cpp
void* my_malloc(size_t size) {
    BYTEHOOK_STACK_SCOPE();  // 第一行必需
    void* result = BYTEHOOK_CALL_PREV(my_malloc, size);
    // ... 日志逻辑 ...
    return result;
}
```

### 无锁环形缓冲区

使用 C++11 原子操作，采用 acquire-release 语义：

```cpp
std::atomic<size_t> write_pos_{0};
std::atomic<size_t> read_pos_{0};

// 生产者（acquire-release 保证可见性）
size_t pos = write_pos_.fetch_add(len, std::memory_order_acq_rel);

// 消费者（acquire 用于同步）
size_t read_pos = read_pos_.load(std::memory_order_acquire);
```

### 内核接口

| 接口 | 用途 | 访问权限 |
|------|------|----------|
| `/proc/self/pagemap` | 虚拟地址到 PFN 转换 | 需要 Root |
| `/sys/kernel/mm/page_idle/bitmap` | 页面访问追踪 | 需要 Root + `CAP_SYS_ADMIN` |
| `/proc/self/maps` | 内存区域枚举 | 无限制 |

## 故障排除

### 构建问题

**x86/x86_64 编译失败**：

```kotlin
// 在 app/build.gradle.kts 中
apply(from = rootProject.file("gradle/prefab_bypass.gradle"))
```

**重复的 libbytehook.so**：
```kotlin
packaging {
    jniLibs.pickFirsts.add("**/libbytehook.so")
}
```

### 运行时问题

**无页面访问数据（`mem_visit.log`全为 -1）**：
- 确保已执行 PFN Helper 启动步骤
- 访问 `/sys/kernel/mm/page_idle/bitmap` 需要 Root 权限
- 检查内核配置中是否启用 `CONFIG_IDLE_PAGE_TRACKING`
- 确认 SELinux 已关闭 (`setenforce 0`)

**内存开销过高**：
- 减小 `log_buffer.h` 中的 `RING_BUFFER_SIZE`
- 增大 `SAMPLE_RATE` 降低采样率

**调用栈缺失**：
- 确保编译器标志包含 `-funwind-tables`
- 检查 `BYTEHOOK_STACK_SCOPE()` 是否为 Hook 函数第一行

**"开始空闲监控"按钮点击无效**：
- 确认 PFN Helper 已正确启动
- 检查 logcat 中是否有权限错误

## 依赖

| 依赖 | 版本 |
|------|------|
| [ByteHook](https://github.com/bytedance/bhook) | 1.1.1 |
| Android NDK | r29 |
| CMake | 3.22.1 |

## 致谢

- [ByteDance ByteHook](https://github.com/bytedance/bhook) - 高性能 PLT Hook 库
- [Linux Kernel](https://www.kernel.org/) - 页面空闲追踪基础设施
- Android NDK 团队 - 原生开发工具

---

**免责声明**：这是一个底层学习项目，仅供教育演示用途。生产使用需要额外的加固和测试。