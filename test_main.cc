// test_main.cc
// Basic unit tests for psync synchronization library
// Target: Linux x86-64 / Windows
// Compilers: GCC, Clang, MSVC

// Platform-specific headers must be included first
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
#endif

#define PSYNC_TEST_BUILD

#include "include/psync/psync.h"

#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>

using namespace psync;

// ============================================================================
// TEST INFRASTRUCTURE
// ============================================================================

static usize tests_passed = 0;
static usize tests_failed = 0;

// Forward declarations for helper tests
static bool test_platform_cpu_pause();
static bool test_platform_atomic_ops();

#define TEST(name) \
    do { \
        printf("Running test: %s...", #name); \
        if (test_##name()) { \
            printf(" PASSED\n"); \
            tests_passed++; \
        } else { \
            printf(" FAILED\n"); \
            tests_failed++; \
        } \
    } while(0)

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            printf("Assertion failed: %s\n", #cond); \
            return false; \
        } \
    } while(0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NE(a, b) ASSERT_TRUE((a) != (b))

// Helper test functions
static bool test_platform_cpu_pause() {
    cpu_pause();
    return true;
}

static bool test_platform_atomic_ops() {
    u32 value = 0;
    
    // Test atomic store/load
    atomic_store(&value, static_cast<u32>(42), MemoryOrder::relaxed);
    ASSERT_EQ(atomic_load(&value, MemoryOrder::relaxed), static_cast<u32>(42));
    
    // Test atomic exchange
    u32 old = atomic_exchange(&value, static_cast<u32>(100), MemoryOrder::relaxed);
    ASSERT_EQ(old, static_cast<u32>(42));
    ASSERT_EQ(atomic_load(&value, MemoryOrder::relaxed), static_cast<u32>(100));
    
    // Test atomic compare exchange
    u32 expected = 100;
    ASSERT_TRUE(atomic_compare_exchange(&value, &expected, static_cast<u32>(200), MemoryOrder::relaxed, MemoryOrder::relaxed));
    ASSERT_EQ(atomic_load(&value, MemoryOrder::relaxed), static_cast<u32>(200));
    
    // Test atomic fetch add
    u32 result = atomic_fetch_add(&value, static_cast<u32>(50), MemoryOrder::relaxed);
    ASSERT_EQ(result, static_cast<u32>(200));
    ASSERT_EQ(atomic_load(&value, MemoryOrder::relaxed), static_cast<u32>(250));
    
    // Test atomic fetch sub
    result = atomic_fetch_sub(&value, static_cast<u32>(50), MemoryOrder::relaxed);
    ASSERT_EQ(result, static_cast<u32>(250));
    ASSERT_EQ(atomic_load(&value, MemoryOrder::relaxed), static_cast<u32>(200));
    
    return true;
}

// ============================================================================
// MUTEX TESTS
// ============================================================================

static bool test_mutex_single_thread() {
    Mutex mutex;
    
    // Basic lock/unlock
    mutex.lock();
    mutex.unlock();
    
    // Try lock
    ASSERT_TRUE(mutex.try_lock());
    ASSERT_FALSE(mutex.try_lock());  // Should fail while locked
    mutex.unlock();
    
    // RAII guard
    {
        LockGuard guard(mutex);
        // Lock is held here
    }
    // Lock is released here
    
    return true;
}

// ============================================================================
// MICROLOCK TESTS
// ============================================================================

static bool test_microlock_single_thread() {
    MicroLock lock;
    
    // Basic lock/unlock
    lock.lock();
    lock.unlock();
    
    // Try lock
    ASSERT_TRUE(lock.try_lock());
    ASSERT_FALSE(lock.try_lock());  // Should fail while locked
    lock.unlock();
    
    // RAII guard
    {
        MicroLockGuard guard(lock);
        // Lock is held here
    }
    // Lock is released here
    
    return true;
}

// ============================================================================
// SEQLOCK TESTS
// ============================================================================

static bool test_seqlock_single_thread() {
    SeqLock seqlock;
    
    // Basic write operation
    seqlock.write_begin();
    seqlock.write_end();
    
    // Basic read operation
    u32 seq = seqlock.read_begin();
    ASSERT_FALSE(seqlock.read_retry(seq));
    
    // Write guard
    {
        SeqLockWriteGuard guard(seqlock);
        // Write in progress
    }
    
    // Read guard
    {
        SeqLockReadGuard guard(seqlock);
        ASSERT_FALSE(guard.retry());
    }
    
    return true;
}

// ============================================================================
// SHARED MUTEX TESTS
// ============================================================================

static bool test_shared_mutex_single_thread() {
    SharedMutex mutex;
    
    // Exclusive lock/unlock
    mutex.lock();
    mutex.unlock();
    
    // Try exclusive lock
    ASSERT_TRUE(mutex.try_lock());
    ASSERT_FALSE(mutex.try_lock());  // Should fail while locked
    mutex.unlock();
    
    // Shared lock/unlock
    mutex.lock_shared();
    mutex.unlock_shared();
    
    // Try shared lock
    ASSERT_TRUE(mutex.try_lock_shared());
    mutex.unlock_shared();
    
    // RAII guards
    {
        UniqueLockGuard guard(mutex);
        // Exclusive lock held here
    }
    
    {
        SharedLockGuard guard(mutex);
        // Shared lock held here
    }
    
    return true;
}

// ============================================================================
// BATCH TESTS
// ============================================================================

static bool test_batch_guard() {
    Mutex mutex;
    usize counter = 0;
    
    {
        BatchGuard<Mutex> guard(mutex);
        counter++;
        counter++;
        counter++;
    }
    
    ASSERT_EQ(counter, 3);
    return true;
}

static bool test_batch_helpers() {
    Mutex mutex;
    usize counter = 0;
    
    batch_execute(mutex, [&]() {
        counter++;
        counter++;
        counter++;
    });
    
    ASSERT_EQ(counter, 3);
    return true;
}

static bool test_batch_shared() {
    SharedMutex mutex;
    usize counter = 0;
    
    batch_execute_shared(mutex, [&]() {
        counter++;
        counter++;
        counter++;
    });
    
    ASSERT_EQ(counter, 3);
    return true;
}

static bool test_batch_exclusive() {
    SharedMutex mutex;
    usize counter = 0;
    
    batch_execute_exclusive(mutex, [&]() {
        counter++;
        counter++;
        counter++;
    });
    
    ASSERT_EQ(counter, 3);
    return true;
}

// ============================================================================
// CONDITION VARIABLE TESTS
// ============================================================================

static bool test_condvar_single_thread() {
    ConditionVariable cv;
    Mutex mutex;
    
    // Basic signal
    {
        LockGuard guard(mutex);
        cv.signal();
    }
    
    // Basic broadcast
    {
        LockGuard guard(mutex);
        cv.broadcast(mutex);
    }
    
    return true;
}

// ============================================================================
// TAGGED POINTER TESTS
// ============================================================================

static bool test_tagged_ptr_single_thread() {
    struct TestData {
        u64 value;
        u64 padding[7];  // Ensure 8-byte alignment
    };
    
    TestData data;
    data.value = 42;
    
    TaggedLockPtr<TestData> ptr(&data);
    
    // Lock and access
    ptr.lock();
    ASSERT_EQ(ptr.get()->value, 42);
    ptr.unlock();
    
    // Try lock
    ASSERT_TRUE(ptr.try_lock());
    ptr.unlock();
    
    // Exchange
    TestData data2;
    data2.value = 100;
    TestData* old = ptr.exchange(&data2);
    ASSERT_EQ(old->value, 42);
    
    return true;
}

// ============================================================================
// GENERIC RAII LOCK TESTS
// ============================================================================

static bool test_generic_locks() {
    Mutex m;
    {
        LockGuard<Mutex> g(m);
        ASSERT_FALSE(m.try_lock());
    }
    ASSERT_TRUE(m.try_lock());
    m.unlock();

    {
        UniqueLock<Mutex> u(m);
        ASSERT_TRUE(u.owns_lock());
        u.unlock();
        ASSERT_FALSE(u.owns_lock());
        u.lock();
        ASSERT_TRUE(u.owns_lock());
    }

    {
        UniqueLock<Mutex> def(m, defer_lock);
        ASSERT_FALSE(def.owns_lock());
        def.lock();
        ASSERT_TRUE(def.owns_lock());
    }

    SharedMutex sm;
    {
        SharedLock<SharedMutex> sl(sm);
        ASSERT_TRUE(sl.owns_lock());
        // Can take multiple shared locks concurrently
        SharedLock<SharedMutex> sl2(sm);
        ASSERT_TRUE(sl2.owns_lock());
        // Cannot take exclusive lock while shared held
        ASSERT_FALSE(sm.try_lock());
    }
    ASSERT_TRUE(sm.try_lock());
    sm.unlock();

    {
        UniqueLock<SharedMutex> ul(sm);
        ASSERT_TRUE(ul.owns_lock());
        ASSERT_FALSE(sm.try_lock_shared());
    }

    // Condition variable with UniqueLock
    ConditionVariable cv;
    {
        UniqueLock<Mutex> lock(m);
        cv.notify_one();
        cv.notify_all();
    }

    return true;
}

// ============================================================================
// ONCE FLAG / CALL ONCE TESTS
// ============================================================================

static bool test_once_flag_call_once() {
    OnceFlag flag;
    int counter = 0;
    auto increment = [&]() { counter++; };

    call_once(flag, increment);
    call_once(flag, increment);
    call_once(flag, increment);

    ASSERT_EQ(counter, 1);
    return true;
}

// ============================================================================
// MULTI-THREADED & ADVANCED CONCURRENCY TESTS
// ============================================================================

static bool test_locks_self_move() {
    Mutex m;
    {
        UniqueLock<Mutex> ul(m);
        ASSERT_TRUE(ul.owns_lock());
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-move"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
#endif
        ul = std::move(ul);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        ASSERT_TRUE(ul.owns_lock());
        ASSERT_FALSE(m.try_lock());
        ul.unlock();
        ASSERT_FALSE(ul.owns_lock());
        ASSERT_TRUE(m.try_lock());
        m.unlock();
    }
    
    SharedMutex sm;
    {
        SharedLock<SharedMutex> sl(sm);
        ASSERT_TRUE(sl.owns_lock());
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-move"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
#endif
        sl = std::move(sl);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        ASSERT_TRUE(sl.owns_lock());
        ASSERT_FALSE(sm.try_lock());
        sl.unlock();
        ASSERT_FALSE(sl.owns_lock());
        ASSERT_TRUE(sm.try_lock());
        sm.unlock();
    }
    return true;
}

static bool test_seqlock_read_transaction_and_concurrency() {
    SeqLock seqlock;
    struct InvariantData {
        u64 a;
        u64 b;
    } data{500, 500};
    
    std::atomic<bool> stop{false};
    std::atomic<u64> read_count{0};
    std::atomic<bool> invariant_broken{false};
    
    // Writer thread repeatedly updating a and b maintaining a + b == 1000
    std::thread writer([&]() {
        u64 step = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            step++;
            u64 val = step % 1000;
            seqlock.write_begin();
            data.a = val;
            data.b = 1000 - val;
            seqlock.write_end();
        }
    });
    
    // 4 Reader threads continuously checking invariant via read_transaction
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                auto snap = read_transaction(seqlock, [&]() {
                    return data;
                });
                if (snap.a + snap.b != 1000) {
                    invariant_broken.store(true, std::memory_order_relaxed);
                }
                read_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop.store(true, std::memory_order_relaxed);
    
    writer.join();
    for (auto& r : readers) {
        r.join();
    }
    
    ASSERT_FALSE(invariant_broken.load());
    ASSERT_TRUE(read_count.load() > 500);
    return true;
}

