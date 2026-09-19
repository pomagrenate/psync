#pragma once

// psync_platform.h
// Platform abstraction layer for psync synchronization library
// Primary target: Linux x86-64
// Secondary target: Windows (for development/testing)
// Compilers: GCC, Clang, MSVC

// Standard C++ headers required across platforms
#include <cstddef>
#include <cstdint>
#include <ctime>

// Platform detection and headers must come first
#if defined(__linux__)
    #ifndef PSYNC_PLATFORM_LINUX
        #define PSYNC_PLATFORM_LINUX
    #endif
    #include <errno.h>
    #include <sys/syscall.h>
    #include <unistd.h>
    #include <sched.h>
    #include <pthread.h>
    #include <time.h>
#elif defined(_WIN32) || defined(_WIN64)
    #ifndef PSYNC_PLATFORM_WINDOWS
        #define PSYNC_PLATFORM_WINDOWS
    #endif
    // Windows headers must be included first to avoid conflicts
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
    #include <processthreadsapi.h>

    // Undefine intrusive Windows macros that collide with standard C++ APIs and methods
    #ifdef DeleteFile
    #undef DeleteFile
    #endif
    #ifdef CopyFile
    #undef CopyFile
    #endif
    #ifdef CreateFile
    #undef CreateFile
    #endif
    #ifdef MoveFile
    #undef MoveFile
    #endif
    #ifdef GetObject
    #undef GetObject
    #endif
    #ifdef Yield
    #undef Yield
    #endif
    #ifdef min
    #undef min
    #endif
    #ifdef max
    #undef max
    #endif
#else
    #error "Unsupported platform"
#endif

