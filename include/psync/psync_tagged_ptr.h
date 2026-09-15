#pragma once

// psync_tagged_ptr.h
// Zero-byte overhead lock using tagged pointer with alignment bits
// Target: Linux x86-64 / Windows
// Compilers: GCC, Clang, MSVC

#include "psync_platform.h"

namespace psync {

// ============================================================================
// MINIMAL PARKING LOT FOR TAGGED POINTER
// ============================================================================

class TaggedPtrParkingLot {
public:
    struct WaiterNode {
        volatile u32 wake_flag;
        WaiterNode* next;
        
        WaiterNode() : wake_flag(0), next(nullptr) {}
    };
    
    struct Bucket {
        volatile u32 lock_state;
        WaiterNode* head;
        WaiterNode* tail;
        
        Bucket() : lock_state(0), head(nullptr), tail(nullptr) {}
    };
    
    static constexpr usize NUM_BUCKETS = 64;
    
    TaggedPtrParkingLot() {
        for (usize i = 0; i < NUM_BUCKETS; ++i) {
            buckets_[i].lock_state = 0;
            buckets_[i].head = nullptr;
            buckets_[i].tail = nullptr;
        }
    }
    
    bool park(void* lock_address, WaiterNode* node) {
        usize bucket_index = hash_address(lock_address);
        Bucket* bucket = &buckets_[bucket_index];
        
        lock_bucket(bucket);
        
        node->wake_flag = 0;
        node->next = nullptr;
        
        if (bucket->tail) {
            bucket->tail->next = node;
        } else {
            bucket->head = node;
        }
        bucket->tail = node;
        
        unlock_bucket(bucket);
        
        while (atomic_load(&node->wake_flag, MemoryOrder::relaxed) == 0) {
            futex::wait(&node->wake_flag, 0);
        }
        
        return true;
    }
    
    bool unpark_one(void* lock_address) {
        usize bucket_index = hash_address(lock_address);
        Bucket* bucket = &buckets_[bucket_index];
        
        lock_bucket(bucket);
        
        WaiterNode* node = bucket->head;
        if (node) {
            bucket->head = node->next;
            if (!bucket->head) {
                bucket->tail = nullptr;
            }
            
            atomic_store(&node->wake_flag, static_cast<u32>(1), MemoryOrder::release);
            unlock_bucket(bucket);
            futex::wake(&node->wake_flag, 1);
            return true;
        }
        
        unlock_bucket(bucket);
        return false;
    }
    
private:
    static usize hash_address(void* address) {
        usize addr = reinterpret_cast<usize>(address);
        return (addr >> 3) & (NUM_BUCKETS - 1);
    }
    
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
                return;
            }
            for (usize i = 0; i < SPIN_INITIAL; ++i) {
                cpu_pause();
            }
        }
    }
    
    void unlock_bucket(Bucket* bucket) {
        atomic_store(&bucket->lock_state, static_cast<u32>(0), MemoryOrder::release);
    }
    
    Bucket buckets_[NUM_BUCKETS];
};

// Global parking lot instance
inline TaggedPtrParkingLot& get_tagged_ptr_parking_lot() {
    static TaggedPtrParkingLot instance;
    return instance;
}

// ============================================================================
// TAGGED LOCK POINTER
// ============================================================================
// Uses low-order alignment bits for lock state and waiter flags
// Assumes 8-byte alignment (3 low bits are naturally zero)
// Bit 0: Locked state
// Bit 1: Waiters present flag
// Bit 2: Reserved for user/Marked flag
// Bits 3-63: Actual pointer value (NO SHIFTING - direct bitmasking)
// ============================================================================

template<typename T>
class TaggedLockPtr {
public:
    // Bit layout (direct bitmasking, no shifting)
    static constexpr u64 LOCKED_BIT = 0x1ull;       // Bit 0
    static constexpr u64 WAITERS_BIT = 0x2ull;     // Bit 1
    static constexpr u64 MARKED_BIT = 0x4ull;      // Bit 2 (reserved for user)
    static constexpr u64 FLAG_MASK = 0x7ull;       // Bits 0-2 (all flags)
    static constexpr u64 POINTER_MASK = ~FLAG_MASK; // Bits 3-63 (pointer)
    
