// SelectThread scheduler, OSWakeupThread and the OSMutex primitives.

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <iostream>

#include "abi_bridge.h"
#include "memory.h"
#include "hle_stubs.h"
#include "ppc_runtime.h"
#include "fiber_manager.h"
#include "runtime_log.h"
#include "os_internal.h"

namespace {
void LinkMutexToThread(uint32_t threadPtr, uint32_t mutexPtr)
{
    const uint32_t tail = ::Memory::Read32(threadPtr + kThreadMutexTailOffset);
    if (tail == 0) {
        ::Memory::Write32(threadPtr + kThreadMutexQueueOffset, mutexPtr);
    } else {
        ::Memory::Write32(tail + kMutexThreadNextOffset, mutexPtr);
    }

    ::Memory::Write32(mutexPtr + kMutexThreadPrevOffset, tail);
    ::Memory::Write32(mutexPtr + kMutexThreadNextOffset, 0);
    ::Memory::Write32(threadPtr + kThreadMutexTailOffset, mutexPtr);
}

void UnlinkMutexFromThread(uint32_t threadPtr, uint32_t mutexPtr)
{
    UnlinkGuestListNode(mutexPtr, kMutexThreadNextOffset, kMutexThreadPrevOffset,
                        threadPtr + kThreadMutexQueueOffset, threadPtr + kThreadMutexTailOffset);
}

// Tail-enqueue `thread` on the run queue for `priority` and record which queue
// it is linked into. Marking the priority pending is a separate step because
// OSWakeupThread runs its fiber bookkeeping between the two.
void LinkThreadOnRunQueue(uint32_t thread, int32_t priority)
{
    const uint32_t queueEntry = kThreadQueueArrayAddr + static_cast<uint32_t>(priority) * 8u;
    const uint32_t tail = ::Memory::Read32(queueEntry + 4u);
    if (tail == 0) {
        ::Memory::Write32(queueEntry, thread);
    } else {
        ::Memory::Write32(tail + kThreadNextOffset, thread);
    }
    ::Memory::Write32(thread + kThreadPrevOffset, tail);
    ::Memory::Write32(thread + kThreadNextOffset, 0);
    ::Memory::Write32(queueEntry + 4u, thread);
    ::Memory::Write32(thread + kThreadQueueOffset, queueEntry);
}

// Publish `priority` in the scheduler pending mask and request a reschedule.
void MarkRunQueuePending(int32_t priority)
{
    const uint32_t pending = ::Memory::Read32(kSchedulerPendingFlagAddr);
    ::Memory::Write32(kSchedulerPendingFlagAddr, pending | (1u << (31 - priority)));
    ::Memory::Write32(kSchedulerReschedCounterAddr, 1);
}

// Pop `head` off the front of the thread queue whose head/tail slots are
// `queueAddr`/`queueAddr + 4`, returning the new head.
uint32_t PopThreadQueueHead(uint32_t queueAddr, uint32_t head)
{
    const uint32_t next = ::Memory::Read32(head + kThreadNextOffset);
    if (next == 0) {
        ::Memory::Write32(queueAddr + 4u, 0);
    } else {
        ::Memory::Write32(next + kThreadPrevOffset, 0);
    }
    ::Memory::Write32(queueAddr, next);
    return next;
}

void PromoteThreadPriority(uint32_t threadPtr, int32_t priority)
{
    while (threadPtr != 0) {
        if (static_cast<int32_t>(::Memory::Read32(threadPtr + kThreadSuspendOffset)) > 0) {
            return;
        }

        const int32_t currentPriority =
            static_cast<int32_t>(::Memory::Read32(threadPtr + kThreadPriorityOffset));
        if (currentPriority <= priority) {
            return;
        }

        threadPtr = SetThreadEffectivePriority(threadPtr, priority);
    }
}
} // namespace


