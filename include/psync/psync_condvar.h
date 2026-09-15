#pragma once

// psync_condvar.h
// Zero-libc condition variable with thundering herd mitigation
// Target: Linux x86-64
// Compilers: GCC, Clang

#include "psync_platform.h"

namespace psync {

// ============================================================================
// CONDITION VARIABLE
// ============================================================================
// Simple futex-based condition variable for both Linux and Windows
// Requires predicate re-checking by caller (standard condition variable semantics)
// ============================================================================

class ConditionVariable {
public:
    // Initialize condition variable
    constexpr ConditionVariable() : state_(EMPTY) {}
    
    // Disable copy and move
    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;
    
    // Wait on condition variable
    // Must be called with mutex locked
    // Caller must re-check predicate after wake (standard condition variable semantics)
    void wait(Mutex& mutex) {
        // Signal that we are waiting
        u32 current = atomic_fetch_or(&state_, WAITERS, MemoryOrder::relaxed);
        
        // Release mutex
        mutex.unlock();
        
        // Wait on futex
        // This may return spuriously - caller must re-check predicate
        futex::wait(&state_, current | WAITERS);
        
        // Re-acquire mutex
        mutex.lock();
    }
    
    // Wake one waiting thread
    void signal() {
        futex::wake(&state_, 1);
    }
    
    // Wake all waiting threads
    void broadcast(Mutex& mutex) {
        (void)mutex;  // Suppress unused parameter warning
#ifdef PSYNC_PLATFORM_LINUX
        // Linux: Use FUTEX_CMP_REQUEUE_PRIVATE for thundering herd mitigation
        u32 current = atomic_load(&state_, MemoryOrder::relaxed);
        if ((current & WAITERS) == 0) {
            return;
        }
        
        u32 expected = current;
        while (true) {
            u32 new_state = current & ~WAITERS;
            if (atomic_compare_exchange(
                &state_,
                &expected,
                new_state,
                MemoryOrder::relaxed,
                MemoryOrder::relaxed
            )) {
                volatile u32* mutex_state = mutex.get_state_address();
                futex::requeue(&state_, mutex_state, 0x7fffffff, current);
                return;
            }
            current = expected;
            
            if ((current & WAITERS) == 0) {
                return;
            }
        }
#else
        // Windows: Wake all (thundering herd on Windows is acceptable fallback)
        futex::wake_all(&state_);
#endif
    }
    
private:
    volatile u32 state_;
    static constexpr u32 EMPTY = 0x00000000u;
    static constexpr u32 WAITERS = 0x00000001u;
};

// ============================================================================
// CONDITION VARIABLE GUARD (RAII)
// ============================================================================

class ConditionVariableGuard {
public:
    ConditionVariableGuard(ConditionVariable& cv, Mutex& mutex) 
        : cv_(cv), mutex_(mutex) {
        cv_.wait(mutex_);
    }
    
    ~ConditionVariableGuard() {
        // Mutex is automatically re-acquired by wait()
    }
    
    // Disable copy and move
    ConditionVariableGuard(const ConditionVariableGuard&) = delete;
    ConditionVariableGuard& operator=(const ConditionVariableGuard&) = delete;
    
private:
    ConditionVariable& cv_;
    Mutex& mutex_;
};

} // namespace psync