namespace psync {

// ============================================================================
// TYPE DEFINITIONS
// ============================================================================

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i32 = int32_t;
using i64 = int64_t;
using usize = size_t;
using isize = ptrdiff_t;

// ============================================================================
// MEMORY ORDERING
// ============================================================================

#if defined(__GNUC__) || defined(__clang__)
enum class MemoryOrder {
    relaxed = __ATOMIC_RELAXED,
    acquire = __ATOMIC_ACQUIRE,
    release = __ATOMIC_RELEASE,
    acq_rel = __ATOMIC_ACQ_REL
    // seq_cst intentionally omitted from hot paths
};
#else
enum class MemoryOrder {
    relaxed = 0,
    acquire = 2,
    release = 3,
    acq_rel = 4
};
#endif

// ============================================================================
// CPU INTRINSICS (x86-64)
// ============================================================================

inline void cpu_pause() {
    // x86-64 pause instruction
    // Reduces power consumption in spin loops
    // Improves hyperthreading performance
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile("pause" ::: "memory");
#elif defined(_MSC_VER)
    _mm_pause();
#else
    #error "Unsupported compiler for cpu_pause"
#endif
}

inline void compiler_fence() {
    // Full compiler fence (prevents compiler reordering)
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile("" ::: "memory");
#elif defined(_MSC_VER)
    _ReadWriteBarrier();
#else
    #error "Unsupported compiler for compiler_fence"
#endif
}

// ============================================================================
// ATOMIC PRIMITIVES (Compiler Builtins)
// ============================================================================

#if defined(__GNUC__) || defined(__clang__)

template<typename T>
inline T atomic_load(const volatile T* ptr, MemoryOrder order) {
    return __atomic_load_n(ptr, static_cast<int>(order));
}

template<typename T>
inline void atomic_store(volatile T* ptr, T value, MemoryOrder order) {
    __atomic_store_n(ptr, value, static_cast<int>(order));
}

template<typename T>
inline T atomic_exchange(volatile T* ptr, T value, MemoryOrder order) {
    return __atomic_exchange_n(ptr, value, static_cast<int>(order));
}

template<typename T>
inline bool atomic_compare_exchange(
    volatile T* ptr,
    T* expected,
    T desired,
    MemoryOrder success_order,
    MemoryOrder failure_order
) {
    return __atomic_compare_exchange_n(
        ptr,
        expected,
        desired,
        false,
        static_cast<int>(success_order),
        static_cast<int>(failure_order)
    );
}

template<typename T>
inline T atomic_fetch_add(volatile T* ptr, T value, MemoryOrder order) {
    return __atomic_fetch_add(ptr, value, static_cast<int>(order));
}

template<typename T>
inline T atomic_fetch_sub(volatile T* ptr, T value, MemoryOrder order) {
    return __atomic_fetch_sub(ptr, value, static_cast<int>(order));
}

template<typename T>
inline T atomic_fetch_and(volatile T* ptr, T value, MemoryOrder order) {
    return __atomic_fetch_and(ptr, value, static_cast<int>(order));
}

template<typename T>
inline T atomic_fetch_or(volatile T* ptr, T value, MemoryOrder order) {
    return __atomic_fetch_or(ptr, value, static_cast<int>(order));
}

#elif defined(_MSC_VER)

// MSVC atomic intrinsics
#include <intrin.h>

template<typename T>
inline T atomic_load(const volatile T* ptr, MemoryOrder order) {
    // MSVC doesn't support weak ordering, use volatile read
    compiler_fence();
    T value = *ptr;
    compiler_fence();
    return value;
}

template<typename T>
inline void atomic_store(volatile T* ptr, T value, MemoryOrder order) {
    // MSVC doesn't support weak ordering, use volatile write
    compiler_fence();
    *ptr = value;
    compiler_fence();
}

template<typename T>
inline T atomic_exchange(volatile T* ptr, T value, MemoryOrder order) {
    if constexpr (sizeof(T) == 4) {
        return static_cast<T>(_InterlockedExchange(
            reinterpret_cast<volatile long*>(ptr),
            static_cast<long>(value)
        ));
    } else if constexpr (sizeof(T) == 8) {
        return static_cast<T>(_InterlockedExchange64(
            reinterpret_cast<volatile long long*>(ptr),
            static_cast<long long>(value)
        ));
    } else {
        static_assert(sizeof(T) == 4 || sizeof(T) == 8, "Unsupported size for atomic_exchange");
    }
}

template<typename T>
inline bool atomic_compare_exchange(
    volatile T* ptr,
    T* expected,
    T desired,
    MemoryOrder success_order,
    MemoryOrder failure_order
) {
    if constexpr (sizeof(T) == 4) {
        T old = *expected;
        T result = static_cast<T>(_InterlockedCompareExchange(
            reinterpret_cast<volatile long*>(ptr),
            static_cast<long>(desired),
            static_cast<long>(old)
        ));
        if (result == old) {
            return true;
        } else {
            *expected = result;
            return false;
        }
    } else if constexpr (sizeof(T) == 8) {
        T old = *expected;
        T result = static_cast<T>(_InterlockedCompareExchange64(
            reinterpret_cast<volatile long long*>(ptr),
            static_cast<long long>(desired),
            static_cast<long long>(old)
        ));
        if (result == old) {
            return true;
        } else {
            *expected = result;
            return false;
        }
    } else {
        static_assert(sizeof(T) == 4 || sizeof(T) == 8, "Unsupported size for atomic_compare_exchange");
    }
}

template<typename T>
inline T atomic_fetch_add(volatile T* ptr, T value, MemoryOrder order) {
    if constexpr (sizeof(T) == 4) {
        return static_cast<T>(_InterlockedExchangeAdd(
            reinterpret_cast<volatile long*>(ptr),
            static_cast<long>(value)
        ));
    } else if constexpr (sizeof(T) == 8) {
        return static_cast<T>(_InterlockedExchangeAdd64(
            reinterpret_cast<volatile long long*>(ptr),
            static_cast<long long>(value)
        ));
    } else {
        static_assert(sizeof(T) == 4 || sizeof(T) == 8, "Unsupported size for atomic_fetch_add");
    }
}

template<typename T>
inline T atomic_fetch_sub(volatile T* ptr, T value, MemoryOrder order) {
    return atomic_fetch_add(ptr, -value, order);
}

template<typename T>
inline T atomic_fetch_and(volatile T* ptr, T value, MemoryOrder order) {
    if constexpr (sizeof(T) == 4) {
        return static_cast<T>(_InterlockedAnd(
            reinterpret_cast<volatile long*>(ptr),
            static_cast<long>(value)
        ));
    } else if constexpr (sizeof(T) == 8) {
        return static_cast<T>(_InterlockedAnd64(
            reinterpret_cast<volatile long long*>(ptr),
            static_cast<long long>(value)
        ));
    } else {
        static_assert(sizeof(T) == 4 || sizeof(T) == 8, "Unsupported size for atomic_fetch_and");
    }
}

template<typename T>
inline T atomic_fetch_or(volatile T* ptr, T value, MemoryOrder order) {
    if constexpr (sizeof(T) == 4) {
        return static_cast<T>(_InterlockedOr(
            reinterpret_cast<volatile long*>(ptr),
            static_cast<long>(value)
        ));
    } else if constexpr (sizeof(T) == 8) {
        return static_cast<T>(_InterlockedOr64(
            reinterpret_cast<volatile long long*>(ptr),
            static_cast<long long>(value)
        ));
    } else {
        static_assert(sizeof(T) == 4 || sizeof(T) == 8, "Unsupported size for atomic_fetch_or");
    }
}

#else
    #error "Unsupported compiler for atomic primitives"
#endif

// ============================================================================
// FUTEX (Linux syscall) / WaitOnAddress (Windows)
// ============================================================================

namespace futex {

#ifdef PSYNC_PLATFORM_LINUX

// Futex operations (from Linux kernel)
constexpr u32 FUTEX_WAIT = 0;
constexpr u32 FUTEX_WAKE = 1;
constexpr u32 FUTEX_REQUEUE = 12;
constexpr u32 FUTEX_CMP_REQUEUE = 13;
constexpr u32 FUTEX_PRIVATE_FLAG = 128;  // Use process-private futex

// Combined operations
constexpr u32 FUTEX_WAIT_PRIVATE = FUTEX_WAIT | FUTEX_PRIVATE_FLAG;
constexpr u32 FUTEX_WAKE_PRIVATE = FUTEX_WAKE | FUTEX_PRIVATE_FLAG;
constexpr u32 FUTEX_REQUEUE_PRIVATE = FUTEX_REQUEUE | FUTEX_PRIVATE_FLAG;
constexpr u32 FUTEX_CMP_REQUEUE_PRIVATE = FUTEX_CMP_REQUEUE | FUTEX_PRIVATE_FLAG;

// Syscall number for futex (x86-64)
#if defined(SYS_futex)
constexpr usize SYS_FUTEX = static_cast<usize>(SYS_futex);
#else
constexpr usize SYS_FUTEX = 202;
#endif

// Timeout for futex_wait (NULL = block indefinitely)
#if defined(PSYNC_PLATFORM_LINUX)
using timespec = struct ::timespec;
#else
struct timespec {
    i64 tv_sec;
    i64 tv_nsec;
};
#endif

// Raw futex syscall wrapper
inline i32 futex_syscall(
    volatile u32* uaddr,
    u32 futex_op,
    u32 val,
    const timespec* timeout = nullptr,
    volatile u32* uaddr2 = nullptr,
    u32 val3 = 0
) {
    long result = ::syscall(SYS_FUTEX, uaddr, futex_op, val, timeout, uaddr2, val3);
    return (result < 0) ? -errno : static_cast<i32>(result);
}

// Wait on futex if *uaddr == val
// Returns 0 on success, error code on failure
inline i32 wait(volatile u32* uaddr, u32 val) {
    i32 result = futex_syscall(uaddr, FUTEX_WAIT_PRIVATE, val);
    // EAGAIN = value changed before we slept (not an error)
    // EINTR = interrupted by signal (not an error)
    // We ignore errors and treat as spurious wakeup
    return result;
}

// Wait on futex if *uaddr == val with timeout in nanoseconds
// Returns 0 on success, error code on failure
inline i32 wait_for(volatile u32* uaddr, u32 val, u64 timeout_ns) {
    timespec ts;
    ts.tv_sec = static_cast<i64>(timeout_ns / 1000000000ull);
    ts.tv_nsec = static_cast<i64>(timeout_ns % 1000000000ull);
    return futex_syscall(uaddr, FUTEX_WAIT_PRIVATE, val, &ts);
}

// Wake up to count waiters on futex
// Returns number of waiters woken
inline i32 wake(volatile u32* uaddr, i32 count) {
    u32 wake_count = (count <= 0) ? 0x7fffffffu : static_cast<u32>(count);
    i32 result = futex_syscall(uaddr, FUTEX_WAKE_PRIVATE, wake_count);
    return result;
}

// Wake all waiters on futex
inline i32 wake_all(volatile u32* uaddr) {
    return wake(uaddr, 0x7fffffff);
}

// Requeue waiters from one futex to another (Linux only)
// Used for thundering herd mitigation in condition variables
// Uses FUTEX_CMP_REQUEUE_PRIVATE to verify condvar state before requeueing
inline i32 requeue(volatile u32* uaddr, volatile u32* uaddr2, i32 count, u32 expected) {
#ifdef PSYNC_PLATFORM_LINUX
    return futex_syscall(uaddr, FUTEX_CMP_REQUEUE_PRIVATE, 1,
                         reinterpret_cast<const timespec*>(static_cast<uintptr_t>(count)),
                         uaddr2, expected);
#else
    // Fallback for non-Linux: wake all (causes thundering herd)
    (void)expected;  // Suppress unused parameter warning
    return wake_all(uaddr);
#endif
}

#elif defined(PSYNC_PLATFORM_WINDOWS)

// Windows equivalent: WaitOnAddress / WakeByAddressSingle / WakeByAddressAll
// These are available on Windows 8+ and Server 2012+
// For older versions, we'll use a fallback with condition variables
// Windows.h is already included at the top of the file

#include <cerrno>

// Try to use WaitOnAddress if available (Windows 8+)
typedef BOOL (WINAPI* WaitOnAddressFunc)(
    volatile void* Address,
    void* CompareAddress,
    SIZE_T AddressSize,
    DWORD dwMilliseconds
);

typedef void (WINAPI* WakeByAddressSingleFunc)(volatile void* Address);
typedef void (WINAPI* WakeByAddressAllFunc)(volatile void* Address);

static WaitOnAddressFunc s_wait_on_address = nullptr;
static WakeByAddressSingleFunc s_wake_by_address_single = nullptr;
static WakeByAddressAllFunc s_wake_by_address_all = nullptr;

static bool s_initialized = false;

static void init_futex_windows() {
    if (s_initialized) return;
    
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
    HMODULE hmod = GetModuleHandleA("kernelbase.dll");
    if (!hmod) {
        hmod = GetModuleHandleA("api-ms-win-core-synch-l1-2-0.dll");
    }
    if (!hmod) {
        hmod = LoadLibraryA("api-ms-win-core-synch-l1-2-0.dll");
    }
    if (!hmod) {
        hmod = GetModuleHandleA("kernel32.dll");
    }
    if (hmod) {
        s_wait_on_address = reinterpret_cast<WaitOnAddressFunc>(
            GetProcAddress(hmod, "WaitOnAddress")
        );
        s_wake_by_address_single = reinterpret_cast<WakeByAddressSingleFunc>(
            GetProcAddress(hmod, "WakeByAddressSingle")
        );
        s_wake_by_address_all = reinterpret_cast<WakeByAddressAllFunc>(
            GetProcAddress(hmod, "WakeByAddressAll")
        );
    }
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    
    s_initialized = true;
}

// Fallback using Windows SRWLock and Condition Variable
struct fallback_wait_state {
    SRWLOCK lock;
    CONDITION_VARIABLE cond;
    bool waiting;
};

// Simple hash-based fallback storage (limited capacity)
constexpr usize FALLBACK_SLOTS = 64;
static fallback_wait_state s_fallback_slots[FALLBACK_SLOTS];
static SRWLOCK s_fallback_lock = SRWLOCK_INIT;

static bool s_fallback_initialized = false;

static void init_fallback_slots() {
    if (s_fallback_initialized) return;
    
    InitializeSRWLock(&s_fallback_lock);
    for (usize i = 0; i < FALLBACK_SLOTS; ++i) {
        InitializeSRWLock(&s_fallback_slots[i].lock);
        InitializeConditionVariable(&s_fallback_slots[i].cond);
        s_fallback_slots[i].waiting = false;
    }
    
    s_fallback_initialized = true;
}

static usize hash_address(volatile u32* addr) {
    return reinterpret_cast<usize>(addr) % FALLBACK_SLOTS;
}

// Wait on address if *uaddr == val
// Returns 0 on success, -1 on failure
inline i32 wait(volatile u32* uaddr, u32 val) {
    init_futex_windows();
    
    if (s_wait_on_address) {
        // Use WaitOnAddress if available
        BOOL result = s_wait_on_address(
            const_cast<volatile void*>(reinterpret_cast<volatile void*>(uaddr)),
            &val,
            sizeof(u32),
            INFINITE
        );
        return result ? 0 : -1;
    } else {
        // Fallback: use condition variable
        init_fallback_slots();
        
        usize slot = hash_address(uaddr);
        fallback_wait_state* state = &s_fallback_slots[slot];
        
        AcquireSRWLockExclusive(&state->lock);
        state->waiting = true;
        
        // Check if value changed before sleeping (while holding lock to prevent lost wakeup)
        if (atomic_load(uaddr, MemoryOrder::relaxed) != val) {
            state->waiting = false;
            ReleaseSRWLockExclusive(&state->lock);
            return 0;
        }
        
        // SleepConditionVariableSRW atomically releases the lock and waits
        // On wake, it re-acquires the lock automatically
        SleepConditionVariableSRW(&state->cond, &state->lock, INFINITE, 0);
        state->waiting = false;
        ReleaseSRWLockExclusive(&state->lock);
        
        return 0;
    }
}

// Wait on address if *uaddr == val with timeout in nanoseconds
// Returns 0 on success, -1 on timeout/failure
inline i32 wait_for(volatile u32* uaddr, u32 val, u64 timeout_ns) {
    init_futex_windows();
    DWORD ms = static_cast<DWORD>((timeout_ns + 999999ull) / 1000000ull);
    if (ms == 0 && timeout_ns > 0) ms = 1;
    
    if (s_wait_on_address) {
        BOOL result = s_wait_on_address(
            const_cast<volatile void*>(reinterpret_cast<volatile void*>(uaddr)),
            &val,
            sizeof(u32),
            ms
        );
        return result ? 0 : -1;
    } else {
        init_fallback_slots();
        usize slot = hash_address(uaddr);
        fallback_wait_state* state = &s_fallback_slots[slot];
        AcquireSRWLockExclusive(&state->lock);
        state->waiting = true;
        if (atomic_load(uaddr, MemoryOrder::relaxed) != val) {
            state->waiting = false;
            ReleaseSRWLockExclusive(&state->lock);
            return 0;
        }
        BOOL res = SleepConditionVariableSRW(&state->cond, &state->lock, ms, 0);
        state->waiting = false;
        ReleaseSRWLockExclusive(&state->lock);
        return res ? 0 : -1;
    }
}

// Wake up to count waiters on address
// Returns number of waiters woken (approximate on Windows)
inline i32 wake(volatile u32* uaddr, i32 count) {
    init_futex_windows();
    
    if (s_wake_by_address_single && s_wake_by_address_all) {
        // Use WakeByAddress if available
        if (count <= 0) {
            s_wake_by_address_all(const_cast<volatile void*>(reinterpret_cast<volatile void*>(uaddr)));
            return 1;  // Approximate
        } else if (count == 1) {
            s_wake_by_address_single(const_cast<volatile void*>(reinterpret_cast<volatile void*>(uaddr)));
            return 1;
        } else {
            // WakeByAddressAll for count > 1 (Windows doesn't have exact count)
            s_wake_by_address_all(const_cast<volatile void*>(reinterpret_cast<volatile void*>(uaddr)));
            return count;  // Approximate
        }
    } else {
        // Fallback: use condition variable
        init_fallback_slots();
        
        usize slot = hash_address(uaddr);
        fallback_wait_state* state = &s_fallback_slots[slot];
        
        AcquireSRWLockExclusive(&state->lock);
        if (state->waiting) {
            if (count == 1) {
                WakeConditionVariable(&state->cond);
            } else {
                WakeAllConditionVariable(&state->cond);
            }
        }
        ReleaseSRWLockExclusive(&state->lock);
        
        return count;  // Approximate
    }
}

// Wake all waiters on address
inline i32 wake_all(volatile u32* uaddr) {
    return wake(uaddr, -1);
}

#else
    #error "Unsupported platform for futex"
#endif

} // namespace futex

// ============================================================================
// SPIN LOOP HELPERS
// ============================================================================

// Adaptive spin parameters
constexpr usize SPIN_INITIAL = 10;
constexpr usize SPIN_MAX = 100;
constexpr usize BACKOFF_MULTIPLIER = 2;
constexpr usize BACKOFF_MAX = 1000;
constexpr usize FUTEX_THRESHOLD = 3;

// Spin with pause instruction
inline void spin_pause(usize iterations) {
    for (usize i = 0; i < iterations; ++i) {
        cpu_pause();
    }
}

// ============================================================================
// THREAD SUPPORT (for testing only)
// ============================================================================

#ifdef PSYNC_TEST_BUILD

namespace thread {

#ifdef PSYNC_PLATFORM_LINUX

using thread_handle = pthread_t;

inline bool thread_create(thread_handle* handle, void (*func)(void*), void* arg) {
    return pthread_create(handle, nullptr, reinterpret_cast<void*(*)(void*)>(func), arg) == 0;
}

inline void thread_join(thread_handle handle) {
    pthread_join(handle, nullptr);
}

inline void thread_yield() {
    sched_yield();
}

inline usize get_hardware_concurrency() {
    long result = sysconf(_SC_NPROCESSORS_ONLN);
    return result > 0 ? static_cast<usize>(result) : 1;
}

#elif defined(PSYNC_PLATFORM_WINDOWS)

using thread_handle = HANDLE;

// Thread context with safe lifetime management
struct thread_context {
    void (*func)(void*);
    void* arg;
};

inline DWORD WINAPI thread_wrapper(LPVOID param) {
    thread_context* ctx = static_cast<thread_context*>(param);
    void (*func)(void*) = ctx->func;
    void* arg = ctx->arg;
    delete ctx;
    func(arg);
    return 0;
}

inline bool thread_create(thread_handle* handle, void (*func)(void*), void* arg) {
    thread_context* ctx = new thread_context{func, arg};
    *handle = CreateThread(nullptr, 0, thread_wrapper, ctx, 0, nullptr);
    if (*handle == nullptr) {
        delete ctx;
        return false;
    }
    return true;
}

inline void thread_join(thread_handle handle) {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        WaitForSingleObject(handle, INFINITE);
        CloseHandle(handle);
    }
}

inline void thread_yield() {
    SwitchToThread();
}

inline usize get_hardware_concurrency() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return static_cast<usize>(si.dwNumberOfProcessors);
}

#else
    #error "Unsupported platform for thread support"
#endif

} // namespace thread

#endif // PSYNC_TEST_BUILD

// ============================================================================
// ASSERTION (for testing)
// ============================================================================

#ifdef PSYNC_TEST_BUILD
inline void assert_internal(bool condition, const char* msg) {
    if (!condition) {
        // Minimal assertion for testing
#if defined(__GNUC__) || defined(__clang__)
        __builtin_trap();
#elif defined(_MSC_VER)
        __debugbreak();
#endif
    }
}
#define PSYNC_ASSERT(cond) psync::assert_internal(cond, #cond)
#else
#define PSYNC_ASSERT(cond) ((void)0)
#endif

} // namespace psync
