/*
 * Copyright 2026 Aurora Silicon
 * SPDX-License-Identifier: MIT
 *
 * Lock acquisition that does not depend on a native waiter wakeup.
 *
 * On this Windows-on-Apple-Silicon ARM64 target a lost wake was captured
 * directly while bringing up the D3D11 stack: a submission producer sat
 * indefinitely in NtWaitForAlertByThreadId acquiring an SRW lock whose live
 * lock word was already zero, and other workers on the same process showed
 * the identical free-lock/waiter contradiction.  A UMD that loses a wake
 * this way stops feeding the GPU before dxgkrnl or the KMD is ever called,
 * which presents as a hang with no submission to attribute it to.
 *
 * Keep the Win32 synchronization ABI -- these remain ordinary SRW locks with
 * the same ownership rules -- but acquire them with a bounded try/yield loop
 * so forward progress is independent of a native wakeup.  An uncontended
 * acquisition still takes the single TryAcquire fast path, so the healthy
 * cost is one interlocked operation, exactly as before.
 */

#pragma once

#include "d12_umd_wddm.h"
#include <windows.h>

/* Yield to any runnable thread first; only fall back to a timed sleep when
 * the scheduler reports nothing else to run, which is when a pure spin would
 * otherwise burn the core the lock holder may need. */
static inline void
d12_umd_sync_backoff(void)
{
   if (!SwitchToThread())
      Sleep(1);
}

static inline void
d12_umd_sync_acquire_exclusive(SRWLOCK *lock)
{
   while (!TryAcquireSRWLockExclusive(lock))
      d12_umd_sync_backoff();
}

static inline void
d12_umd_sync_acquire_shared(SRWLOCK *lock)
{
   while (!TryAcquireSRWLockShared(lock))
      d12_umd_sync_backoff();
}
