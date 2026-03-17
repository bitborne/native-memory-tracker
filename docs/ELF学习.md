# ELF 结构与 so 跳转机制学习笔记

> 本笔记记录 ELF Reader 开发过程中的学习心得，从代码实践中理解 ELF 文件结构和动态链接机制。

---

## 步骤 1：ELF Header 解析

### 2024-03-17 完成情况

**目标**：理解 ELF 基本元数据（Magic、类型、架构、入口点、Header 位置）

**实现文件**：
- `app/src/main/cpp/elf_reader/elf_types.h`
- `app/src/main/cpp/elf_reader/elf_types.cpp`
- `app/src/main/cpp/elf_reader/main.cpp`
- `app/src/main/cpp/elf_reader/CMakeLists.txt`

### 1.1 ELF 文件结构概览

ELF（Executable and Linkable Format）文件由以下几部分组成：

```
+----------------------------------+
|          ELF Header              |  <-- 52 bytes (32-bit) / 64 bytes (64-bit)
|   - 文件类型、架构、入口点        |
|   - Section Header 偏移位置       |
|   - Program Header 偏移位置       |
+----------------------------------+
|       Program Header Table       |  <-- 段加载信息（运行时用）
|   - PT_LOAD: 可加载段            |
|   - PT_DYNAMIC: 动态链接信息     |
+----------------------------------+
|          .text (代码)            |
|          .data (数据)            |
|          ...                     |
+----------------------------------+
|       Section Header Table       |  <-- 节区信息（链接时用）
|   - .dynsym: 动态符号表          |
|   - .plt: 过程链接表             |
|   - .got.plt: 全局偏移表         |
+----------------------------------+
```

### 1.2 ELF Header 结构详解

#### 1.2.1 e_ident[] - 标识字节数组（16 bytes）

| 索引 | 名称 | 含义 |
|------|------|------|
| 0-3 | EI_MAG0-3 | Magic: `0x7f` 'E' 'L' 'F' |
| 4 | EI_CLASS | 文件类别：1=32位, 2=64位 |
| 5 | EI_DATA | 数据编码：1=小端, 2=大端 |
| 6 | EI_VERSION | ELF 版本，固定为 1 |
| 7 | EI_OSABI | OS/ABI 类型：0=System V, 3=Linux |
| 8-15 | EI_PAD | 填充，保留 |

#### 1.2.2 关键字段（64-bit ELF）

```cpp
struct Elf64_Ehdr {
    uint8_t  e_ident[16];      // 标识信息
    uint16_t e_type;           // 文件类型：ET_DYN=动态库(so), ET_EXEC=可执行文件
    uint16_t e_machine;        // 目标架构：183=ARM64, 62=X86_64, 40=ARM
    uint32_t e_version;        // 版本
    uint64_t e_entry;          // 入口点虚拟地址（so 通常为 0）
    uint64_t e_phoff;          // Program Header 在文件中的偏移
    uint64_t e_shoff;          // Section Header 在文件中的偏移
    uint32_t e_flags;          // 处理器特定标志
    uint16_t e_ehsize;         // ELF Header 大小：64 bytes
    uint16_t e_phentsize;      // 每个 Program Header 条目大小
    uint16_t e_phnum;          // Program Header 条目数量
    uint16_t e_shentsize;      // 每个 Section Header 条目大小
    uint16_t e_shnum;          // Section Header 条目数量
    uint16_t e_shstrndx;       // 字符串表的 Section Header 索引
};
```

### 1.3 字节序处理

ELF 文件可能使用大端或小端格式。我在 `elf_types.cpp` 中实现了通用的读取函数：

```cpp
// 小端读取：低位字节在低地址
template<typename T>
static T readLE(const uint8_t* p) {
    T val = 0;
    for (size_t i = 0; i < sizeof(T); i++) {
        val |= static_cast<T>(p[i]) << (i * 8);
    }
    return val;
}

// 大端读取：高位字节在低地址
template<typename T>
static T readBE(const uint8_t* p) {
    T val = 0;
    for (size_t i = 0; i < sizeof(T); i++) {
        val |= static_cast<T>(p[i]) << ((sizeof(T) - 1 - i) * 8);
    }
    return val;
}
```

**验证方式**：Android 设备上运行的 so 都是小端格式（ARM64 支持双端但通常用小端）。

### 1.4 32-bit vs 64-bit 差异

| 字段 | 32-bit | 64-bit |
|------|--------|--------|
| Header 大小 | 52 bytes | 64 bytes |
| 地址/偏移类型 | uint32_t | uint64_t |
| e_entry 偏移 | 24 | 24 |
| e_phoff 偏移 | 28 | 32 |
| e_shoff 偏移 | 32 | 40 |

**代码技巧**：通过 `is64bit` 标志统一处理两种格式：
```cpp
if (is64bit) {
    e_entry = readVal<uint64_t>(p + 24, isLittleEndian);
} else {
    e_entry = readVal<uint32_t>(p + 24, isLittleEndian);
}
```

### 1.5 运行测试

编译后推送到 Android 设备测试：

```bash
# 编译
./gradlew :app:externalNativeBuildDebug

# 推送并运行
adb push app/build/intermediates/cmake/debug/obj/arm64-v8a/elf_reader /data/local/tmp/
adb shell chmod +x /data/local/tmp/elf_reader
adb shell /data/local/tmp/elf_reader /system/lib64/libc.so
```

**预期输出**（类似 readelf -h）：
```
File: /system/lib64/libc.so
Size: 1234567 bytes

ELF Header:
  Magic:   7f 45 4c 46 02 01 01 00 00 00 00 00 00 00 00 00
  Class:                             ELF64
  Data:                              2's complement, little endian
  Version:                           1 (current)
  OS/ABI:                            UNIX - System V
  ABI Version:                       0
  Type:                              DYN (Shared object file)
  Machine:                           AArch64
  Version:                           0x1
  Entry point address:               0x0
  Start of program headers:          64 (bytes into file)
  Start of section headers:          1234000 (bytes into file)
  Flags:                             0x0
  Size of this header:               64 (bytes)
  Size of program headers:           56 (bytes)
  Number of program headers:         10
  Size of section headers:           64 (bytes)
  Number of section headers:         28
  Section header string table index: 27
```

### 1.6 学习要点总结

1. **Magic 识别**：ELF 文件以 `0x7f 'E' 'L' 'F'` 开头，这是识别 ELF 文件的第一道检查
2. **e_type = ET_DYN**：共享库（.so）的类型是 DYN，而不是 EXEC
3. **e_entry = 0**：共享库没有固定的入口点，由动态链接器决定加载地址
4. **e_shoff 和 e_phoff**：这两个偏移量告诉我们 Section Header 和 Program Header 的位置，是后续解析的关键
5. **e_shstrndx**：Section Header 字符串表的索引，后续解析 Section 名称时需要用到

### 1.7 下一步预告

步骤 2 将解析 **Section Header Table**，重点关注：
- `.dynsym`：动态符号表（导出的函数）
- `.dynstr`：动态字符串表
- `.plt` 和 `.got.plt`：延迟绑定的关键结构
- `.dynamic`：动态链接信息

---

## 参考资料

- [ELF Format Specification](https://refspecs.linuxfoundation.org/elf/elf.pdf)
- [System V ABI ARM64 Supplement](https://github.com/ARM-software/abi-aa/releases)
- Android NDK 中的 `elf.h` 头文件