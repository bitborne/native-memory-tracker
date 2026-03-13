---
title: 'ByteHook (bhook) 集成指南 - 从零开始的 Android Native Hook 开发'
description: '本文面向 Android 开发零基础同学，详细记录将 ByteHook 集成到 Android 项目的完整流程，包括 Gradle 配置、CMake 设置、x86/x86_64 兼容性处理，以及实际项目中遇到的坑和解决方案。'
pubDate: '2026-03-13'
---

# ByteHook (bhook) 集成指南 - 从零开始的 Android Native Hook 开发

> 本文面向 Android 开发零基础同学，详细记录将 ByteHook 集成到 Android 项目的完整流程，以及我们实际项目中遇到的坑和解决方案。

## 一、ByteHook 是什么？

ByteHook 是字节跳动开源的 **Android Native Hook 框架**，基于 PLT (Procedure Linkage Table) Hook 技术，可以拦截 Native 层的函数调用，比如 `malloc`/`free`、`mmap`/`munmap` 等内存操作函数。

适用场景：
- 内存泄漏检测
- 性能监控
- 调用链路追踪
- 安全审计

## 二、前置知识：几个关键概念

在正式配置之前，先了解几个 Android NDK 开发的核心概念：

| 概念 | 通俗解释 |
|------|---------|
| **NDK** | Native Development Kit，用来写 C/C++ 代码并编译成 Android 能运行的二进制文件 |
| **CMake** | 跨平台的构建工具，告诉编译器怎么组织 C/C++ 源文件 |
| **Prefab** | Android Gradle Plugin 4.1+ 引入的机制，让 AAR 包可以暴露 C/C++ 库给其他模块使用 |
| **ABI** | Application Binary Interface，不同的 CPU 架构（arm64-v8a、armeabi-v7a、x86、x86_64）需要不同的二进制文件 |
| **SO 文件** | Shared Object，Android 上的动态链接库，类似 Windows 的 DLL |

## 三、集成步骤（完整版）

### 步骤 1：在 `build.gradle.kts` 中添加依赖

```kotlin
plugins {
    alias(libs.plugins.android.application)
}

android {
    // ... 其他配置
    
    buildFeatures {
        // ★★★ 必须启用 Prefab 支持 ★★★
        // 这是 ByteHook 作为 Prefab 包被引用的前提
        prefab = true
    }
    
    // ★★★ 解决 libbytehook.so 重复打包冲突 ★★★
    // 原因：ByteHook 库本身包含 libbytehook.so，
    // 如果你的项目有多个模块都可能包含这个 so，会报重复错误
    packaging {
        jniLibs.pickFirsts.add("**/libbytehook.so")
    }
}

dependencies {
    // ByteHook 核心库, 注意修改版本号
    implementation("com.bytedance:bytehook:1.1.1") 
    
    // ... 其他依赖
}
```

### 步骤 2：ABI 过滤配置

```kotlin
android {
    defaultConfig {
        ndk {
            // 指定要编译的 ABI 架构
            // ByteHook 支持：arm64-v8a、armeabi-v7a、x86、x86_64
            abiFilters.clear()
            abiFilters += listOf("x86_64", "arm64-v8a", "armeabi-v7a", "x86")
        }
    }
}
```

#### ⚠️ 为什么要 `clear()`？

Android Gradle Plugin 默认可能会给 `abiFilters` 设置默认值，如果直接 `+=` 追加，可能导致列表里有重复的架构或者不想要的架构。先 `clear()` 确保干净状态。

#### ⚠️ x86/x86_64 的特殊处理（重要！）

**这是最容易踩坑的地方！**

ByteHook 官方 README 说支持 x86 和 x86_64，但实际上在 **x86 和 x86_64 架构**下编译时，会有这样的编译错误: 
```bash
[CXX1210] /<project path hidden>/src/main/cpp/CMakeLists.txt debug|x86_64 : No compatible library found [//shadowhook/shadowhook]
```

- 详见: https://github.com/bytedance/bhook/issues/109

**解决方案(bhook 1.1.1)**：需要在项目根目录创建 `gradle/prefab_bypass.gradle` 脚本：

https://github.com/bytedance/bhook/blob/main/gradle/prefab_bypass.gradle

然后在 `build.gradle.kts` 中应用：

```kotlin
// 放在 plugins 块下面，android 块上面
apply(from = rootProject.file("gradle/prefab_bypass.gradle"))
```

**没有这个脚本，在 x86 模拟器或 x86_64 设备上编译会报错！**

### 步骤 3：CMake 配置

在 `app/src/main/cpp/CMakeLists.txt` 中：

```cmake
cmake_minimum_required(VERSION 3.22.1)
project("demo_so")

# 你的主库
add_library(${CMAKE_PROJECT_NAME} SHARED native-lib.cpp)

# 查找系统 log 库
find_library(log-lib log)

# ★★★ 查找 ByteHook 包 ★★★
# 这行会在 Prefab 路径中查找 bytehook 库
find_package(bytehook REQUIRED CONFIG)

# 你的主库链接（如果需要使用 bytehook 的话）
target_link_libraries(${CMAKE_PROJECT_NAME}
    ${log-lib}
    atomic
)

# ★★★ 创建使用 ByteHook 的库 ★★★
# 比如我们的内存检测库 so2
add_library(so2 SHARED so2-hook.cpp log_buffer.cpp log_hooks.cpp)

# 链接 ByteHook 库
# bytehook::bytehook 是 Prefab 暴露的 target
target_link_libraries(so2 bytehook::bytehook log atomic)
```

#### ⚠️ 关于 `bytehook::bytehook`

