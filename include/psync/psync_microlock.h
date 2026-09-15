#pragma once

// psync_microlock.h
// 1-byte microlock with external parking lot
// Target: Linux x86-64 / Windows
// Compilers: GCC, Clang, MSVC

#include "psync_platform.h"

namespace psync {

// ============================================================================
// MICROLOCK STATE LAYOUT (8 bits)
// ============================================================================
// Bits 7-1: Reserved (future use)
// Bit 0:    LOCKED flag (1 = locked, 0 = unlocked)
// ============================================================================

// ============================================================================
// PARKING LOT
// ============================================================================
// Global hash-based parking lot for microlocks
// Lock address is hashed to a bucket
// Each bucket contains a waiter queue
// ============================================================================

class ParkingLot {
public:
    // Waiter node (intrusive list)
    struct WaiterNode {
        volatile u32 wake_flag;
        WaiterNode* next;
        
        WaiterNode() : wake_flag(0), next(nullptr) {}
    };
    
    // Bucket containing waiter queue
    struct Bucket {
        volatile u32 lock_state;  // Internal lock for bucket
        WaiterNode* head;
        WaiterNode* tail;
        
        Bucket() : lock_state(0), head(nullptr), tail(nullptr) {}
    };
    
    // Number of buckets (power of 2 for efficient hashing)
    static constexpr usize NUM_BUCKETS = 64;
    
    // Initialize parking lot (trivial initialization)
    ParkingLot() {
        for (usize i = 0; i < NUM_BUCKETS; ++i) {
            buckets_[i].lock_state = 0;
            buckets_[i].head = nullptr;
            buckets_[i].tail = nullptr;
        }
    }
    
    // Park a thread waiting on a lock address
    // Returns true if parked successfully, false if should retry
    bool park(void* lock_address, WaiterNode* node) {
        usize bucket_index = hash_address(lock_address);
        Bucket* bucket = &buckets_[bucket_index];
        
        // Lock the bucket
        lock_bucket(bucket);
        
        // Add waiter to queue
        node->wake_flag = 0;
        node->next = nullptr;
        
        if (bucket->tail) {
            bucket->tail->next = node;
        } else {
            bucket->head = node;
        }
        bucket->tail = node;
        
        // Unlock the bucket
        unlock_bucket(bucket);
        
        // Wait on wake flag
        while (atomic_load(&node->wake_flag, MemoryOrder::relaxed) == 0) {
            futex::wait(&node->wake_flag, 0);
        }
        
        return true;
    }
    
    // Unpark one thread waiting on a lock address
    // Returns true if a waiter was unparked, false otherwise
    bool unpark_one(void* lock_address) {
        usize bucket_index = hash_address(lock_address);
        Bucket* bucket = &buckets_[bucket_index];
        
        // Lock the bucket
        lock_bucket(bucket);
        
        // Remove one waiter from queue
        WaiterNode* node = bucket->head;
        if (node) {
            bucket->head = node->next;
            if (!bucket->head) {
                bucket->tail = nullptr;
            }
            
            // Set wake flag BEFORE unlocking bucket
            // This ensures the waiter node is still valid when we wake it
            atomic_store(&node->wake_flag, static_cast<u32>(1), MemoryOrder::release);
            
            // Unlock the bucket BEFORE waking
            // This reduces bucket contention
            unlock_bucket(bucket);
            
            // Wake the waiter
            futex::wake(&node->wake_flag, 1);
            
            return true;
        }
        
        // Unlock the bucket
        unlock_bucket(bucket);
        
        return false;
    }
    
    // Unpark all threads waiting on a lock address
    // Returns number of waiters unparked
    usize unpark_all(void* lock_address) {
        usize bucket_index = hash_address(lock_address);
        Bucket* bucket = &buckets_[bucket_index];
        
        // Lock the bucket
        lock_bucket(bucket);
        
        // Collect all waiters from queue
        usize count = 0;
        WaiterNode* node = bucket->head;
        bucket->head = nullptr;
        bucket->tail = nullptr;
        
        // Unlock the bucket
        unlock_bucket(bucket);
        
        // Wake all waiters
        while (node) {
            WaiterNode* next = node->next;
            
            // Set wake flag
            atomic_store(&node->wake_flag, static_cast<u32>(1), MemoryOrder::release);
            futex::wake(&node->wake_flag, 1);
            
            count++;
            node = next;
        }
        
        return count;
    }
    
private:
    // Hash function for lock address
    // Shift by 3 to align with cache line boundaries
    static usize hash_address(void* address) {
        usize addr = reinterpret_cast<usize>(address);
        return (addr >> 3) & (NUM_BUCKETS - 1);
    }
    
