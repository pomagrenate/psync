#pragma once

// psync_seqlock.h
// Sequence lock for read-mostly workloads
// Target: Linux x86-64 / Windows
// Compilers: GCC, Clang, MSVC

#include "psync_platform.h"

namespace psync {

// ============================================================================
// SEQLOCK STATE LAYOUT (32 bits)
// ============================================================================
// Bits 31-0: SEQUENCE_NUMBER
// Even sequence = stable snapshot (no writer active)
// Odd sequence = write in progress (writer active)
// ============================================================================

class SeqLock {
public:
    // Initialize seqlock (trivial initialization)
    constexpr SeqLock() : sequence_(0) {}
    
    // Disable copy and move
    SeqLock(const SeqLock&) = delete;
    SeqLock& operator=(const SeqLock&) = delete;
    
    // Begin a write operation
    // Returns the sequence number after increment
    // Caller must ensure no other writers are active (external serialization)
    u32 write_begin() {
        // Increment sequence (becomes odd)
        u32 seq = atomic_fetch_add(&sequence_, static_cast<u32>(1), MemoryOrder::acquire);
        
        // Ensure all previous writes are visible before we proceed
        compiler_fence();
        
        return seq + 1;
    }
    
    // End a write operation
    // Increments sequence (becomes even)
    void write_end() {
        // Ensure all writes are published before incrementing sequence
        compiler_fence();
        
        // Increment sequence (becomes even)
        atomic_fetch_add(&sequence_, static_cast<u32>(1), MemoryOrder::release);
    }
    
    // Begin a read operation
    // Returns the current sequence number
    // If sequence is odd, retry (write in progress)
    u32 read_begin() const {
        u32 seq;
        do {
            seq = atomic_load(&sequence_, MemoryOrder::acquire);
        } while ((seq & 1) != 0);  // Retry if odd
            
        return seq;
    }
    
    // Check if read snapshot is still valid
    // Returns true if sequence changed (snapshot invalid), false otherwise
    bool read_retry(u32 seq) const {
        u32 current = atomic_load(&sequence_, MemoryOrder::acquire);
        return current != seq;
    }
    
private:
    volatile u32 sequence_;
};

// ============================================================================
// SEQLOCK READ GUARD (RAII for readers)
// ============================================================================

class SeqLockReadGuard {
public:
    explicit SeqLockReadGuard(const SeqLock& lock) : lock_(lock) {
        do {
            sequence_ = lock_.read_begin();
        } while (lock_.read_retry(sequence_));
    }
    
    // Check if the read snapshot is still valid
    bool retry() const {
        return lock_.read_retry(sequence_);
    }
    
    // Disable copy and move
    SeqLockReadGuard(const SeqLockReadGuard&) = delete;
    SeqLockReadGuard& operator=(const SeqLockReadGuard&) = delete;
    
private:
    const SeqLock& lock_;
    u32 sequence_;
};

// ============================================================================
// SEQLOCK WRITE GUARD (RAII for writers)
// ============================================================================

class SeqLockWriteGuard {
public:
    explicit SeqLockWriteGuard(SeqLock& lock) : lock_(lock) {
        lock_.write_begin();
    }
    
    ~SeqLockWriteGuard() {
        lock_.write_end();
    }
    
    // Disable copy and move
    SeqLockWriteGuard(const SeqLockWriteGuard&) = delete;
    SeqLockWriteGuard& operator=(const SeqLockWriteGuard&) = delete;
    
private:
    SeqLock& lock_;
};

} // namespace psync
