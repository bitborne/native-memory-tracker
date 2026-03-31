#include <android/log.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <jni.h>
#include <mutex>
#include <queue>
#include <random>
#include <string>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <vector>

#define LOG_TAG "SO1_GameSim"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace std;

// ==================== 内存页常量 ====================
constexpr size_t kPageSize = 4096;
constexpr size_t kPageMask = ~(kPageSize - 1);

// 对齐到4KB边界
inline size_t alignToPage(size_t size) {
    return (size + kPageSize - 1) & kPageMask;
}

// 对齐分配辅助函数（使用 posix_memalign）
inline void* page_aligned_alloc(size_t size) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, kPageSize, size) != 0) {
        return nullptr;
    }
    return ptr;
}

// 计算页数
inline size_t pageCount(size_t size) {
    return (size + kPageSize - 1) / kPageSize;
}

// ==================== 内存热度级别 ====================
enum class MemHotness {
  L1_HOT = 1,  // 极热：每帧访问，永不释放（渲染缓冲区）
  L2_WARM = 2, // 温热：频繁分配释放（游戏对象池）
  L3_COOL = 3, // 凉爽：偶尔访问（配置缓存）
  L4_COLD = 4  // 冰冷：极少访问，大块内存（资源包）
};

// ==================== 内存块元数据 ====================
struct MemBlock {
    void* ptr;
    size_t size;
    MemHotness level;
    uint64_t allocTime;
    uint64_t accessCount = 0;  // 改成普通变量

    MemBlock(void* p, size_t s, MemHotness l)
            : ptr(p), size(s), level(l), allocTime(getTimestamp()) {}

    static uint64_t getTimestamp() {
      return chrono::duration_cast<chrono::milliseconds>(
              chrono::steady_clock::now().time_since_epoch()).count();
    }
};

// ==================== 游戏内存模拟器 ====================
class GameMemorySimulator {
private:
  // L1 热内存：长期持有，每帧高频访问
  vector<MemBlock> hotMemory;
  mutex hotMutex;

  // L4 冷内存：mmap 大块，完全不访问
  vector<pair<void *, size_t>> coldMappings;
  mutex coldMutex;

  // 控制
  atomic<bool> running{false};
  vector<thread> workers;
  random_device rd;
  mt19937 gen{static_cast<unsigned int>(rd())};

public:
  void start() {
    running = true;

    // 启动4个线程对应4种热度
    workers.emplace_back(&GameMemorySimulator::hotThread, this);
    workers.emplace_back(&GameMemorySimulator::warmThread, this);
    workers.emplace_back(&GameMemorySimulator::coolThread, this);
    workers.emplace_back(&GameMemorySimulator::coldThread, this);

    LOGI("=== SO1 Game Simulator Started ===");
    LOGI("L1-HOT: Render buffers (never free)");
    LOGI("L2-WARM: Object pool (high freq malloc/free)");
    LOGI("L3-COOL: Config cache (periodic cleanup)");
    LOGI("L4-COLD: Resource packs (mmap/munmap)");
  }