// ---------------------------------------------------------------------------
// TEMPORARY: locate the first write that corrupts a live OSThread during the
// SelectThread -> context switch -> resume path. Watches the game thread's
// priority/state pair; priority legitimately holds its created value (16) until
// SetThreadEffectivePriority runs, so priority dropping to 0 is corruption.
// Reports the first checkpoint at which the struct has gone bad, once.
// ---------------------------------------------------------------------------
namespace OsSwitchDiag {
uint32_t g_watchThread = 0;      // set at OSCreateThread
uint32_t g_lastPrio = 0xFFFFFFFFu;
bool g_reported = false;

void Arm(uint32_t threadPtr)
{
    g_watchThread = threadPtr;
    g_reported = false;
    g_lastPrio = 0xFFFFFFFFu;
    RT_LOGF(RT_TAG_OS, "SWDIAG: armed on thread 0x%08X\n", threadPtr);
}

void Check(const char* tag)
{
    if (g_watchThread == 0 || g_reported) return;
    try {
        const uint32_t prio = ::Memory::Read32(g_watchThread + 0x2D0u);
        const uint16_t st = ::Memory::Read16(g_watchThread + 0x2C8u);
        if (prio != g_lastPrio) {
            RT_LOGF(RT_TAG_OS, "SWDIAG %s: prio=%u state=%u\n", tag, prio, (unsigned)st);
            g_lastPrio = prio;
        }
        if (prio == 0 && st == 0) {
            g_reported = true;
            RT_LOGF(RT_TAG_OS, "SWDIAG *** CORRUPT FIRST SEEN AT %s *** thread=0x%08X\n",
                    tag, g_watchThread);
        }
    } catch (const ::Memory::AccessViolation&) {
        g_reported = true;
        RT_LOGF(RT_TAG_OS, "SWDIAG *** UNREADABLE AT %s ***\n", tag);
    }
}
} // namespace OsSwitchDiag

extern "C" uint32_t g_nsmbwDiagThreadPtrs[16];
extern "C" uint32_t g_nsmbwDiagThreadCount;
extern "C" void OSWakeupThread_HLE_801aaaa4(CpuContext* ctx);
extern "C" void OSSleepThread_HLE_801aa9b8(CpuContext* ctx);

static void TryInvokeSwitchCallback(uint32_t oldCtx, uint32_t newCtx, CpuContext* cpu)
{
    try {
        const uint32_t callbackAddr = ::Memory::Read32(kSwitchThreadCallbackPtrAddr);
        if (callbackAddr == 0) {
            return;
        }

        if (TranslatedFunctionRegistry::FindByAddressPtr(callbackAddr)) {
            CpuContext* ctx = cpu ? cpu : &GetPersistentCpuContext();
            ctx->gpr[3] = oldCtx;
            ctx->gpr[4] = newCtx;
            InvokeIndirectCpu(callbackAddr, ctx);
            return;
        }

        RT_LOG(RT_TAG_OS) << "__OSThreadInit: switchThreadCallback 0x" << std::hex << callbackAddr
                  << std::dec << " not registered; skipping" << std::endl;
    } catch (const ::Memory::AccessViolation& e) {
        RT_LOG(RT_TAG_OS) << "__OSThreadInit: failed to read callback pointer (0x" << std::hex
                  << e.address() << std::dec << ")" << std::endl;
    }
}

// Forward declare for SelectThread to use
extern "C" void OS__SetCurrentContext_801a1e70(uint32_t contextAddr);
extern "C" void func_801A1ED8(CpuContext* ctx);

// ============================================================================
// SelectThread HLE - Thread Scheduler (Fiber-Aware)
// Address: 0x801B4FE0 (registered in projects/nsmbw/native/nsmbw_select_thread_override.cpp -
// see that file for why this override moved here from the wrong address, 0x801A9C08, and why
// its PPC_NATIVE_OVERRIDE_VOID lives outside this shared runtime tree). Picks the next runnable
// thread; switches via Windows Fibers instead of blocking InvokeIndirectJump.
// ============================================================================