    // Initialize with null pointer
    constexpr TaggedLockPtr() : tagged_ptr_(0) {}
    
    // Initialize with pointer (requires 8-byte alignment)
    explicit TaggedLockPtr(T* ptr) {
        PSYNC_ASSERT((reinterpret_cast<u64>(ptr) & FLAG_MASK) == 0);
        tagged_ptr_ = reinterpret_cast<u64>(ptr);
    }
    
    // Disable copy and move
    TaggedLockPtr(const TaggedLockPtr&) = delete;
    TaggedLockPtr& operator=(const TaggedLockPtr&) = delete;
    
    // Get raw pointer (atomically, with direct bitmasking)
    T* get() const {
        u64 current = atomic_load(&tagged_ptr_, MemoryOrder::acquire);
        return reinterpret_cast<T*>(current & POINTER_MASK);
    }
    
    // Lock acquisition (uncontended fast path)
    void lock() {
        while (true) {
            u64 current = atomic_load(&tagged_ptr_, MemoryOrder::relaxed);
            
            // Check if already locked
            if ((current & LOCKED_BIT) != 0) {
                // Contended path
                lock_contended();
                return;
            }
            
            // Try to set locked bit
            u64 new_state = current | LOCKED_BIT;
            u64 expected = current;
            if (atomic_compare_exchange(
                &tagged_ptr_,
                &expected,
                new_state,
                MemoryOrder::acquire,
                MemoryOrder::relaxed
            )) {
                // Successfully acquired lock
                return;
            }
            // CAS failed, retry
        }
    }
    
    // Try lock without blocking
    bool try_lock() {
        u64 current = atomic_load(&tagged_ptr_, MemoryOrder::relaxed);
        
        // Check if already locked
        if ((current & LOCKED_BIT) != 0) {
            return false;
        }
        
        // Try to set locked bit
        u64 new_state = current | LOCKED_BIT;
        u64 expected = current;
        return atomic_compare_exchange(
            &tagged_ptr_,
            &expected,
            new_state,
            MemoryOrder::acquire,
            MemoryOrder::relaxed
        );
    }
    
    // Unlock
    void unlock() {
        while (true) {
            u64 current = atomic_load(&tagged_ptr_, MemoryOrder::relaxed);
            
            // Check if waiters present
            if ((current & WAITERS_BIT) != 0) {
                // Clear locked bit and waiters bit, wake one waiter
                u64 new_state = current & ~(LOCKED_BIT | WAITERS_BIT);
                u64 expected = current;
                if (atomic_compare_exchange(
                    &tagged_ptr_,
                    &expected,
                    new_state,
                    MemoryOrder::release,
                    MemoryOrder::relaxed
                )) {
                    // Successfully unlocked, wake one waiter
                    get_tagged_ptr_parking_lot().unpark_one(this);
                    return;
                }
                // CAS failed, retry
                continue;
            }
            
            // No waiters, just clear locked bit
            u64 new_state = current & ~LOCKED_BIT;
            u64 expected = current;
            if (atomic_compare_exchange(
                &tagged_ptr_,
                &expected,
                new_state,
                MemoryOrder::release,
                MemoryOrder::relaxed
            )) {
                // Successfully unlocked
                return;
            }
            // CAS failed, retry
        }
    }
    
    // Atomic pointer swap (exchange)
    // Returns old pointer (with direct bitmasking)
    T* exchange(T* new_ptr) {
        PSYNC_ASSERT((reinterpret_cast<u64>(new_ptr) & FLAG_MASK) == 0);
        
        u64 new_tagged = reinterpret_cast<u64>(new_ptr);
        u64 old_tagged = atomic_exchange(&tagged_ptr_, new_tagged, MemoryOrder::acq_rel);
        return reinterpret_cast<T*>(old_tagged & POINTER_MASK);
    }
    