static bool test_mutex_multithreaded() {
    Mutex m;
    u64 count = 0;
    constexpr int num_threads = 4;
    constexpr int iters = 5000;
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < iters; ++i) {
                LockGuard<Mutex> g(m);
                count++;
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    ASSERT_EQ(count, static_cast<u64>(num_threads * iters));
    return true;
}

static bool test_microlock_multithreaded_contention() {
    MicroLock lock1;
    MicroLock lock2;
    u64 count1 = 0;
    u64 count2 = 0;
    
    constexpr int num_threads = 4;
    constexpr int iters_per_thread = 5000;
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < iters_per_thread; ++i) {
                if (t % 2 == 0) {
                    MicroLockGuard g(lock1);
                    count1++;
                } else {
                    MicroLockGuard g(lock2);
                    count2++;
                }
            }
        });
    }
    
    for (auto& th : threads) {
        th.join();
    }
    
    ASSERT_EQ(count1, static_cast<u64>((num_threads / 2) * iters_per_thread));
    ASSERT_EQ(count2, static_cast<u64>((num_threads - (num_threads / 2)) * iters_per_thread));
    return true;
}

static bool test_shared_mutex_multithreaded() {
    SharedMutex sm;
    u64 shared_val = 0;
    std::atomic<bool> stop{false};
    std::atomic<u64> reads{0};
    
    std::vector<std::thread> writers;
    for (int w = 0; w < 2; ++w) {
        writers.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                UniqueLock<SharedMutex> lock(sm);
                shared_val++;
            }
        });
    }
    
    std::vector<std::thread> readers;
    for (int r = 0; r < 4; ++r) {
        readers.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                SharedLock<SharedMutex> lock(sm);
                u64 v = shared_val;
                (void)v;
                reads.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop.store(true, std::memory_order_relaxed);
    
    for (auto& wr : writers) wr.join();
    for (auto& rd : readers) rd.join();
    
    ASSERT_TRUE(shared_val > 0);
    ASSERT_TRUE(reads.load() > 0);
    return true;
}