extern "C" void SelectThread_801b4fe0(CpuContext* ctx)
{
    CpuContext* cpu = ctx ? ctx : &GetPersistentCpuContext();
    const uint32_t forceSwitch = cpu->gpr[3];

    ProcessSleepTimers(cpu);

    // Read scheduler state
    const uint32_t idleFlag = ::Memory::Read32(kSchedulerIdleFlagAddr);
    if (idleFlag >= 1) {
        // Scheduler is idle/disabled, return 0
        cpu->gpr[3] = 0;
        return;
    }

    // Get current context
    const uint32_t currentContext = ::Memory::Read32(kOSCurrentContextAddr);
    const uint32_t runningContext = ::Memory::Read32(kOSRunningContextAddr);
    
    // Early exit if no thread system is initialized yet
        // (both current and running contexts are 0)
        if (currentContext == 0 && runningContext == 0) {
            cpu->gpr[3] = 0;
            return;
        }

    // TEMPORARY diagnostic (NSMBW_LOG_IDLE_THREADS): when a thread arrives here already WAITING
    // (i.e. from OSSleepThread), print who put it to sleep, from the live guest stack.
    {
        static const bool dumpSleepers = std::getenv("NSMBW_LOG_IDLE_THREADS") != nullptr;
        if (dumpSleepers && runningContext != 0 && ::Memory::Read16(runningContext + 0x2C8u) == 4u) {
            static uint32_t sleepEvents = 0;
            ++sleepEvents;
            if (sleepEvents <= 12 || (sleepEvents % 300) == 0) {
                char bt[220] = {};
                try {
                    uint32_t sp = cpu->gpr[1];
                    size_t used = 0;
                    for (int d = 0; d < 9 && sp >= 0x80000000u && used + 12 < sizeof(bt); ++d) {
                        const uint32_t chain = ::Memory::Read32(sp);
                        if (chain < 0x80000000u) break;
                        used += static_cast<size_t>(std::snprintf(bt + used, sizeof(bt) - used, " %08X", ::Memory::Read32(chain + 4u)));
                        sp = chain;
                    }
                } catch (...) {}
                RT_LOGF(RT_TAG_OS, "[nsmbw][diag] sleep#%u thread 0x%08X queue=0x%08X lr=0x%08X bt:%s\n",
                        sleepEvents, runningContext, ::Memory::Read32(runningContext + 0x2DCu),
                        static_cast<uint32_t>(cpu->lr), bt);
            }
        }
    }
    if (currentContext != runningContext) {
        // Not in the running thread's context, return 0
        cpu->gpr[3] = 0;
        return;
    }

    // Check if we have a running thread
    if (runningContext != 0) {
        // Read thread state at offset 0x2C8 (state field)
        const uint16_t threadState = ::Memory::Read16(runningContext + 0x2C8u);
        
        if (threadState == 2) { // RUNNING state
            // Check if we need to reschedule
            const uint32_t reschedPending = ::Memory::Read32(kSchedulerPendingFlagAddr);
            
            if (forceSwitch == 0) {
                // Get thread priority from offset 0x2D0
                const int32_t threadPriority = static_cast<int32_t>(::Memory::Read32(runningContext + 0x2D0u));
                
                // countLeadingZeros equivalent - find highest priority pending
                uint32_t pendingClz = 32;
                if (reschedPending != 0) {
                    pendingClz = PPC_Cntlzw(reschedPending);
                }
                
                if (threadPriority <= static_cast<int32_t>(pendingClz)) {
                    // Current thread has higher or equal priority, don't switch
                    cpu->gpr[3] = 0;
                    return;
                }
            }
            
            // Mark thread as READY (state = 1)
            ::Memory::Write16(runningContext + 0x2C8u, 1);
            
            // Add running thread back to its priority queue
            const int32_t prio = static_cast<int32_t>(::Memory::Read32(runningContext + 0x2D0u));
            LinkThreadOnRunQueue(runningContext, prio);
            MarkRunQueuePending(prio);
        }

        // 0x801A1ED8 is NOT OSSaveContext (mid-function code ending in a spin on -0x5100(r13)).
        // It is gone for good: its bogus non-zero return was only masking the fiber-switch path
        // below by making SelectThread bail before reaching it. Fiber switching saves and restores
        // the host stack, so no guest context save belongs here.
        OsSwitchDiag::Check("A:after-requeue");
    }

    OsSwitchDiag::Check("B:pre-idle-check");
    // Check if there are any runnable threads
    uint32_t reschedPending = ::Memory::Read32(kSchedulerPendingFlagAddr);
    if (reschedPending == 0) {
        // No threads to run - enter idle loop
        // TEMPORARY diagnostic for the post-input-fix black-screen investigation: report
        // every time the scheduler goes idle (rare enough not to spam) so we can see what
        // was running right before it, and then confirm via the counter below whether this
        // specific idle period ever actually ends.
        RT_LOGF(RT_TAG_OS, "[nsmbw][diag] SelectThread: entering idle loop, runningContext=0x%08X\n",
                runningContext);
        // TEMPORARY diagnostic (NSMBW_LOG_IDLE_THREADS): state of every created thread at idle
        // entry. OSThread layout: state u16 @0x2C8 (1 READY 2 RUNNING 4 WAITING 8 MORIBUND),
        // suspend s32 @0x2CC, priority @0x2D0, sleep queue ptr @0x2DC, mutex @0x2F0;
        // OSContext: lr @0x84, srr0 @0x198.
        {
            static const bool dumpThreads = std::getenv("NSMBW_LOG_IDLE_THREADS") != nullptr;
            static uint32_t idleEntries = 0;
            ++idleEntries;
            if (dumpThreads && (idleEntries <= 3 || (idleEntries % 400) == 0)) {
                for (uint32_t i = 0; i < g_nsmbwDiagThreadCount; ++i) {
                    const uint32_t t = g_nsmbwDiagThreadPtrs[i];
                    uint32_t lr = 0, srr0 = 0, q = 0, mtx = 0, prio = 0, susp = 0; uint16_t st = 0;
                    try {
                        st = ::Memory::Read16(t + 0x2C8u); susp = ::Memory::Read32(t + 0x2CCu);
                        prio = ::Memory::Read32(t + 0x2D0u); q = ::Memory::Read32(t + 0x2DCu);
                        mtx = ::Memory::Read32(t + 0x2F0u); lr = ::Memory::Read32(t + 0x84u);
                        srr0 = ::Memory::Read32(t + 0x198u);
                    } catch (...) {}
                    RT_LOGF(RT_TAG_OS, "[nsmbw][diag] idle#%u thread 0x%08X state=%u suspend=%d prio=%u sleepQueue=0x%08X mutex=0x%08X lr=0x%08X srr0=0x%08X\n",
                            idleEntries, t, st, static_cast<int>(susp), prio, q, mtx, lr, srr0);
                    // Back-trace from the saved context: gpr[1] @+0x04 is the SP; each frame's
                    // back chain is at [sp], its saved LR at [chain+4].
                    char bt[200] = {};
                    try {
                        uint32_t sp = ::Memory::Read32(t + 0x4u);
                        size_t used = 0;
                        for (int d = 0; d < 8 && sp != 0 && used + 12 < sizeof(bt); ++d) {
                            const uint32_t chain = ::Memory::Read32(sp);
                            if (chain == 0 || chain < 0x80000000u) break;
                            const uint32_t ret = ::Memory::Read32(chain + 4u);
                            used += static_cast<size_t>(std::snprintf(bt + used, sizeof(bt) - used, " %08X", ret));
                            sp = chain;
                        }
                    } catch (...) {}
                    RT_LOGF(RT_TAG_OS, "[nsmbw][diag]   backtrace:%s\n", bt);
                }
            }
        }
        OsSwitchDiag::Check("C:pre-idle-switchcb");
        TryInvokeSwitchCallback(runningContext, 0, cpu);
        OsSwitchDiag::Check("D:post-idle-switchcb");
        ::Memory::Write32(kOSRunningContextAddr, 0);

        // Set current context to idle thread context
        OS__SetCurrentContext_801a1e70(kIdleThreadContextAddr);
        OsSwitchDiag::Check("E:post-setcurctx-idle");

        uint64_t diagIdleSpins = 0;
        while (true) {
            // Enable interrupts and idle until something becomes runnable.
            OS__EnableInterrupts_801a65c0();

            while (::Memory::Read32(kSchedulerPendingFlagAddr) == 0) {
                if ((++diagIdleSpins % 2000000) == 0) {
                    RT_LOGF(RT_TAG_OS, "[nsmbw][diag] SelectThread: still idle after %llu spins\n",
                            static_cast<unsigned long long>(diagIdleSpins));
                }
                ProcessSleepTimers(cpu);
                // Dolphin models DSP audio DMA as an independent 4 kHz timing
                // event.  Poll it from the guest scheduler instead of batching
                // completed 3 ms DMA blocks at VI retrace cadence.  A completed
                // block wakes SoundThread and sets the scheduler pending mask.
                Audio_HLE_Poll(cpu);
                // No early break here. When the guest runs slower than real time the audio
                // pump has a completed block on every pass, so breaking as soon as it wakes
                // SoundThread meant VI_HLE_PollRetrace below never ran: the main thread, asleep
                // in EGG::AsyncDisplay::beginFrame on the display queue (0x807602F4) that only
                // the VI retrace callback wakes, then stayed WAITING forever while SoundThread
                // ran, slept, and was re-woken in a loop (NSMBW_LOG_IDLE_THREADS, 2026-09-20,
                // first seen the moment the AI DMA callback was bound for NSMBW). Retrace,
                // fiber timers and alarms all get their pass before the pending check.
                VI_HLE_PollRetrace(cpu);
                if (Fiber::GuestFiberManager::IsInitialized()) {
                    Fiber::GuestFiberManager::ProcessTimerEvents(cpu);
                }
                ProcessAlarmQueue(cpu, 8);
                if (::Memory::Read32(kSchedulerPendingFlagAddr) != 0) {
                    break;
                }
                VI_HLE_WaitForNextRetracePoll();
            }

            OS__DisableInterrupts_801a65ac();
            reschedPending = ::Memory::Read32(kSchedulerPendingFlagAddr);
            if (reschedPending != 0) {
                break;
            }
        }

        OsSwitchDiag::Check("F:pre-clearctx-idle");
        OS__ClearContext_801a2098(kIdleThreadContextAddr);
        OsSwitchDiag::Check("G:post-clearctx-idle");
    }

    // Clear reschedule counter
    ::Memory::Write32(kSchedulerReschedCounterAddr, 0);

    // Note: When the current thread is already in WAITING state (not RUNNING),
    // we skip the preemptibility check entirely, matching the original OS behavior.
    // The preemptibility check only applies when the thread is RUNNING and we're
    // deciding whether to preempt it.

    // Find highest priority thread
    uint32_t pendingMask = ::Memory::Read32(kSchedulerPendingFlagAddr);
    
    if (pendingMask == 0) {
        // No runnable threads in the run queue at this point
        cpu->gpr[3] = 0;
        return;
    }
    
    uint32_t priorityLevel = 0;
    uint32_t queueEntry = 0;
    uint32_t nextThread = 0;
    while (true) {
        priorityLevel = PPC_Cntlzw(pendingMask);
        queueEntry = kThreadQueueArrayAddr + (priorityLevel * 8);
        nextThread = ::Memory::Read32(queueEntry);

        if (nextThread == 0) {
            RT_LOG(RT_TAG_OS) << "SelectThread: ERROR - pending bit set but no thread in queue!" << std::endl;
            const uint32_t mask = ~(1u << (31 - priorityLevel));
            ::Memory::Write32(kSchedulerPendingFlagAddr, pendingMask & mask);
            pendingMask = ::Memory::Read32(kSchedulerPendingFlagAddr);
            if (pendingMask == 0) {
                cpu->gpr[3] = 0;
                return;
            }
            continue;
        }

        const uint16_t state = ::Memory::Read16(nextThread + kThreadStateOffset);
        if (ThreadStateIsTerminated(state) || Fiber::GuestFiberManager::IsTerminated(nextThread)) {
            RemoveThreadFromQueue(nextThread);
            pendingMask = ::Memory::Read32(kSchedulerPendingFlagAddr);
            if (pendingMask == 0) {
                cpu->gpr[3] = 0;
                return;
            }
            continue;
        }

        break;
    }

    // Remove thread from queue head
    const uint32_t threadNext = PopThreadQueueHead(queueEntry, nextThread);

    // If queue is now empty, clear the pending bit
    if (threadNext == 0) {
        const uint32_t mask = ~(1u << (31 - priorityLevel));
        ::Memory::Write32(kSchedulerPendingFlagAddr, pendingMask & mask);
    }

    OsSwitchDiag::Check("H:picked-thread");
    // Clear thread's queue pointer
    ::Memory::Write32(nextThread + 0x2DCu, 0);
    
    // Set thread to RUNNING state
    ::Memory::Write16(nextThread + 0x2C8u, 2);

    OsSwitchDiag::Check("I:pre-switchcb");
    // Invoke switch callback
    TryInvokeSwitchCallback(runningContext, nextThread, cpu);
    OsSwitchDiag::Check("J:post-switchcb");

    // Update running context
    ::Memory::Write32(kOSRunningContextAddr, nextThread);
    
    // Set as current context
    OS__SetCurrentContext_801a1e70(nextThread);
    OsSwitchDiag::Check("K:post-setcurctx");

    // Use fiber-based context switch if available
    if (Fiber::GuestFiberManager::IsInitialized()) {
        uint32_t currentGuestThread = Fiber::GuestFiberManager::GetCurrentGuestThread();
        
        // If we don't have a current guest fiber but we DO have a running context,
        // register the running context as the "main thread" fiber.
        // This allows the main thread to participate in fiber switching.
        if (currentGuestThread == 0 && runningContext != 0) {
            if (!Fiber::GuestFiberManager::HasFiber(runningContext)) {
                OsSwitchDiag::Check("L:pre-register-main-fiber");
                Fiber::GuestFiberManager::RegisterMainThreadAsFiber(runningContext, cpu);
                OsSwitchDiag::Check("M:post-register-main-fiber");
            }
            // Whether we registered it or it already existed, use it as the current thread
            currentGuestThread = runningContext;
        }
        
        // Check if target thread has a fiber
        if (!Fiber::GuestFiberManager::HasFiber(nextThread)) {
            // No fiber for target thread - this can happen if the thread was created
            // by translated code that didn't go through our HLE. Just continue.
            cpu->gpr[3] = nextThread;
            OS__LoadContext_801a1f58(cpu);
            return;
        }
        
        // If we still don't have a current guest thread (no running context),
        // we can't do a proper fiber switch. Just return and let the caller handle it.
        if (currentGuestThread == 0) {
            OsSwitchDiag::Check("N:pre-switch-nocur");
            Fiber::GuestFiberManager::SwitchToThread(nextThread, cpu);
            OsSwitchDiag::Check("O:post-switch-nocur");
            return;
        }
        
        // Perform the fiber switch!
        OsSwitchDiag::Check("P:pre-switch");
        Fiber::GuestFiberManager::SwitchToThread(nextThread, cpu);
        OsSwitchDiag::Check("Q:post-switch");
        // When we return here, we've been switched back
        return;
    }
    
    // Fallback to blocking InvokeIndirectJump (original behavior)
    cpu->gpr[3] = nextThread;
    OS__LoadContext_801a1f58(cpu);
}