这是 CMake 的 **Imported Target** 语法。当 `find_package(bytehook REQUIRED CONFIG)` 成功后，会定义一些变量和 target：
- `bytehook::bytehook` - 主要的库 target
- `bytehook_DIR` - 库的配置目录

链接时使用 `bytehook::bytehook`，CMake 会自动处理：
- 头文件路径 (`-I`)
- 库文件路径 (`-L` 和 `-l`)
- 编译选项

### 步骤 4：Java/Kotlin 层加载 SO

```java
public class MainActivity extends AppCompatActivity {
    
    static {
        // 顺序很重要：先加载 bytehook 依赖的库，再加载你的库
        System.loadLibrary("bytehook");  // 可选，通常会自动加载
        System.loadLibrary("so2");       // 你的 Hook 库
        System.loadLibrary("demo_so");   // 你的主库
    }
    
    // ...
}
```

实际上 ByteHook 作为 Prefab 依赖，Gradle 会自动打包 `libbytehook.so`，不需要手动 load。

## 四、完整配置汇总

### `app/build.gradle.kts`

```kotlin
plugins {
    alias(libs.plugins.android.application)
}

// x86/x86_64 兼容性脚本
apply(from = rootProject.file("gradle/prefab_bypass.gradle"))

android {
    namespace = "com.example.demo_so"
    compileSdk = 36
    
    ndkPath = "/opt/android-ndk"  // 你的 NDK 路径
    ndkVersion = "29.0.14206865"

    defaultConfig {
        applicationId = "com.example.demo_so"
        minSdk = 24
        targetSdk = 36
        
        ndk {
            abiFilters.clear()
            abiFilters += listOf("x86_64", "arm64-v8a", "armeabi-v7a", "x86")
        }
    }
    
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
    
    buildFeatures {
        prefab = true
    }
    
    packaging {
        jniLibs.pickFirsts.add("**/libbytehook.so")
    }
}

dependencies {
    implementation("com.bytedance:bytehook:1.1.1")
    // ... 其他依赖
}
```

### `app/src/main/cpp/CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.22.1)
project("demo_so")

# 主库
add_library(demo_so SHARED native-lib.cpp)
find_library(log-lib log)

# 查找 ByteHook
find_package(bytehook REQUIRED CONFIG)

# Hook 库
add_library(so2 SHARED so2-hook.cpp log_buffer.cpp log_hooks.cpp)
target_link_libraries(so2 bytehook::bytehook log atomic)

# 主库链接
target_link_libraries(demo_so log atomic)
```

### `gradle/prefab_bypass.gradle`

见上文步骤 2。

## 五、常见问题 FAQ

### Q1: 编译报错 `Could not find a package configuration file provided by "bytehook"`

**原因**：Prefab 没有正确配置，或者 ABI 不匹配。

**解决**：
1. 检查 `buildFeatures { prefab = true }` 是否添加
2. 检查 NDK 版本是否兼容
3. 检查是否应用了 `prefab_bypass.gradle`（x86/x86_64）

### Q2: gradle 同步报错

```txt
A build operation failed.
The contents of the immutable workspace '/home/<user>/.gradle/caches/9.3.1/transforms/<损坏的目录名>' have been modified.
...
```

**原因**: 这个错误是 Gradle 转换缓存（Transform Cache）损坏 的典型表现。Gradle 的 transforms 目录被设计为"不可变"（immutable），一旦被修改就会触发这个错误

**解决**:

```bash
./gradlew --stop 2>/dev/null
rm -rf ~/.gradle/caches/9.3.1/transforms/<对应损坏的缓存目录>
```


### Q3: 多架构支持问题

ByteHook 官方 AAR 包含的 ABI：
- `arm64-v8a` ✅
- `armeabi-v7a` ✅
- `x86` ✅（需要 bypass 脚本）
- `x86_64` ✅（需要 bypass 脚本）

如果你只需要 arm64，可以简化配置，不需要 bypass 脚本。

### Q4: Hook 不生效

检查点：
1. `bytehook_init()` 是否成功返回 0
2. Hook 的目标库名是否正确（如 `"libdemo_so.so"`）
3. 被 Hook 的函数是否真的被调用了

## 六、与官方 README 的主要区别总结

| 项目 | 官方 README | 我的项目配置 | 原因 |
|------|-----------|--------------|------|
| ABI 过滤 | 未提及 | 显式 `clear()` + 设置 | 避免默认 ABI 干扰 |
| x86/x86_64 支持 | 声称支持 | 需要 `prefab_bypass.gradle` | 官方 Prefab 配置有 bug |
| SO 重复打包 | 未提及 | `pickFirsts` 解决 | 多模块/依赖冲突时需要 |
| CMake 链接 | 示例代码 | 实际项目中使用 `bytehook::bytehook` target | 更规范的 CMake 用法 |
| NDK 版本 | 未指定 | 明确指定 r29 | 避免版本兼容问题 |

## 七、下一步

配置完成后，就可以开始编写 Hook 代码了：

```cpp
#include "bytehook.h"

void* my_malloc(size_t size) {
    BYTEHOOK_STACK_SCOPE();
    // 你的逻辑...
    void* result = BYTEHOOK_CALL_PREV(my_malloc, size);
    return result;
}

// 初始化时
bytehook_init(BYTEHOOK_MODE_AUTOMATIC, true);
bytehook_hook_single("libtarget.so", nullptr, "malloc", (void*)my_malloc, nullptr, nullptr);
```

关于 Hook 代码的编写，将在下一篇文章《ByteHook 实战：内存分配监控》中详细介绍。

---

**参考链接**：
- [ByteHook GitHub](https://github.com/bytedance/bhook)
- [Android NDK 官方文档](https://developer.android.com/ndk/guides)
- [CMake 官方文档](https://cmake.org/documentation/)
