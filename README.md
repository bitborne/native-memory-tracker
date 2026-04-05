# Android Native Memory Tracking & Analysis - Low-Level Learning Project

[![Android](https://img.shields.io/badge/Android-API_24+-green.svg)](https://developer.android.com)
[![NDK](https://img.shields.io/badge/NDK-r29-blue.svg)](https://developer.android.com/ndk)
[![ByteHook](https://img.shields.io/badge/ByteHook-1.1.1-orange.svg)](https://github.com/bytedance/bhook)

English | [中文](./README.zh-CN.md)

A comprehensive Android native memory analysis project for learning low-level Android development, demonstrating **ByteDance's ByteHook PLT hook library** for memory allocation tracking, combined with **Linux kernel page idle monitoring** for complete memory access pattern analysis.

- [Android Native Memory Tracking \& Analysis - Low-Level Learning Project](#android-native-memory-tracking--analysis---low-level-learning-project)
  - [Overview](#overview)
  - [Project Goals](#project-goals)
  - [Architecture](#architecture)
  - [Features](#features)
    - [1. Memory Hook Library (libso2.so)](#1-memory-hook-library-libso2so)
    - [2. Game Memory Simulator (libdemo\_so.so)](#2-game-memory-simulator-libdemo_soso)
    - [3. Lock-Free Logging System](#3-lock-free-logging-system)
    - [4. Idle Page Monitor](#4-idle-page-monitor)
    - [5. ELF Reader Library](#5-elf-reader-library)
  - [Quick Start](#quick-start)
    - [Prerequisites](#prerequisites)
    - [Build](#build)
    - [Device Environment Setup (Execute once after device boot)](#device-environment-setup-execute-once-after-device-boot)
    - [Install APK](#install-apk)
    - [Basic Usage Flow](#basic-usage-flow)
    - [Pull Logs](#pull-logs)
    - [Visualization Analysis](#visualization-analysis)
  - [Usage](#usage)
    - [ELF Reader CLI](#elf-reader-cli)
      - [Build and Deploy](#build-and-deploy)
      - [Command Line Options](#command-line-options)
      - [Usage Examples](#usage-examples)
  - [Configuration](#configuration)
    - [Idle Page Monitor Mode](#idle-page-monitor-mode)
    - [Sampling Rate](#sampling-rate)
    - [Log Buffer Size](#log-buffer-size)
  - [Technical Details](#technical-details)
    - [ByteHook Integration](#bytehook-integration)
    - [Lock-Free Ring Buffer](#lock-free-ring-buffer)
    - [Kernel Interfaces](#kernel-interfaces)
  - [Troubleshooting](#troubleshooting)
    - [Build Issues](#build-issues)
    - [Runtime Issues](#runtime-issues)
  - [Dependencies](#dependencies)
  - [Acknowledgments](#acknowledgments)


---



## Overview

This project demonstrates advanced Android native development techniques for memory profiling and analysis:

- **Memory Allocation Hooking**: Intercept `malloc`, `free`, `mmap`, and other memory operations using PLT hook
- **Lock-Free Logging**: High-performance multi-level buffering system with minimal overhead
- **Page Access Tracking**: Monitor physical memory access patterns via `/sys/kernel/mm/page_idle/bitmap`
- **Game Memory Simulation**: Realistic memory allocation patterns mimicking game engine behavior

## Project Goals

| Goal | Solution | Output |
|------|----------|--------|
| **Trace memory allocation/free stack** | App Hook memory functions + stack trace | `mem_reg.log` |
| **Memory usage status** | Device Root + Kernel with IdlePage enabled | `mem_visit.log` |
| **ELF file parsing** | Modular ELF Reader compatible with readelf | CLI tool `elf_reader` |

![Monitoring Logs Display](https://disk.0voice.com/u/EQ)

## Architecture

![Memory Access Monitoring Architecture](https://disk.0voice.com/u/EP)

![Memory Access Data Flow Diagram](https://disk.0voice.com/u/ER)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          Android Application                            │
├─────────────────────────────────────────────────────────────────────────┤
│  ┌──────────────┐      ┌──────────────┐      ┌──────────────────────┐   │
│  │   Game Sim   │      │  Hook Lib    │      │   Idle Page Monitor  │   │
│  │ (libdemo_so) │◄────►│  (libso2)    │◄────►│  (Kernel Interface)  │   │
│  └──────────────┘      └──────┬───────┘      └──────────────────────┘   │
│                               │                                         │
│                      ┌────────┴────────┐                                │
│                      ▼                 ▼                                │
│           ┌─────────────────┐  ┌───────────────┐                        │
│           │ ByteHook (bhook)│  │ Lock-Free Log │                        │
│           │  PLT Hooking    │  │   Buffer      │                        │
│           └─────────────────┘  └───────┬───────┘                        │
│                                        │                                │
│                                        ▼                                │
│                              ┌──────────────────┐                       │
│                              │   mem_reg.log    │                       │
│                              │   mem_visit.log  │                       │
│                              └──────────────────┘                       │
└─────────────────────────────────────────────────────────────────────────┘
```

## Features

### 1. Memory Hook Library (libso2.so)

Hooks 9 memory allocation functions with full call stack capture:

| Function | Description |
|----------|-------------|
| `malloc` / `free` | Standard heap allocation |
| `calloc` | Zero-initialized allocation |
| `realloc` | Resize existing allocation |
| `mmap` / `munmap` / `mmap64` | Memory-mapped files |
| `posix_memalign` | Aligned allocation (POSIX) |
| `aligned_alloc` | Aligned allocation (C11) |

**Log Format** (CSV):
```
timestamp_us,type,address,req_size,actual_size,tid,0,0,stack_addrs...
```

### 2. Game Memory Simulator (libdemo_so.so)

Simulates four memory "hotness" levels across separate threads:

| Level | Name | Pattern | Size | Lifecycle |
|-------|------|---------|------|-----------|
| L1 | **HOT** | Render buffers | 64KB (16 pages) | Never freed, 60FPS updates |
| L2 | **WARM** | Object pool | 4KB-8KB | 1-10ms lifecycle |
| L3 | **COOL** | Config cache | 16KB-64KB | Cleared every 5s |
| L4 | **COLD** | Resource packs | 4MB mmap | 30s lifecycle |

### 3. Lock-Free Logging System

Three-tier buffering architecture for zero-contention logging:

```
Thread-Local Buffer (512B)
           │
           ▼
Lock-Free Ring Buffer (16MB)  ◄── Atomic operations only
           │
           ▼
Background Writer Thread
           │
           ▼
      Disk (fdatasync)
```

**Key Optimizations**:
- `thread_local` buffers eliminate thread contention
- Lock-free ring buffer using C++11 atomics
- Batch processing: 16 records per TLS flush, 64 per disk write
- `fdatasync()` every 16KB for durability

### 4. Idle Page Monitor

Tracks physical memory page access patterns using Linux kernel interfaces.

**Requirements**:
- Root access for `/sys/kernel/mm/page_idle/bitmap`
- Kernel with `CONFIG_IDLE_PAGE_TRACKING` enabled
- PFN Helper utility is required to properly observe page hot/cold status

**Dual Monitoring Modes**:

| Mode | Target | Source |
|------|--------|--------|
| SO_CODE_SECTIONS | SO code/data segments | `/proc/self/maps` |
| HEAP_ALLOCATIONS | Dynamic allocations | malloc/mmap hooks |

**Sampling Workflow**:
1. `SAMPLE_START`: Clear PTE Accessed bit via `page_idle_set()`
2. Wait for sampling interval (adaptive: 10ms-1s)
3. `SAMPLE_END`: Check access status via `page_idle_get()`
4. Log results with timestamp, vaddr, PFN, and access status

### 5. ELF Reader Library

Educational modular ELF parsing library compatible with GNU readelf:

| Module | Functionality |
|--------|--------------|
| Core | ELF header parsing (32/64-bit) |
| Sections | Section header table parsing |
| Symbols | `.dynsym` and `.symtab` parsing |
| Relocations | `.rela.plt` and `.rela.dyn` handling |
| PLT | PLT entry disassembly and GOT verification |
| Dynamic | `.dynamic` segment parsing |
| Segments | Program header (load segments) |
| DWARF | Debug line information |

## Quick Start

### Prerequisites

- Android Studio Hedgehog (2023.1.1) or later
- Android NDK r29 or later
- CMake 3.22.1+
- Android device/emulator with API 24+ (Android 7.0)
- **Root access** (required for page access tracking)
- Kernel with `CONFIG_IDLE_PAGE_TRACKING` enabled

### Build

```bash
# Build debug APK
./gradlew :app:assembleDebug

# Build release APK
./gradlew :app:assembleRelease

# Build native libraries only
./gradlew :app:externalNativeBuildDebug
```

### Device Environment Setup (Execute once after device boot)

**Note**: The following steps only need to be executed once after the device boots. They must be re-executed after a device reboot:

```bash
# 1. Connect to Android device (if using network debugging)
adb connect <device_ip>:<port>

# 2. Get root access
adb root

# 3. Push PFN Helper to device
adb push app/build/intermediates/cxx/Debug/*/obj/*/pfn_helper /data/local/tmp/

# 4. Grant execute permission
adb shell chmod +x /data/local/tmp/pfn_helper

# 5. Change page_idle bitmap permissions (allow non-privileged access)
adb shell chmod 666 /sys/kernel/mm/page_idle/bitmap

# 6. Disable SELinux (required for debugging)
adb shell setenforce 0
```

### Install APK

```bash
# Install to connected device
./gradlew :app:installDebug
```

### Basic Usage Flow

**Before each time you click the "Start Simulation" button, execute the following complete process:**

1. **Launch the app**
   ```bash
   adb shell am start -n com.example.demo_so/.MainActivity
   ```

2. **Start PFN Helper** (Must be executed before clicking "Start Simulation")
   ```bash
   # Get app process ID and start PFN Helper
   adb shell /data/local/tmp/pfn_helper $(adb shell pidof com.example.demo_so)
   ```
   **Important**: If this step is not executed, `mem_visit.log` will not show correct page hot/cold information.

3. Tap **"Start Simulation"** button to begin memory simulation

4. Run for your desired duration

5. Tap **"Stop Simulation"** button to stop simulation

6. Pull logs for analysis

### Pull Logs

```bash
# Pull memory allocation logs
adb pull /storage/emulated/0/Android/data/com.example.demo_so/files/mem_reg.log

# Pull page access logs
adb pull /storage/emulated/0/Android/data/com.example.demo_so/files/mem_visit.log
```

### Visualization Analysis

Use `lazy_visit.py` tool for joint visualization analysis of both log files:

```bash
# Install dependencies
pip install streamlit pandas numpy plotly pyelftools

# Launch visualization interface
streamlit run lazy_visit.py
```

**Features**:
- **Memory Page Access Heatmap**: Visualize access cycle distribution for each memory block
- **Hot/Cold Memory Classification**: Automatically identifies Never Accessed, Cold, Warm, Hot categories
- **Call Stack Resolution**: Supports ELF symbol resolution to restore allocation call chains
- **Detailed Statistics Panel**: Runtime duration, scan rounds, block count, total memory usage

![Combined logs visualization analysis interface](https://disk.0voice.com/u/En)

## Usage

### ELF Reader CLI

This project includes a complete ELF parser (located in `app/src/main/cpp/elf_reader/`), compatible with GNU readelf's main features, designed for learning ELF file structure and dynamic linking mechanisms.

#### Build and Deploy

```bash
# Build ELF reader binary
./gradlew :app:externalNativeBuildDebug

# Push to device
adb push app/build/intermediates/cxx/Debug/*/obj/*/elf_reader /data/local/tmp/
adb shell chmod +x /data/local/tmp/elf_reader
```

#### Command Line Options

| Option | Function | readelf Equivalent |
|--------|----------|-------------------|
| `-h` | ELF header information | `-h` |
| `-S` | Section Header table | `-S` |
| `-s` | Dynamic symbol table (.dynsym) | `--dyn-syms` |
| `-r` | Relocation tables (.rela.plt + .rela.dyn) | `-r` |
| `-d` | Dynamic linking info (.dynamic) | `-d` |
| `-l` | PT_LOAD segment info | `-l` |
| `-D` | PLT disassembly | N/A |
| `-f` | .eh_frame exception handling frames | N/A |
| `-g` | DWARF debug line info (.debug_line) | N/A |
| `-R` | Read-only data section (.rodata) | N/A |
| `-a` | Show all information | `-a` |

#### Usage Examples

**1. View ELF Header**

```bash
adb shell /data/local/tmp/elf_reader -h /system/lib64/libc.so
```

Output example:
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

**2. View Section Header Table**

```bash
adb shell /data/local/tmp/elf_reader -S /system/lib64/libc.so
```

Output example:
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

**3. View Dynamic Symbol Table**

```bash
adb shell /data/local/tmp/elf_reader -s /system/lib64/libc.so
```

Output example (partial):
```
Dynamic symbol table (.dynsym):
  Num    Value            Size   Type   Bind   Vis      Ndx   Name
     0 0000000000000000 00000000 NOTYPE  LOCAL  DEFAULT  UND
     1 0000000000000000 00000000 SECTION LOCAL  DEFAULT    1
   ...
  1129 00000000000550b0 00000504 FUNC   GLOBAL DEFAULT   13 malloc
  1130 00000000000555b4 000002a0 FUNC   GLOBAL DEFAULT   13 free
  1131 0000000000055854 000007d0 FUNC   GLOBAL DEFAULT   13 realloc
```

**4. View Relocation Tables**

```bash
adb shell /data/local/tmp/elf_reader -r /system/lib64/libc.so
```

Output example (partial):
```
PLT relocations (function jumps):
  Entry  Offset           GOT Index  Symbol
  [   0] 00000000000f6e78 [  3]      __libc_init
  [   1] 00000000000f6e80 [  4]      __stack_chk_fail
  ...
  [  21] 00000000000f6f10 [ 24]      malloc
  [  22] 00000000000f6f18 [ 25]      free

Note: .rela.plt[n] corresponds to GOT[n+3]
      rela[21] → GOT[24] (malloc)
```

**5. View Dynamic Linking Information**

```bash
adb shell /data/local/tmp/elf_reader -d /system/lib64/libc.so
```

Output example:
```
Dynamic Section (.dynamic):
  Dependency libraries (DT_NEEDED):
    [None]

  SONAME: libc.so
  Symbol table address (DT_SYMTAB): 0x2f8
  String table address (DT_STRTAB): 0xf4108
  Hash table address (DT_HASH): 0xde79c
  PLT relocation address (DT_JMPREL): 0xf6f20
  GNU hash table address (DT_GNU_HASH): 0x89ad0
```

**6. View PT_LOAD Segment Information**

```bash
adb shell /data/local/tmp/elf_reader -l /system/lib64/libc.so
```

Output example:
```
PT_LOAD segments (runtime memory mapping):
  No. | Virtual Address    | File Off | File Sz  | Mem Sz   | Perm | Usage
  ----|--------------------|----------|----------|----------|------|----------
  [ 0] | 0x0000000000000000 | 0x000000 | 0x0b3ca0 | 0x0b3ca0 | R E  | Code (.text)
  [ 1] | 0x00000000000b4000 | 0x0b4000 | 0x002298 | 0x0022a0 | RW-  | Data (.data/.bss)
  [ 2] | 0x00000000000d7000 | 0x0d62a0 | 0x00f5b8 | 0x00f5b8 | R--  | Read-only (.rodata)
```

**7. PLT Disassembly**

```bash
adb shell /data/local/tmp/elf_reader -D /system/lib64/libc.so
```

Output example (partial):
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

**8. View Exception Handling Frames (.eh_frame)**

```bash
adb shell /data/local/tmp/elf_reader -f /system/lib64/libc.so
```

Output example:
```
.eh_frame exception handling frames:
  CIE count: 1
  FDE count: 1523

  CIE [0] @ 0x0
    Version: 1
    Augmentation: zPR
    Code alignment: 4
    Data alignment: -8
    Return register: 30 (x30/LR)
    Has personality function
    Has LSDA pointer

  FDE [0] @ 0x14
    Function address: 0x54000
    Function size: 0x100
    Associated CIE: 0
```

**9. Show All Information**

```bash
adb shell /data/local/tmp/elf_reader -a /system/lib64/libc.so
```

## Configuration

### Idle Page Monitor Mode

Edit `so2-hook.cpp` to select monitoring mode at compile time:

```cpp
// so2-hook.cpp (around line 142)
// Mode selection:
//   0 = SO_CODE_SECTIONS (monitor SO code sections, log shows permissions+filename)
//   1 = HEAP_ALLOCATIONS (monitor heap memory, log shows "(heap)")
auto mode = idle_page::IdlePageMonitor::MonitorMode::HEAP_ALLOCATIONS;  // Change to SO_CODE_SECTIONS to switch mode
```

### Sampling Rate

Adjust sampling interval in `idle_page_timer.h`:

```cpp
constexpr int FAST_MS = 100;    // >10% pages accessed
constexpr int MEDIUM_MS = 500;  // 1-10% pages accessed
constexpr int SLOW_MS = 2000;   // <1% pages accessed
```

### Log Buffer Size

Configure buffer sizes in `log_buffer.h`:

```cpp
constexpr size_t RING_BUFFER_SIZE = 16 * 1024 * 1024;  // 16MB
constexpr size_t BATCH_SIZE = 512;                     // TLS buffer
constexpr uint32_t SAMPLE_RATE = 1;                    // 1/1 = full
```

## Technical Details

### ByteHook Integration

ByteHook is integrated via Maven with Prefab support:

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

**Hook Implementation Pattern**:

```cpp
void* my_malloc(size_t size) {
    BYTEHOOK_STACK_SCOPE();  // Required first line
    void* result = BYTEHOOK_CALL_PREV(my_malloc, size);
    // ... logging logic ...
    return result;
}
```

### Lock-Free Ring Buffer

Uses C++11 atomics with acquire-release semantics:

```cpp
std::atomic<size_t> write_pos_{0};
std::atomic<size_t> read_pos_{0};

// Producer (acquire-release for visibility)
size_t pos = write_pos_.fetch_add(len, std::memory_order_acq_rel);

// Consumer (acquire for synchronization)
size_t read_pos = read_pos_.load(std::memory_order_acquire);
```

### Kernel Interfaces

| Interface | Purpose | Access |
|-----------|---------|--------|
| `/proc/self/pagemap` | Virtual to PFN translation | Require root |
| `/sys/kernel/mm/page_idle/bitmap` | Page access tracking | Requires root + `CAP_SYS_ADMIN` |
| `/proc/self/maps` | Memory region enumeration | Unrestricted |

## Troubleshooting

### Build Issues

**x86/x86_64 compilation fails**:
```kotlin
// In app/build.gradle.kts
apply(from = rootProject.file("gradle/prefab_bypass.gradle"))
```

**Duplicate libbytehook.so**:
```kotlin
packaging {
    jniLibs.pickFirsts.add("**/libbytehook.so")
}
```

### Runtime Issues

**No page access data (all -1 in `mem_visit.log`)**:
- Ensure PFN Helper has been started
- Root access required for `/sys/kernel/mm/page_idle/bitmap`
- Check `CONFIG_IDLE_PAGE_TRACKING` in kernel config
- Verify SELinux is disabled (`setenforce 0`)

**High memory overhead**:
- Reduce `RING_BUFFER_SIZE` in `log_buffer.h`
- Increase `SAMPLE_RATE` for lower sampling

**Missing stack traces**:
- Ensure `-funwind-tables` compiler flag
- Check `BYTEHOOK_STACK_SCOPE()` is first in hook function

**"Start Idle Monitor" button not working**:
- Confirm PFN Helper is properly started
- Check logcat for permission errors

## Dependencies

| Dependency | Version |
|------------|---------|
| [ByteHook](https://github.com/bytedance/bhook) | 1.1.1 |
| Android NDK | r29 |
| CMake | 3.22.1 |

## Acknowledgments

- [ByteDance ByteHook](https://github.com/bytedance/bhook) - High-performance PLT hook library
- [Linux Kernel](https://www.kernel.org/) - Page idle tracking infrastructure
- Android NDK Team - Native development tools

---

**Disclaimer**: This is a low-level learning project for educational purposes. Production use requires additional hardening and testing.