// (registration moved to projects/nsmbw/native/nsmbw_select_thread_override.cpp)

// ============================================================================
// OSWakeupThread HLE - drain a thread queue and mark threads runnable
// Address: 0x801AAAA4
// ============================================================================
static void WakeupThreadQueue(CpuContext* ctx, bool allowImmediateReschedule)
{
    CpuContext* cpu = ctx ? ctx : &GetPersistentCpuContext();
    const uint32_t queueAddr = cpu->gpr[3];
    if (queueAddr == 0) {
        return;
    }

    const int32_t irqState = OS__DisableInterrupts_801a65ac();
    bool resched = false;
    constexpr int kMaxWake = 256;
    int woke = 0;

    while (woke < kMaxWake) {
        const uint32_t thread = ::Memory::Read32(queueAddr);
        if (thread == 0) {
            break;
        }

        PopThreadQueueHead(queueAddr, thread);

        const uint16_t state = ::Memory::Read16(thread + kThreadStateOffset);
        if (ThreadStateIsTerminated(state) || Fiber::GuestFiberManager::IsTerminated(thread)) {
            ::Memory::Write32(thread + kThreadQueueOffset, 0);
            ::Memory::Write32(thread + kThreadNextOffset, 0);
            ::Memory::Write32(thread + kThreadPrevOffset, 0);
            woke++;
            continue;
        }

        // Mark READY
        ::Memory::Write16(thread + 0x2C8u, 1);

        const int32_t suspend = static_cast<int32_t>(::Memory::Read32(thread + 0x2CCu));
        if (suspend < 1) {
            int32_t prio = static_cast<int32_t>(::Memory::Read32(thread + 0x2D0u));
            if (prio < 0) prio = 0;
            if (prio > 31) prio = 31;
            LinkThreadOnRunQueue(thread, prio);

            if (Fiber::GuestFiberManager::IsInitialized()) {
                if (thread == kDefaultThreadContextAddr && !Fiber::GuestFiberManager::HasFiber(thread)) {
                    Fiber::GuestFiberManager::RegisterMainThreadAsFiber(thread, cpu);
                }
                Fiber::GuestFiberManager::ResumeGuestThread(thread);
            }

            MarkRunQueuePending(prio);
            resched = true;
        }

        woke++;
    }

    if (woke >= kMaxWake) {
        RT_LOG(RT_TAG_OS) << "OSWakeupThread: hit safety limit, possible queue cycle at 0x"
                  << std::hex << queueAddr << std::dec << std::endl;
    }

    if (allowImmediateReschedule && resched && !VI_HLE_IsAdvancingRetrace()) {
        cpu->gpr[3] = 0;
        SelectThread_801b4fe0(cpu);
    }

    OS__RestoreInterrupts_801a65d4(irqState);
}

