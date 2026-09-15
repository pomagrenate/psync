# PSYNC DESIGN DOCUMENT

## Overview

This document describes the design of the psync synchronization library, including state machines, memory ordering, futex protocols, and correctness arguments.

---

## 1. Mutex State Machine (4-byte Adaptive Mutex)

### State Representation (32 bits)

```
Bits 31-16: Reserved (future use)
Bits 15-1:  WAITER_COUNT (number of parked waiters)
Bit 0:      LOCKED flag (1 = locked, 0 = unlocked)
```

### States

- `UNLOCKED = 0x00000000`: Lock is free, no waiters
- `LOCKED_NO_WAITERS = 0x00000001`: Lock is held, no waiters
- `LOCKED_WITH_WAITERS = 0x00000001 | (waiter_count << 1)`: Lock is held with waiters

### Operations

#### lock()

**Fast path (uncontended):**
```
CAS(state, UNLOCKED, LOCKED_NO_WAITERS, relaxed, acquire)
- If success: return (lock acquired)
- If failure: proceed to slow path
```

**Slow path (contended):**
```
1. Adaptive spin loop:
   - Try CAS from UNLOCKED to LOCKED_NO_WAITERS
   - Insert pause instructions
   - Exponential backoff

2. If still failed after spinning:
   - CAS to increment WAITER_COUNT atomically
   - futex_wait(state, expected_value)
   - On wake, retry from step 1
```

