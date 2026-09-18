#pragma once

// psync.h
// Umbrella header for the psync zero-dependency synchronization library.
// Provides native futex-backed Mutex, SharedMutex, ConditionVariable,
// generic RAII locks (LockGuard, UniqueLock, SharedLock), OnceFlag, call_once,
// MicroLock, SeqLock, Batch, and TaggedPtr.

#include "psync_platform.h"
#include "psync_locks.h"
#include "psync_mutex.h"
#include "psync_shared.h"
#include "psync_condvar.h"
#include "psync_once.h"
#include "psync_microlock.h"
#include "psync_seqlock.h"
#include "psync_batch.h"
#include "psync_tagged_ptr.h"