static bool test_condvar_multithreaded_and_timeout() {
    ConditionVariable cv;
    Mutex m;
    
    // 1. Timed wait test - timeout case
    {
        UniqueLock<Mutex> lock(m);
        auto start = std::chrono::steady_clock::now();
        bool signaled = cv.wait_for(lock, std::chrono::milliseconds(20), []() { return false; });
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        ASSERT_FALSE(signaled);
        ASSERT_TRUE(elapsed >= 15);
    }
    
    // 2. Timed wait test - success case
    {
        bool ready = false;
        std::thread waiter([&]() {
            UniqueLock<Mutex> lock(m);
            cv.wait_for(lock, std::chrono::seconds(2), [&]() { return ready; });
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        {
            UniqueLock<Mutex> lock(m);
            ready = true;
            cv.notify_one();
        }
        waiter.join();
        ASSERT_TRUE(ready);
    }
    
    // 3. Multi-threaded Producer-Consumer queue
    {
        std::vector<int> queue;
        bool done = false;
        constexpr int total_items = 200;
        int items_consumed = 0;
        
        std::thread consumer([&]() {
            while (true) {
                UniqueLock<Mutex> lock(m);
                cv.wait(lock, [&]() { return !queue.empty() || done; });
                while (!queue.empty()) {
                    queue.pop_back();
                    items_consumed++;
                }
                if (done && queue.empty()) {
                    break;
                }
            }
        });
        
        for (int i = 0; i < total_items; ++i) {
            {
                UniqueLock<Mutex> lock(m);
                queue.push_back(i);
                cv.notify_one();
            }
        }
        
        {
            UniqueLock<Mutex> lock(m);
            done = true;
            cv.notify_all();
        }
        
        consumer.join();
        ASSERT_EQ(items_consumed, total_items);
    }
    
    return true;
}

static bool test_tagged_ptr_multithreaded_contention() {
    struct CounterData {
        u64 count;
        u64 pad[7];
    };
    CounterData cdata{0, {0}};
    TaggedLockPtr<CounterData> lock_ptr(&cdata);
    
    constexpr int num_threads = 4;
    constexpr int iters_per_thread = 5000;
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < iters_per_thread; ++i) {
                lock_ptr.lock();
                lock_ptr.get()->count++;
                lock_ptr.unlock();
            }
        });
    }
    
    for (auto& th : threads) {
        th.join();
    }
    
    ASSERT_EQ(lock_ptr.get()->count, static_cast<u64>(num_threads * iters_per_thread));
    return true;
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