**Memory ordering:**
- CAS uses relaxed for expected value (we're only interested in success/failure)
- CAS uses acquire on success (establishes happens-before with previous unlock)
- Waiter count operations use relaxed (only hints for wake strategy)

#### unlock()

**Fast path (no waiters):**
```
CAS(state, LOCKED_NO_WAITERS, UNLOCKED, release, relaxed)
- If success: return (no waiters to wake)
- If failure: proceed to slow path
```

**Slow path (waiters present):**
```
1. Atomically decrement WAITER_COUNT
2. Clear LOCKED flag
3. futex_wake(state, 1)  // wake one waiter
```

**Memory ordering:**
- CAS uses release on success (publishes critical section writes)
- State modifications before futex_wake ensure wakee sees correct state

### Correctness Arguments

**Mutual exclusion:**
- Only one thread can successfully CAS from UNLOCKED to LOCKED_NO_WAITERS
- LOCKED flag prevents other threads from entering critical section

**No lost wakeups:**
- Waiter sets WAITER flag BEFORE calling futex_wait
- Unlocker checks WAITER flag AFTER clearing LOCKED
- If waiter sets flag before unlocker clears LOCKED, unlocker will wake
- If unlocker clears LOCKED before waiter sets flag, waiter's CAS will fail and retry
- Waiter always re-checks state after futex_wait returns (handles spurious wakeups)

**No deadlock:**
- Uncontended path never blocks
- Contended path eventually yields via futex_wait
- Unlock always wakes waiters if present

---

## 2. MicroLock State Machine (1-byte)

### State Representation (8 bits)

```
Bits 7-1: Reserved (future use)
Bit 0:    LOCKED flag (1 = locked, 0 = unlocked)
```

### States

- `UNLOCKED = 0x00`: Lock is free
- `LOCKED = 0x01`: Lock is held

### Operations

#### lock()

**Fast path:**
```
CAS(state, UNLOCKED, LOCKED, relaxed, acquire)
- If success: return
- If failure: proceed to parking lot
```

**Slow path (via parking lot):**
```
1. Register waiter in parking lot using lock address
2. Adaptive spin
3. futex_wait on parking lot state
4. On wake, retry fast path
```

#### unlock()

**Fast path:**
```
CAS(state, LOCKED, UNLOCKED, release, relaxed)
- If success: return
- If parking lot has waiters: wake one via parking lot
```

### Parking Lot Architecture

**Data structure:**
- Global hash table with fixed number of buckets
- Each bucket contains a lock-protected waiter queue
- Waiter nodes: thread ID, wake flag, next pointer (intrusive list)

**Hash function:**
- `hash(lock_address) = (lock_address >> 3) % NUM_BUCKETS`
- Shift by 3 to align with cache line boundaries (reduces false sharing)

**Operations:**
- `park(lock_address)`: Hash to bucket, lock bucket, add waiter to queue, unlock bucket, futex_wait
- `unpark_one(lock_address)`: Hash to bucket, lock bucket, remove one waiter, unlock bucket, futex_wake
- `unpark_all(lock_address)`: Hash to bucket, lock bucket, remove all waiters, unlock bucket, futex_wake all

**Lifetime safety:**
- Waiter nodes allocated on stack (no dynamic allocation)
- Bucket locks use internal spinlocks
- Parking lot is global static (lives forever)

### Correctness Arguments

**Mutual exclusion:**
- Same as mutex: only one CAS from UNLOCKED to LOCKED can succeed

**No lost wakeups:**
- Waiter registers in parking lot BEFORE futex_wait
- Unlocker checks parking lot AFTER clearing LOCKED
- Registration and wake are serialized by bucket lock
- Waiter re-checks lock state after wake

**ABA prevention:**
- Lock address is stable (not reallocated during lock lifetime)
- Parking lot uses lock address as key, not lock state value

---

## 3. Shared Mutex State Machine (4-byte)

### State Representation (32 bits)

```
Bits 31-17: PENDING_WRITERS (optional for writer fairness)
Bits 16-1:  READER_COUNT (max 65535 readers)
Bit 0:      WRITER_LOCKED flag (1 = writer holds lock, 0 = no writer)
```

### States

- `UNLOCKED = 0x00000000`: No readers, no writer
- `READER_LOCKED(n) = (n << 1)`: n readers holding lock (n >= 1)
- `WRITER_LOCKED = 0x00000001`: Writer holding lock
- `WRITER_PENDING = 0x00000001 | (pending_writers << 17)`: Writer waiting

### Operations

#### lock_shared()

```
1. Load current state (acquire)
2. If WRITER_LOCKED or WRITER_PENDING:
   - Wait via futex or adaptive spin
   - Retry from step 1
3. CAS to increment READER_COUNT (acquire)
   - If CAS fails (state changed), retry from step 1
4. Return (lock acquired as reader)
```

#### unlock_shared()

```
1. fetch_sub READER_COUNT by 1 (release)
2. If READER_COUNT becomes 0 and PENDING_WRITERS > 0:
   - Wake one writer via futex
```

#### lock()

```
1. Load current state (acquire)
2. If WRITER_LOCKED or READER_COUNT > 0:
   - Set PENDING_WRITERS flag (CAS)
   - Wait via futex or adaptive spin
   - Retry from step 1
3. CAS to set WRITER_LOCKED flag (acquire)
   - If CAS fails, retry from step 1
4. Return (lock acquired as writer)
```

#### unlock()

```
1. Clear WRITER_LOCKED flag (release)
2. If PENDING_WRITERS > 0:
   - Wake one writer via futex
3. Else if READER_COUNT > 0:
   - Wake all readers via futex
```

### Memory Ordering

- Reader lock: acquire (ensures writer's writes are visible)
- Reader unlock: release (publishes reader's writes)
- Writer lock: acquire (ensures all previous writes are visible)
- Writer unlock: release (publishes writer's writes)
- Reader count: acquire/release for synchronization

### Correctness Arguments

**Reader-writer exclusion:**
- Writer cannot acquire lock if READER_COUNT > 0 (CAS will fail)
- Readers cannot acquire lock if WRITER_LOCKED (check prevents this)
- Only one writer at a time (WRITER_LOCKED flag is mutual exclusion)

**Writer starvation prevention:**
- PENDING_WRITERS flag signals writer intent
- New readers check PENDING_WRITERS before acquiring
- Wakes prefer writers when present

**Reader overflow protection:**
- Max 65535 readers (16 bits)
- If READER_COUNT overflows, lock breaks (documented limitation)

**Upgrade/downgrade:**
- Not implemented (documented limitation)
- Would require complex state transitions

---

## 4. SeqLock State Machine

### State Representation (32 bits)

```
Bits 31-0: SEQUENCE_NUMBER
```

### States

- `EVEN = stable snapshot` (no writer active)
- `ODD = write in progress` (writer active)

### Operations

#### write_begin()

```
1. fetch_add SEQUENCE by 1 (acquire)
   - Ensures sequence becomes odd
2. Return (writer active)
```

#### write_end()

```
1. fetch_add SEQUENCE by 1 (release)
   - Ensures sequence becomes even
2. Memory fence to publish writes
```

#### read_begin()

```
1. Load SEQUENCE (acquire)
2. If odd: retry (write in progress)
3. Return sequence value
```

#### read_retry(sequence)

```
1. Load SEQUENCE (acquire)
2. If current != sequence: return true (sequence changed)
3. Return false (stable snapshot)
```

### Correctness Arguments

**Reader lock-freedom:**
- Readers never block on writers
- Readers only retry if sequence changes
- No atomic operations other than loads

**Writer exclusion:**
- Only one writer at a time (caller must ensure this)
- SeqLock does NOT serialize writers (documented limitation)

**No invalid snapshot acceptance:**
- If sequence changes between read_begin and read_retry, reader detects it
- Odd sequence indicates write in progress (snapshot invalid)
- Even sequence indicates stable snapshot

**Protected data requirements:**
- Protected data must be naturally aligned
- Protected data must support atomic loads/stores
- Complex structures require careful access patterns
- Writers must not race with each other (external serialization required)

---

## 5. Batch Synchronization

### Architecture

BatchGuard is an RAII wrapper that amortizes synchronization overhead:

```cpp
{
    BatchGuard<Mutex> guard(mutex);
    // Multiple operations here
} // Auto-unlock
```

### Benefits

- Single lock/unlock for multiple operations
- Reduces cache coherency traffic
- Minimizes futex wake/sleep cycles
- Amortizes adaptive spin overhead

### API

```cpp
template<typename Lock>
class BatchGuard {
public:
    explicit BatchGuard(Lock& lock);
    ~BatchGuard();
    
    // No copy, no move
    BatchGuard(const BatchGuard&) = delete;
    BatchGuard& operator=(const BatchGuard&) = delete;
};
```

### Correctness

- RAII ensures unlock even on exception
- Nested batches are not allowed (compile-time error or runtime check)
- Maximum batch duration must be bounded by user (avoid holding lock forever)
- Fairness: batch guard holds lock for entire batch (may increase contention)

---

## 6. Futex Protocol

### Futex API

```cpp
futex_wait(u32* addr, u32 expected_value)
- Block if *addr == expected_value
- Return immediately if *addr != expected_value
- Spurious wakeups possible

futex_wake(u32* addr, int count)
- Wake up to count waiters on addr
- count = -1 wakes all
```

### Lost-Wakeup Prevention

**Critical invariant:**
- Waiter must set waiter flag BEFORE calling futex_wait
- Unlocker must check waiter flag AFTER clearing lock state
- State changes and futex operations must be atomic with respect to each other

**Race analysis:**

```
Thread A (waiter):          Thread B (unlocker):
load state -> LOCKED       
                            CAS state -> UNLOCKED
                            check waiter flag -> none
                            return (no wake)
set waiter flag            
futex_wait(addr, LOCKED)    // Lost wakeup!
```

**Solution:**
- Waiter uses CAS to atomically set waiter flag AND check lock state
- If CAS fails (state changed), retry instead of sleeping
- If CAS succeeds, then call futex_wait

**Corrected protocol:**

```
Thread A (waiter):
CAS(state, LOCKED, LOCKED_WITH_WAITER, relaxed, acquire)
- If success: futex_wait(state, LOCKED_WITH_WAITER)
- If failure: retry (state changed, don't sleep)

Thread B (unlocker):
CAS(state, LOCKED_WITH_WAITER, UNLOCKED, release, relaxed)
- If success: futex_wake(state, 1)
```

**Spurious wakeup handling:**
- Always re-check lock state after futex_wait returns
- If lock is still locked, retry wait
- If lock is unlocked, proceed to acquire

---

## 7. Memory Ordering Summary

### Acquire Ordering

Used when:
- Loading data written by another thread
- Entering a critical section
- Observing a publication

Applies to:
- Mutex lock success
- Reader lock
- Writer lock
- SeqLock read_begin/read_retry

### Release Ordering

Used when:
- Publishing data to other threads
- Exiting a critical section
- Releasing ownership

Applies to:
- Mutex unlock
- Reader unlock
- Writer unlock
- SeqLock write_end

### Relaxed Ordering

Used when:
- Operations that don't need synchronization
- Counters that are only hints
- Operations where atomicity is sufficient

Applies to:
- Waiter count increments/decrements
- Retry attempts
- Failed CAS operations

### Sequential Consistency

**NOT USED on hot paths.**
Only used if absolutely required for correctness (documented if used).

---

## 8. Adaptive Spinning

### Strategy

```
1. Immediate CAS attempt (no spin)
2. If failed, short spin with pause (10-100 iterations)
3. If still failed, exponential backoff
4. If still failed after threshold, futex_wait
```

### Backoff Parameters

- Initial spin: 10 iterations
- Maximum spin: 100 iterations
- Backoff multiplier: 2x
- Maximum backoff: 1000 iterations
- Futex threshold: after 3 failed spin attempts

### Platform Considerations

- `pause` instruction on x86-64 (reduces power consumption, improves hyperthreading)
- Consider CPU count (don't spin indefinitely on single-core)
- Consider oversubscription (back off aggressively if many threads)

---

## 9. Cache Line Behavior

### Lock Object Size

- Mutex: 4 bytes (compact)
- MicroLock: 1 byte (very compact)
- Shared mutex: 4 bytes (compact)
- SeqLock: 4 bytes (compact)

### Placement Considerations

- Frequently contended locks should be on separate cache lines
- Use `alignas(64)` for locks with high contention
- Don't pad everything (memory cost)
- Document placement recommendations in API

### False Sharing Prevention

- Parking lot buckets aligned to cache lines
- Waiter nodes in parking lot padded
- Global static structures aligned

---

## 10. Initialization and Destruction

### Initialization

- All primitives are trivially initialized (zero initialization)
- No dynamic initialization required
- Safe to use in static storage

### Destruction

- Must not destroy while locked (UB)
- Must not destroy while waiters present (UB)
- No dynamic cleanup required (no malloc/free)
- Parking lot is global static (lives forever)

### Lifetime Semantics

- Lock objects must outlive all operations
- Caller-owned storage (no internal allocation)
- Static storage safe
- Stack storage safe

---

## 11. Error Handling

- No exceptions
- No RTTI
- No iostreams
- No logging frameworks
- Test failures use minimal assertion mechanism
- Return codes or assertions for critical errors

---

## 12. Limitations

### Documented Limitations

- **Reader overflow:** Shared mutex supports max 65535 readers
- **Writer serialization:** SeqLock does NOT serialize writers (caller must ensure)
- **No upgrade/downgrade:** Shared mutex does not support lock upgrade/downgrade
- **Architecture:** Linux x86-64 only (ARM64 not implemented)
- **Compiler:** GCC/Clang only (MSVC not tested)
- **CPU assumptions:** Assumes x86-64 memory model and TSO
- **Kernel:** Requires Linux futex support

### Known Performance Trade-offs

- Compact lock size may increase cache contention
- Adaptive spinning may waste CPU on low-core machines
- Parking lot hash collisions may cause contention
- Batch synchronization may increase starvation risk

---

## 13. Test Strategy

### Basic Tests

- Single-thread lock/unlock
- try_lock functionality
- Repeated acquisition

### Concurrency Tests

- Mutual exclusion (counter increment)
- Contention stress
- Shared mutex reader/writer exclusion
- SeqLock sequence validation
- MicroLock high contention
- Lost wakeup stress
- Starvation detection

### Failure Mode Tests

- Double ownership detection
- Premature unlock detection
- Missed release detection
- Stale read detection
- Incorrect memory ordering
- ABA-like state corruption
- Waiter corruption
- Deadlock detection
- Livelock detection

### Sanitizers

- ThreadSanitizer (data races)
- AddressSanitizer (memory errors)
- UndefinedBehaviorSanitizer (UB detection)

---

## 14. Benchmark Strategy

### Comparisons

- psync mutex vs std::mutex
- Uncontended latency
- 2-thread contention
- Multi-thread contention
- Different critical section durations
- Batch size variations (1, 4, 16, 64, 256)

### Metrics

- Operations per second
- Average latency
- p50, p95, p99 percentiles
- CPU utilization

### Honesty

- Report all results, favorable or not
- Never cherry-pick data
- Document benchmark conditions
- "UNMEASURED" for unverified claims

---

## 15. Implementation Order

1. **psync_platform.h**: Futex, atomic primitives, CPU intrinsics
2. **psync_mutex.h**: 4-byte adaptive mutex
3. **psync_microlock.h**: 1-byte microlock with parking lot
4. **psync_seqlock.h**: Sequence lock
5. **psync_shared.h**: Shared mutex
6. **psync_batch.h**: Batch synchronization
7. **test_main.cc**: Comprehensive test suite

Each primitive implemented and tested before moving to the next.