    // Atomic compare-and-swap pointer
    // Returns true if successful (with direct bitmasking)
    bool compare_exchange(T* expected, T* desired) {
        PSYNC_ASSERT((reinterpret_cast<u64>(expected) & FLAG_MASK) == 0);
        PSYNC_ASSERT((reinterpret_cast<u64>(desired) & FLAG_MASK) == 0);
        
        u64 expected_tagged = reinterpret_cast<u64>(expected);
        u64 desired_tagged = reinterpret_cast<u64>(desired);
        
        return atomic_compare_exchange(
            &tagged_ptr_,
            &expected_tagged,
            desired_tagged,
            MemoryOrder::acq_rel,
            MemoryOrder::relaxed
        );
    }
    
    // Dereference (atomically get pointer)
    T* operator->() const {
        return get();
    }
    
    // Dereference (atomically get pointer)
    T& operator*() const {
        return *get();
    }
    
private:
    // Contended lock acquisition with parking
    void lock_contended() {
        // Thread-local waiter node (stack allocated)
        TaggedPtrParkingLot::WaiterNode node;
        
        while (true) {
            // Adaptive spin
            for (usize i = 0; i < SPIN_INITIAL; ++i) {
                cpu_pause();
                
                // Try to acquire lock
                u64 current = atomic_load(&tagged_ptr_, MemoryOrder::relaxed);
                if ((current & LOCKED_BIT) == 0) {
                    u64 new_state = current | LOCKED_BIT;
                    u64 expected = current;
                    if (atomic_compare_exchange(
                        &tagged_ptr_,
                        &expected,
                        new_state,
                        MemoryOrder::acquire,
                        MemoryOrder::relaxed
                    )) {
                        // Successfully acquired lock after spinning
                        return;
                    }
                }
            }
            
            // Set waiters flag and park
            while (true) {
                u64 current = atomic_load(&tagged_ptr_, MemoryOrder::relaxed);
                
                // If lock is unlocked, try to acquire it directly
                if ((current & LOCKED_BIT) == 0) {
                    u64 new_state = current | LOCKED_BIT;
                    u64 expected = current;
                    if (atomic_compare_exchange(
                        &tagged_ptr_,
                        &expected,
                        new_state,
                        MemoryOrder::acquire,
                        MemoryOrder::relaxed
                    )) {
                        // Successfully acquired lock
                        return;
                    }
                    // CAS failed, retry
                    continue;
                }
                
                // Lock is still locked, try to set waiters flag
                if ((current & WAITERS_BIT) == 0) {
                    u64 new_state = current | WAITERS_BIT;
                    u64 expected = current;
                    if (atomic_compare_exchange(
                        &tagged_ptr_,
                        &expected,
                        new_state,
                        MemoryOrder::relaxed,
                        MemoryOrder::relaxed
                    )) {
                        // Successfully set waiters flag
                        break;
                    }
                    // CAS failed, retry
                    continue;
                }
                
                // Waiters flag already set, proceed to park
                break;
            }
            
            // Park in parking lot
            get_tagged_ptr_parking_lot().park(this, &node);
            
            // After wake, retry acquisition
        }
    }
    
    volatile u64 tagged_ptr_;
};

// ============================================================================
// TAGGED LOCK POINTER GUARD (RAII)
// ============================================================================

template<typename T>
class TaggedLockGuard {
public:
    explicit TaggedLockGuard(TaggedLockPtr<T>& ptr) : ptr_(ptr) {
        ptr_.lock();
    }
    
    ~TaggedLockGuard() {
        ptr_.unlock();
    }
    
    // Disable copy and move
    TaggedLockGuard(const TaggedLockGuard&) = delete;
    TaggedLockGuard& operator=(const TaggedLockGuard&) = delete;
    
    // Access to underlying pointer
    T* get() const {
        return ptr_.get();
    }
    
    T* operator->() const {
        return ptr_.get();
    }
    
    T& operator*() const {
        return *ptr_.get();
    }
    
private:
    TaggedLockPtr<T>& ptr_;
};

} // namespace psync