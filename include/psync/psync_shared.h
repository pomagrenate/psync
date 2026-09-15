#pragma once

// psync_shared.h
// 4-byte shared mutex (reader/writer lock)
// Target: Linux x86-64 / Windows
// Compilers: GCC, Clang, MSVC

#include "psync_platform.h"

namespace psync {

// ============================================================================
// SHARED MUTEX STATE LAYOUT (32 bits)
// ============================================================================
// Bits 31-17: Reserved (future use)
// Bits 16-1:  READER_COUNT (max 65535 readers)
// Bit 0:      WRITER_LOCKED flag (1 = writer holds lock, 0 = no writer)
// ============================================================================

class SharedMutex {
public:
    // State constants
    static constexpr u32 UNLOCKED = 0x00000000u;
    static constexpr u32 WRITER_LOCKED = 0x00000001u;
    static constexpr u32 WRITER_FLAG = 0x00000001u;
    static constexpr u32 READER_SHIFT = 1;
    static constexpr u32 READER_INCREMENT = 0x00000002u;  // 1 << READER_SHIFT
    static constexpr u32 MAX_READERS = 0x0000FFFFu;  // 65535 readers
    
    // Initialize shared mutex (trivial initialization)
    constexpr SharedMutex() : state_(UNLOCKED) {}
    
    // Disable copy and move
    SharedMutex(const SharedMutex&) = delete;
    SharedMutex& operator=(const SharedMutex&) = delete;
    
    // Acquire the mutex for exclusive (writer) access
    void lock() {
        // Fast path: try to acquire uncontended lock
        u32 expected = UNLOCKED;
        if (atomic_compare_exchange(
            &state_,
            &expected,
            WRITER_LOCKED,
            MemoryOrder::acquire,
            MemoryOrder::relaxed
        )) {
            // Successfully acquired lock uncontended
            return;
        }
        
        // Slow path: contended acquisition
        lock_contended();
    }
    
    // Try to acquire the mutex for exclusive (writer) access without blocking
    // Returns true if lock was acquired, false otherwise
    bool try_lock() {
        u32 expected = UNLOCKED;
        return atomic_compare_exchange(
            &state_,
            &expected,
            WRITER_LOCKED,
            MemoryOrder::acquire,
            MemoryOrder::relaxed
        );
    }
    
    // Release the mutex from exclusive (writer) access
    void unlock() {
        // Clear writer locked flag
        atomic_store(&state_, UNLOCKED, MemoryOrder::release);
        
        // Wake all waiters (readers and writers)
        futex::wake_all(&state_);
    }
    
    // Acquire the mutex for shared (reader) access
    void lock_shared() {
        // Fast path: try to acquire uncontended read lock
        u32 current = atomic_load(&state_, MemoryOrder::relaxed);
        
        while (true) {
            // Check if writer is locked
            if ((current & WRITER_FLAG) != 0) {
                // Writer holds lock, wait
                lock_shared_contended();
                return;
            }
            
            // Try to increment reader count
            u32 new_state = current + READER_INCREMENT;
            u32 expected = current;
            
            if (atomic_compare_exchange(
                &state_,
                &expected,
                new_state,
                MemoryOrder::acquire,
                MemoryOrder::relaxed
            )) {
                // Successfully acquired read lock
                return;
            }
            
            // CAS failed, retry
            current = expected;
        }
    }
    
    // Try to acquire the mutex for shared (reader) access without blocking
    // Returns true if lock was acquired, false otherwise
    bool try_lock_shared() {
        u32 current = atomic_load(&state_, MemoryOrder::relaxed);
        
        // Check if writer is locked
        if ((current & WRITER_FLAG) != 0) {
            return false;
        }
        
        // Try to increment reader count
        u32 new_state = current + READER_INCREMENT;
        u32 expected = current;
        
        return atomic_compare_exchange(
            &state_,
            &expected,
            new_state,
            MemoryOrder::acquire,
            MemoryOrder::relaxed
        );
    }
    
    // Release the mutex from shared (reader) access
    void unlock_shared() {
        // Decrement reader count
        u32 prev = atomic_fetch_sub(&state_, READER_INCREMENT, MemoryOrder::release);
        
        // If this was the last reader, wake potential writers
        u32 reader_count = (prev >> READER_SHIFT) - 1;
        if (reader_count == 0) {
            futex::wake(&state_, 1);
        }
    }
    
private:
    // Contended exclusive (writer) lock acquisition
    void lock_contended() {
        while (true) {
            // Adaptive spin
            for (usize i = 0; i < SPIN_INITIAL; ++i) {
                cpu_pause();
                
                // Try to acquire lock
                u32 expected = UNLOCKED;
                if (atomic_compare_exchange(
                    &state_,
                    &expected,
                    WRITER_LOCKED,
                    MemoryOrder::acquire,
                    MemoryOrder::relaxed
                )) {
                    // Successfully acquired lock after spinning
                    return;
                }
            }
            
            // Wait on futex
            u32 current = atomic_load(&state_, MemoryOrder::relaxed);
            futex::wait(&state_, current);
        }
    }
    
    // Contended shared (reader) lock acquisition
    void lock_shared_contended() {
        while (true) {
            // Adaptive spin
            for (usize i = 0; i < SPIN_INITIAL; ++i) {
                cpu_pause();
                
                // Try to acquire read lock
                u32 current = atomic_load(&state_, MemoryOrder::relaxed);
                if ((current & WRITER_FLAG) == 0) {
                    u32 new_state = current + READER_INCREMENT;
                    u32 expected = current;
                    
                    if (atomic_compare_exchange(
                        &state_,
                        &expected,
                        new_state,
                        MemoryOrder::acquire,
                        MemoryOrder::relaxed
                    )) {
                        // Successfully acquired read lock
                        return;
                    }
                }
            }
            
            // Wait on futex
            u32 current = atomic_load(&state_, MemoryOrder::relaxed);
            futex::wait(&state_, current);
        }
    }
    
    volatile u32 state_;
};

// ============================================================================
// SHARED LOCK GUARD (RAII for readers)
// ============================================================================

class SharedLockGuard {
public:
    explicit SharedLockGuard(SharedMutex& mutex) : mutex_(mutex) {
        mutex_.lock_shared();
    }
    
    ~SharedLockGuard() {
        mutex_.unlock_shared();
    }
    
    // Disable copy and move
    SharedLockGuard(const SharedLockGuard&) = delete;
    SharedLockGuard& operator=(const SharedLockGuard&) = delete;
    
private:
    SharedMutex& mutex_;
};

// ============================================================================
// EXCLUSIVE LOCK GUARD (RAII for writers)
// ============================================================================

class UniqueLockGuard {
public:
    explicit UniqueLockGuard(SharedMutex& mutex) : mutex_(mutex) {
        mutex_.lock();
    }
    
    ~UniqueLockGuard() {
        mutex_.unlock();
    }
    
    // Disable copy and move
    UniqueLockGuard(const UniqueLockGuard&) = delete;
    UniqueLockGuard& operator=(const UniqueLockGuard&) = delete;
    
private:
    SharedMutex& mutex_;
};

} // namespace psync