extern "C" void OSWakeupThread_HLE_801aaaa4(CpuContext* ctx)
{
    WakeupThreadQueue(ctx, true);
}

void OS_HLE_WakeupThreadNoReschedule(CpuContext* ctx, uint32_t waitQueue)
{
    CpuContext* cpu = ctx ? ctx : &GetPersistentCpuContext();
    const uint32_t savedR3 = cpu->gpr[3];
    cpu->gpr[3] = waitQueue;
    WakeupThreadQueue(cpu, false);
    cpu->gpr[3] = savedR3;
}

PPC_NATIVE_OVERRIDE_VOID(801AAAA4, OSWakeupThread_HLE_801aaaa4, (CpuContext* ctx), (ctx));

extern "C" void OSSleepThread_HLE_801aa9b8(CpuContext* ctx);

extern "C" void OSLockMutex_HLE_801a7ee4(CpuContext* ctx)
{
    CpuContext* cpu = ctx ? ctx : &GetPersistentCpuContext();
    const uint32_t mutexPtr = cpu->gpr[3];
    if (mutexPtr == 0) {
        return;
    }

    const int32_t irqState = OS__DisableInterrupts_801a65ac();

    try {
        const uint32_t currentThread = ::Memory::Read32(kOSRunningContextAddr);
        if (currentThread == 0) {
            OS__RestoreInterrupts_801a65d4(irqState);
            return;
        }

        while (true) {
            const uint32_t ownerThread = ::Memory::Read32(mutexPtr + kMutexOwnerOffset);
            if (ownerThread == 0) {
                ::Memory::Write32(mutexPtr + kMutexOwnerOffset, currentThread);
                const uint32_t count = ::Memory::Read32(mutexPtr + kMutexCountOffset);
                ::Memory::Write32(mutexPtr + kMutexCountOffset, count + 1u);
                LinkMutexToThread(currentThread, mutexPtr);
                break;
            }

            if (ownerThread == currentThread) {
                const uint32_t count = ::Memory::Read32(mutexPtr + kMutexCountOffset);
                ::Memory::Write32(mutexPtr + kMutexCountOffset, count + 1u);
                break;
            }

            ::Memory::Write32(currentThread + kThreadMutexOffset, mutexPtr);
            const int32_t priority =
                static_cast<int32_t>(::Memory::Read32(currentThread + kThreadPriorityOffset));
            PromoteThreadPriority(ownerThread, priority);

            cpu->gpr[3] = mutexPtr;
            OSSleepThread_HLE_801aa9b8(cpu);
            ::Memory::Write32(currentThread + kThreadMutexOffset, 0);
        }
    } catch (const ::Memory::AccessViolation& e) {
        LogMemoryError(RT_TAG_OS, "OSLockMutex", e);
    }

    OS__RestoreInterrupts_801a65d4(irqState);
}
PPC_NATIVE_OVERRIDE_VOID(801A7EE4, OSLockMutex_HLE_801a7ee4, (CpuContext* ctx), (ctx));