int main() {
    printf("=== PSYNC SYNCHRONIZATION LIBRARY TESTS ===\n\n");
    
    printf("--- Platform Tests ---\n");
    TEST(platform_cpu_pause);
    TEST(platform_atomic_ops);
    
    printf("\n--- Mutex Tests ---\n");
    TEST(mutex_single_thread);
    TEST(mutex_multithreaded);
    
    printf("\n--- MicroLock Tests ---\n");
    TEST(microlock_single_thread);
    TEST(microlock_multithreaded_contention);
    
    printf("\n--- SeqLock Tests ---\n");
    TEST(seqlock_single_thread);
    TEST(seqlock_read_transaction_and_concurrency);
    
    printf("\n--- Shared Mutex Tests ---\n");
    TEST(shared_mutex_single_thread);
    TEST(shared_mutex_multithreaded);
    
    printf("\n--- Batch Tests ---\n");
    TEST(batch_guard);
    TEST(batch_helpers);
    TEST(batch_shared);
    TEST(batch_exclusive);
    
    printf("\n--- Condition Variable Tests ---\n");
    TEST(condvar_single_thread);
    TEST(condvar_multithreaded_and_timeout);
    
    printf("\n--- Generic RAII Lock Tests ---\n");
    TEST(generic_locks);
    TEST(locks_self_move);

    printf("\n--- OnceFlag / CallOnce Tests ---\n");
    TEST(once_flag_call_once);
    
    printf("\n--- Tagged Pointer Tests ---\n");
    TEST(tagged_ptr_single_thread);
    TEST(tagged_ptr_multithreaded_contention);
    
    printf("\n=== TEST SUMMARY ===\n");
    printf("Passed: %zu\n", tests_passed);
    printf("Failed: %zu\n", tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}