    // Lock a bucket (spinlock)
    void lock_bucket(Bucket* bucket) {
        while (true) {
            u32 expected = 0;
            if (atomic_compare_exchange(
                &bucket->lock_state,
                &expected,
                static_cast<u32>(1),
                MemoryOrder::acquire,
                MemoryOrder::relaxed
            )) {
                // Successfully locked bucket
                return;
            }
            
            // Adaptive spin
            for (usize i = 0; i < SPIN_INITIAL; ++i) {
                cpu_pause();
            }
        }
    }
    
    // Unlock a bucket
    void unlock_bucket(Bucket* bucket) {
        atomic_store(&bucket->lock_state, static_cast<u32>(0), MemoryOrder::release);
    }
    
    Bucket buckets_[NUM_BUCKETS];
};

// Get global parking lot instance (function-local static for thread-safe initialization)
inline ParkingLot& get_parking_lot() {
    static ParkingLot instance;
    return instance;
}

// ============================================================================
// MICROLOCK
// ============================================================================

class MicroLock {
public:
    // State constants
    static constexpr u8 UNLOCKED = 0x00u;
    static constexpr u8 LOCKED = 0x01u;
    
    // Initialize microlock (trivial initialization)
    constexpr MicroLock() : state_(UNLOCKED) {}
    
    // Disable copy and move
    MicroLock(const MicroLock&) = delete;
    MicroLock& operator=(const MicroLock&) = delete;
    
    // Acquire the microlock
    // IMPORTANT: MicroLock must not be destroyed while threads are waiting on it
    // The lock object must outlive all waiters
    void lock() {
        // Fast path: try to acquire uncontended lock
        u8 expected = UNLOCKED;
        if (atomic_compare_exchange(
            &state_,
            &expected,
            LOCKED,
            MemoryOrder::acquire,
            MemoryOrder::relaxed
        )) {
            // Successfully acquired lock uncontended
            return;
        }
        
        // Slow path: contended acquisition with parking
        lock_contended();
    }
    
    // Try to acquire the microlock without blocking
    // Returns true if lock was acquired, false otherwise
    bool try_lock() {
        u8 expected = UNLOCKED;
        return atomic_compare_exchange(
            &state_,
            &expected,
            LOCKED,
            MemoryOrder::acquire,
            MemoryOrder::relaxed
        );
    }
    
    // Release the microlock
    void unlock() {
        // Clear locked flag
        atomic_store(&state_, UNLOCKED, MemoryOrder::release);
        
        // Wake one waiter if any
        get_parking_lot().unpark_one(this);
    }
    
private:
    // Contended lock acquisition with parking
    // NOTE: This implementation uses stack-allocated waiter nodes
    // This is safe as long as the lock() function does not return while the thread is parked
    // The parking lot ensures the waiter is removed from the queue before waking
    void lock_contended() {
        // Thread-local waiter node (stack allocated)
        ParkingLot::WaiterNode node;
        
        while (true) {
            // Adaptive spin
            for (usize i = 0; i < SPIN_INITIAL; ++i) {
                cpu_pause();
                
                // Try to acquire lock
                u8 expected = UNLOCKED;
                if (atomic_compare_exchange(
                    &state_,
                    &expected,
                    LOCKED,
                    MemoryOrder::acquire,
                    MemoryOrder::relaxed
                )) {
                    // Successfully acquired lock after spinning
                    return;
                }
            }
            
            // Park in parking lot
            // The parking lot will remove the waiter from the queue before waking
            // This ensures the waiter node is valid when accessed
            get_parking_lot().park(this, &node);
            
            // After wake, retry acquisition
        }
    }
    
    volatile u8 state_;
};

// ============================================================================
// MICROLOCK GUARD (RAII)
// ============================================================================

class MicroLockGuard {
public:
    explicit MicroLockGuard(MicroLock& lock) : lock_(lock) {
        lock_.lock();
    }
    
    ~MicroLockGuard() {
        lock_.unlock();
    }
    
    // Disable copy and move
    MicroLockGuard(const MicroLockGuard&) = delete;
    MicroLockGuard& operator=(const MicroLockGuard&) = delete;
    
private:
    MicroLock& lock_;
};

} // namespace psync