  void stop() {
      running = false;

      // 分离线程，不阻塞主线程（避免 ANR）
      for (auto& t : workers) {
          if (t.joinable()) t.detach();
      }
      workers.clear();

      // 短暂等待让线程自行退出，然后清理资源
      this_thread::sleep_for(chrono::milliseconds(200));
      cleanup();

      LOGI("=== SO1 Game Simulator Stopped ===");
  }

private:
  // L1: 极热内存 - 渲染循环，每帧访问多页，永不释放
  void hotThread() {
    // 初始分配 10 个 64KB (16页) 缓冲区
    for (int i = 0; i < 10; i++) {
      size_t size = alignToPage(64 * 1024); // 64KB = 16页
      void *p = page_aligned_alloc(size);
      if (p) {
        // 初始化：写入每页的第一个字节
        volatile uint8_t *data = static_cast<volatile uint8_t*>(p);
        for (size_t page = 0; page < pageCount(size); page++) {
          data[page * kPageSize] = 0xAA;
        }
        lock_guard<mutex> lock(hotMutex);
        hotMemory.emplace_back(p, size, MemHotness::L1_HOT);
      }
    }
    LOGI("[L1-HOT] Allocated 10x64KB(16pages) render buffers");

    int frame = 0;
    while (running) {
      {
        lock_guard<mutex> lock(hotMutex);
        for (auto &block : hotMemory) {
          // 高频率访问：每帧访问所有页的第一个字节（模拟渲染数据更新）
          volatile uint8_t *data = static_cast<volatile uint8_t*>(block.ptr);
          size_t pages = pageCount(block.size);
          for (size_t page = 0; page < pages; page++) {
            data[page * kPageSize] = static_cast<uint8_t>(frame % 256);
          }
          block.accessCount++;
        }
      }

      // 每100帧扩容
      if (++frame % 100 == 0) {
        size_t newSize = alignToPage(32 * 1024); // 32KB = 8页
        void *p = page_aligned_alloc(newSize);
        if (p) {
          // 初始化每页
          volatile uint8_t *data = static_cast<volatile uint8_t*>(p);
          for (size_t page = 0; page < pageCount(newSize); page++) {
            data[page * kPageSize] = 0xAA;
          }
          lock_guard<mutex> lock(hotMutex);
          hotMemory.emplace_back(p, newSize, MemHotness::L1_HOT);
          LOGI("[L1-HOT] Expanded +%zuKB(%zupages), total %zu blocks",
               newSize / 1024, pageCount(newSize), hotMemory.size());
        }
      }

      this_thread::sleep_for(chrono::milliseconds(16)); // 60FPS
    }
  }

  // L2: 温热内存 - 对象池，1-2页大小，访问部分页后释放
  void warmThread() {
    // 对象大小：1页或2页（4KB或8KB）
    uniform_int_distribution<> pageDist(1, 2);

    vector<MemBlock> activeObjects;

    while (running) {
      // 批量分配 3-5 个新对象
      uniform_int_distribution<> allocDist(3, 5);
      int allocCount = allocDist(gen);

      for (int i = 0; i < allocCount; i++) {
        size_t pages = pageDist(gen);
        size_t size = pages * kPageSize; // 4KB或8KB
        void *p = page_aligned_alloc(size);
        if (p) {
          // 初始化：写入每页的第一个字节
          volatile uint8_t *data = static_cast<volatile uint8_t*>(p);
          for (size_t pg = 0; pg < pages; pg++) {
            data[pg * kPageSize] = 0xBB;
          }
          activeObjects.emplace_back(p, size, MemHotness::L2_WARM);
        }
      }

      // 模拟对象使用：每 20ms 访问一次
      for (int use = 0; use < 5 && running; use++) {
        for (auto &obj : activeObjects) {
          volatile uint8_t *data = static_cast<volatile uint8_t*>(obj.ptr);
          size_t pages = pageCount(obj.size);
          // 只访问第一页（模拟对象头/元数据访问）
          data[0] = static_cast<uint8_t>(use);
          // 如果对象有2页，偶尔访问第二页
          if (pages > 1 && (gen() % 4) == 0) {
            data[kPageSize] = static_cast<uint8_t>(use);
          }
          obj.accessCount++;
        }
        this_thread::sleep_for(chrono::milliseconds(20));
      }

      // 释放最老的对象
      int releaseCount = min((int)activeObjects.size(), 3);
      for (int i = 0; i < releaseCount && !activeObjects.empty(); i++) {
        free(activeObjects.front().ptr);
        activeObjects.erase(activeObjects.begin());
      }

      this_thread::sleep_for(chrono::milliseconds(10));
    }

    for (auto &obj : activeObjects) {
      free(obj.ptr);
    }
    activeObjects.clear();
  }

