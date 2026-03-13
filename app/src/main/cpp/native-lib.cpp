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
  // L1 热内存：长期持有，模拟渲染数据
  vector<MemBlock> hotMemory;
  mutex hotMutex;

  // L2 温内存：对象池，高频分配释放
  queue<MemBlock> warmPool;
  mutex warmMutex;

  // L3 凉爽内存：定期清理
  vector<MemBlock> coolMemory;
  mutex coolMutex;

  // L4 冷内存：mmap 大块
  vector<pair<void *, size_t>> coldMappings;
  mutex coldMutex;

  // 控制
  atomic<bool> running{false};
  vector<thread> workers;
  random_device rd;
  mt19937 gen{rd()};

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
  // L1: 极热内存 - 渲染循环，每帧访问，永不释放
  void hotThread() {
    // 初始分配 10 个 64KB 缓冲区
    for (int i = 0; i < 10; i++) {
      size_t size = 64 * 1024;
      void *p = malloc(size);
      if (p) {
        memset(p, 0xAA, size);
        lock_guard<mutex> lock(hotMutex);
        hotMemory.emplace_back(p, size, MemHotness::L1_HOT);
      }
    }
    LOGI("[L1-HOT] Allocated 10x64KB render buffers");

    int frame = 0;
    while (running) {
      // 每帧"访问"（模拟 GPU 数据更新）
      {
        lock_guard<mutex> lock(hotMutex);
        for (auto &block : hotMemory) {
          memset(block.ptr, frame % 256, 1024); // 写前1KB
          block.accessCount++;
        }
      }

      // 每100帧扩容（模拟新资源）
      if (++frame % 100 == 0) {
        size_t newSize = 32 * 1024;
        void *p = malloc(newSize);
        if (p) {
          lock_guard<mutex> lock(hotMutex);
          hotMemory.emplace_back(p, newSize, MemHotness::L1_HOT);
          LOGI("[L1-HOT] Expanded +%zuKB, total %zu blocks", newSize / 1024,
               hotMemory.size());
        }
      }

      this_thread::sleep_for(chrono::milliseconds(16)); // 60FPS
    }
  }

  // L2: 温热内存 - 对象池，高频 malloc/free
  void warmThread() {
    uniform_int_distribution<> sizeDist(256, 8192); // 256B-8KB
    uniform_int_distribution<> lifeDist(1, 10);     // 存活1-10ms

    while (running) {
      // 批量分配 5 个对象
      for (int i = 0; i < 5; i++) {
        size_t size = sizeDist(gen);
        void *p = malloc(size);
        if (p) {
          memset(p, 0xBB, size);
          lock_guard<mutex> lock(warmMutex);
          warmPool.push(MemBlock(p, size, MemHotness::L2_WARM));
        }
      }

      // 模拟对象生命周期后释放
      this_thread::sleep_for(chrono::milliseconds(lifeDist(gen)));

      {
        lock_guard<mutex> lock(warmMutex);
        int releaseCount = min((int)warmPool.size(), 3);
        for (int i = 0; i < releaseCount && !warmPool.empty(); i++) {
          free(warmPool.front().ptr);
          warmPool.pop();
        }
      }

      this_thread::sleep_for(chrono::milliseconds(5));
    }
  }

  // L3: 凉爽内存 - 配置缓存，定期清理
  void coolThread() {
    uniform_int_distribution<> sizeDist(16 * 1024, 256 * 1024); // 16-256KB

    while (running) {
      // 分配 3 个缓存块
      for (int i = 0; i < 3; i++) {
        size_t size = sizeDist(gen);
        void *p = calloc(1, size); // 清零分配
        if (p) {
          // 模拟填充数据
          string dummy(size / 2, 'C');
          memcpy(p, dummy.data(), dummy.size());

          lock_guard<mutex> lock(coolMutex);
          coolMemory.emplace_back(p, size, MemHotness::L3_COOL);
          LOGI("[L3-COOL] Allocated %zuKB", size / 1024);
        }
      }

      // 保持5秒后清理一半
      this_thread::sleep_for(chrono::seconds(5));

      {
        lock_guard<mutex> lock(coolMutex);
        size_t n = coolMemory.size();
        for (size_t i = 0; i < n / 2 && !coolMemory.empty(); i++) {
          free(coolMemory.back().ptr);
          coolMemory.pop_back();
        }
        LOGI("[L3-COOL] Cleaned %zu/%zu blocks", n / 2, n);
      }
    }
  }

  // L4: 冰冷内存 - 大资源包，mmap/munmap
  void coldThread() {
    while (running) {
      // 映射 3 个 4MB 资源包
      for (int i = 0; i < 3; i++) {
        size_t size = 4 * 1024 * 1024; // 4MB
        void *p = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p != MAP_FAILED) {
          // 延迟初始化（冷数据特征）
          this_thread::sleep_for(chrono::milliseconds(50));
          memset(p, 0xDD, 4096); // 只写第一页

          lock_guard<mutex> lock(coldMutex);
          coldMappings.emplace_back(p, size);
          LOGI("[L4-COLD] Mapped %zuMB", size / (1024 * 1024));
        }
      }

      // 保持30秒（模拟资源生命周期）
      this_thread::sleep_for(chrono::seconds(30));

      {
        lock_guard<mutex> lock(coldMutex);
        for (auto &[ptr, size] : coldMappings) {
          munmap(ptr, size);
        }
        size_t count = coldMappings.size();
        coldMappings.clear();
        LOGI("[L4-COLD] Unmapped all %zu blocks", count);
      }
    }
  }

  void cleanup() {
    for (auto &b : hotMemory)
      free(b.ptr);
    hotMemory.clear();

    while (!warmPool.empty()) {
      free(warmPool.front().ptr);
      warmPool.pop();
    }

    for (auto &b : coolMemory)
      free(b.ptr);
    coolMemory.clear();

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
