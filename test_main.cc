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
// MAIN TEST RUNNER
// ============================================================================

int main() {
    printf("=== PSYNC SYNCHRONIZATION LIBRARY TESTS ===\n\n");
    
    printf("--- Platform Tests ---\n");
    TEST(platform_cpu_pause);
    TEST(platform_atomic_ops);
    
    printf("\n--- Mutex Tests ---\n");
    TEST(mutex_single_thread);
    
    printf("\n--- MicroLock Tests ---\n");
    TEST(microlock_single_thread);
    
    printf("\n--- SeqLock Tests ---\n");
    TEST(seqlock_single_thread);
    
    printf("\n--- Shared Mutex Tests ---\n");
    TEST(shared_mutex_single_thread);
    
    printf("\n--- Batch Tests ---\n");
    TEST(batch_guard);
    TEST(batch_helpers);
    TEST(batch_shared);
    TEST(batch_exclusive);
    
    printf("\n--- Condition Variable Tests ---\n");
    TEST(condvar_single_thread);
    
    printf("\n--- Generic RAII Lock Tests ---\n");
    TEST(generic_locks);

    printf("\n--- OnceFlag / CallOnce Tests ---\n");
    TEST(once_flag_call_once);
    
    printf("\n--- Tagged Pointer Tests ---\n");
    TEST(tagged_ptr_single_thread);
    
    printf("\n=== TEST SUMMARY ===\n");
    printf("Passed: %zu\n", tests_passed);
    printf("Failed: %zu\n", tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}