  // L3: 凉爽内存 - 配置缓存，4-16页，偶尔访问部分页
  void coolThread() {
    // 缓存大小：4-16页（16KB-64KB）
    uniform_int_distribution<> pageDist(4, 16);
    vector<MemBlock> configCaches;

    while (running) {
      // 分配 2 个缓存块
      for (int i = 0; i < 2; i++) {
        size_t pages = pageDist(gen);
        size_t size = pages * kPageSize;
        void *p = page_aligned_alloc(size);
        if (p) {
          // 初始化：填充配置数据（每页前1KB）
          for (size_t pg = 0; pg < pages; pg++) {
            memset(static_cast<char*>(p) + pg * kPageSize, 'C', 1024);
          }
          configCaches.emplace_back(p, size, MemHotness::L3_COOL);
          LOGI("[L3-COOL] Allocated %zuKB(%zupages)", size / 1024, pages);
        }
      }

      // 保持期间：偶尔访问
      for (int cycle = 0; cycle < 2 && running; cycle++) {
        this_thread::sleep_for(chrono::seconds(2));

        // 偶尔读取（30%概率）
        if (!configCaches.empty() && (gen() % 10) < 3) {
          auto &cache = configCaches[gen() % configCaches.size()];
          volatile uint8_t *data = static_cast<volatile uint8_t*>(cache.ptr);
          size_t pages = pageCount(cache.size);
          // 只随机访问1-2页（模拟只查询部分配置）
          uniform_int_distribution<> pageAccessDist(1, 2);
          int pagesToAccess = min((size_t)pageAccessDist(gen), pages);
          for (int p = 0; p < pagesToAccess; p++) {
            size_t pageIdx = gen() % pages;
            data[pageIdx * kPageSize]++; // 访问页首字节
          }
          cache.accessCount++;
          LOGI("[L3-COOL] Queried %d/%zu pages", pagesToAccess, pages);
        }
      }

      // 清理最老的缓存
      if (configCaches.size() >= 2) {
        size_t n = configCaches.size();
        size_t toRemove = n / 2;
        for (size_t i = 0; i < toRemove && !configCaches.empty(); i++) {
          free(configCaches.back().ptr);
          configCaches.pop_back();
        }
        LOGI("[L3-COOL] Cleaned %zu/%zu blocks", toRemove, n);
      }
    }

    for (auto &b : configCaches) {
      free(b.ptr);
    }
    configCaches.clear();
  }

  // L4: 冰冷内存 - 大资源包，mmap 对齐页大小，完全不访问
  void coldThread() {
    vector<pair<void *, size_t>> coldMappings;

    while (running) {
      // 映射 3 个 4MB (1024页) 资源包
      for (int i = 0; i < 3; i++) {
        size_t size = 1024 * kPageSize; // 4MB，已经是页对齐
        void *p = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p != MAP_FAILED) {
          // 完全不访问 - "冷" 数据的特征
          coldMappings.emplace_back(p, size);
          LOGI("[L4-COLD] Mapped %zuMB(1024pages) - NOT accessed", size / (1024 * 1024));
        }
      }

      // 保持30秒，期间完全不访问
      this_thread::sleep_for(chrono::seconds(30));

      // 30秒后释放
      for (auto &[ptr, size] : coldMappings) {
        munmap(ptr, size);
      }
      size_t count = coldMappings.size();
      coldMappings.clear();
      LOGI("[L4-COLD] Unmapped all %zu blocks (cold data never accessed)", count);
    }
  }

  void cleanup() {
    for (auto &b : hotMemory)
      free(b.ptr);
    hotMemory.clear();

    for (auto &[ptr, size] : coldMappings)
      munmap(ptr, size);
    coldMappings.clear();
  }
};

// ==================== JNI 接口 ====================
static unique_ptr<GameMemorySimulator> g_simulator;

extern "C" JNIEXPORT void JNICALL
Java_com_example_demo_1so_MainActivity_nativeStartSimulation(JNIEnv *env, jobject) {
  if (!g_simulator) {
    g_simulator = make_unique<GameMemorySimulator>();
  }
  g_simulator->start();
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_demo_1so_MainActivity_nativeStopSimulation(JNIEnv *env, jobject) {
  if (g_simulator) {
    g_simulator->stop();
  }
}