extern "C" void OSUnlockMutex_HLE_801a7fc0(CpuContext* ctx)
{
    CpuContext* cpu = ctx ? ctx : &GetPersistentCpuContext();
    const uint32_t mutexPtr = cpu->gpr[3];
    if (mutexPtr == 0) {
        return;
    }

    const int32_t irqState = OS__DisableInterrupts_801a65ac();

    try {
        const uint32_t currentThread = ::Memory::Read32(kOSRunningContextAddr);
        if (currentThread == 0) {
            OS__RestoreInterrupts_801a65d4(irqState);
            return;
        }

        if (::Memory::Read32(mutexPtr + kMutexOwnerOffset) == currentThread) {
            const uint32_t count = ::Memory::Read32(mutexPtr + kMutexCountOffset) - 1u;
            ::Memory::Write32(mutexPtr + kMutexCountOffset, count);
            if (count == 0) {
                UnlinkMutexFromThread(currentThread, mutexPtr);
                ::Memory::Write32(mutexPtr + kMutexOwnerOffset, 0);

                const int32_t effectivePriority =
                    static_cast<int32_t>(::Memory::Read32(currentThread + kThreadPriorityOffset));
                const int32_t basePriority =
                    static_cast<int32_t>(::Memory::Read32(currentThread + kThreadBasePriorityOffset));
                if (effectivePriority < basePriority) {
                    const int32_t recomputedPriority = ComputeThreadEffectivePriority(currentThread);
                    ::Memory::Write32(currentThread + kThreadPriorityOffset,
                                      static_cast<uint32_t>(recomputedPriority));
                }

                cpu->gpr[3] = mutexPtr;
                OSWakeupThread_HLE_801aaaa4(cpu);
            }
        }
    } catch (const ::Memory::AccessViolation& e) {
        LogMemoryError(RT_TAG_OS, "OSUnlockMutex", e);
    }

    OS__RestoreInterrupts_801a65d4(irqState);
}
PPC_NATIVE_OVERRIDE_VOID(801A7FC0, OSUnlockMutex_HLE_801a7fc0, (CpuContext* ctx), (ctx));

