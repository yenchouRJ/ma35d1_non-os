# MA35D1 FreeRTOS-SMP Development Memo

## SMP Boot Flow

### Core Startup Sequence

```
start64 (FreeRTOS_startup.S)
    │
    ├── Read MPIDR_EL1 to determine core ID (bits 0-1)
    │
    ├── Core 0 (PrimaryCore)
    │   ├── Set EL3_stack, EL0_stack
    │   ├── Clear BSS section
    │   ├── SystemInit0()
    │   └── main()
    │
    └── Core 1 (SecondaryCore)
        ├── Set EL3_stack_s, EL0_stack_s
        ├── SystemInit1()
        └── main1()
```

### Core 0 (main) Execution

1. `SYS_Init()` - Initialize clocks, GPIO
2. `UART0_Init()` - Initialize UART for sysprintf
3. `vSafePrintfInit()` - Initialize recursive mutex for thread-safe printf
4. Install SGI0 yield handler via `IRQ_SetHandler()`
5. `RunCore1()` - Trigger core 1 boot via CA35WRBADR1 + SEV
6. Run selected demo application

### Core 1 (main1) Execution

1. Install SGI0 yield handler (same as core 0)
2. Spin-wait until:
   - `pxCurrentTCBs[1] != NULL` (idle task created)
   - `xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED`
3. `portDISABLE_INTERRUPTS()`
4. `vPortRestoreTaskContext()` - Enter scheduler, never returns

### Key Points

- Core 1 does NOT set up a tick timer (only core 0 runs tick)
- Both cores must install SGI0 handler before scheduler starts
- `vPortRestoreTaskContext()` installs FreeRTOS vector table (VBAR_EL3)
- FreeRTOS vector table uses SMC for yield (EL3) and IRQ for tick/IPI

## Demo Programs

### Demo 0: Ping-Pong (mainSELECTED_APPLICATION == 0)

Simple cross-core notification using `xTaskNotifyGive()`:
- Sender on core 0, receiver on core 1
- Kernel sends SGI0 via `portYIELD_CORE(1)` to wake receiver
- Infinite loop, prints status every 100 iterations

### Demo 1: Scheduling Verification (mainSELECTED_APPLICATION == 1)

**Demo 1-A: Preemption & Yield**
- Same-core preemption (hi-prio preempts lo-prio on core 0)
- `taskYIELD()` voluntary yield
- ISR timer semaphore wakeup via `portYIELD_FROM_ISR()`

**Demo 1-B: Interrupt Nesting Storm**
- TIMER2 and TIMER3 at different GIC priorities
- Verify `ullPortInterruptNesting[coreID]` > 0 inside ISR
- Verify 16-byte stack alignment in ISR
- Demonstrates genuine IRQ nesting

**Demo 1-C: Migration & Critical Section**
- Task A migrates across cores (forced by evictor task)
- Critical section (taskENTER_CRITICAL/EXIT_CRITICAL) works during migration
- Task B contends for critical section from other core

**Demo 1-D: FPU Pinned**
- Task calls `vPortTaskUsesFPU()` before using FPU
- Verifies sin^2(x) + cos^2(x) == 1 identity
- Core pinning required for FPU tasks

**Demo 1-E: Cross-Core Ping-Pong**
- Same as Demo 0, integrated into demo suite

## Supported by Driver

### FPU Support in IRQ

**Supported, but requires explicit implementation:**
- Default weak `vApplicationIRQHandler` does NOT save FPU registers
- To use FPU in ISR, implement strong `vApplicationFPUSafeIRQHandler()`
- The weak version saves all 32 Q registers + FPSR/FPCR before calling handler

```c
// To use FPU in ISR:
void vApplicationFPUSafeIRQHandler(uint32_t ulICCIAR)
{
    // Your ISR code using FPU here
}
```

### FPU Context for Tasks

**Supported via explicit registration:**
```c
void vFPUtask(void *pv)
{
    vPortTaskUsesFPU();  // MUST call before any FPU instructions
    
    double x = sin(0.5);  // Now safe to use FPU
}
```

- `portSAVE_CONTEXT` saves FPU registers IF `ullPortTaskHasFPUContext[coreID]` != 0
- `portRESTORE_CONTEXT` restores FPU registers based on per-core flag

### Core Affinity API

**Fully supported:**
```c
vTaskCoreAffinitySet(xTask, (1U << 0));  // Pin to core 0
vTaskCoreAffinitySet(xTask, (1U << 1));  // Pin to core 1

UBaseType_t mask = vTaskCoreAffinityGet(xTask);  // Get affinity
```

- `configUSE_CORE_AFFINITY = 1` in FreeRTOSConfig.h
- `configNUMBER_OF_CORES = 2`

### Interrupt Nesting

**Fully supported:**
- Per-core `ullPortInterruptNesting[coreID]` counter
- ISR can be preempted by higher-priority IRQ
- `portCHECK_IF_IN_ISR()` returns nesting depth > 0 inside ISR

```c
// Inside ISR:
if (ullPortInterruptNesting[portGET_CORE_ID()] > 0) {
    // We're in an ISR
}
```

### Critical Sections

**Fully supported (SMP-aware):**
```c
taskENTER_CRITICAL();   // Masks interrupts, acquires spinlock
// Critical code
taskEXIT_CRITICAL();  // Releases spinlock
```

- Recursive spinlocks track ownership and recursion count
- Works correctly across cores (no deadlock)

### Inter-Core Yield (SGI)

**Supported:**
```c
portYIELD_CORE(1);  // Send SGI0 to core 1, trigger context switch
```

- SGI0 used for FreeRTOS yield
- `vPortYieldCore()` sends SGI to target core

### Tick Timer

**Only core 0 runs tick:**
- ARM Generic Timer (CNTP, PPI 30)
- Core 1 does NOT run tick (would double tick rate)

## NOT Supported by Driver

### FPU Migration Between Cores

**NOT supported:**
- Tasks using FPU MUST be pinned to ONE core
- If a task with FPU context migrates to another core:
  - FPU registers will be from wrong core (corrupted)
  - No FPU context migration implemented

```c
// WRONG - FPU task without pinning:
void vFPUtask(void *pv) {
    vPortTaskUsesFPU();
    // ... uses FPU ...
    // If scheduler migrates this task to another core,
    // FPU state will be corrupted!
}

// CORRECT - FPU task with pinning:
xTaskCreate(vFPUtask, "FPU", stack, NULL, prio, &handle);
vTaskCoreAffinitySet(handle, (1U << 1));  // Pin to core 1
```

This is demonstrated in Demo 1-C (Migration & Critical Section):
- The migratable task is NOT using FPU
- Demo 1-D uses FPU with explicit core pinning

### Per-Core Independent Tick

**Not supported:**
- Only core 0 runs tick
- Time-slicing managed by core 0 for all cores
- Running tick on both cores would double tick rate

### Dynamic Task Preemption Disable

**Disabled:**
- `configUSE_TASK_PREEMPTION_DISABLE = 0`
- Individual tasks cannot disable preemption

### Automatic Secondary Core Boot

**Manual boot required:**
- Application must call `RunCore1()` explicitly
- Core 1 waits for scheduler to be ready before entering

## Configuration Notes

### GIC Priorities

- `configMAX_API_CALL_INTERRUPT_PRIORITY = 18`
- `configUNIQUE_INTERRUPT_PRIORITIES = 32`
- `portPRIORITY_SHIFT = 3`

Valid priorities: 0-31 (0 = highest, 31 = lowest)
ISR-safe interrupts: priority >= 18 (numerically <= 18)

### Tick Configuration

- `vConfigureTickInterrupt()` - Configures ARM Generic Timer
- `FreeRTOS_Tick_Handler()` - Tick ISR (core 0 only)

## Summary Table

| Feature | Supported | Notes |
|---------|-----------|-------|
| Dual-core SMP | Yes | 2 cores |
| FPU in task | Yes | Call vPortTaskUsesFPU() |
| FPU in ISR | Yes | Implement vApplicationFPUSafeIRQHandler |
| FPU migration | **No** | Must pin FPU tasks |
| Core affinity | Yes | vTaskCoreAffinitySet |
| IRQ nesting | Yes | ullPortInterruptNesting |
| Critical section | Yes | SMP spinlocks |
| SGI yield | Yes | portYIELD_CORE() |
| Tick on all cores | No | Core 0 only |
| Preemption disable | No | configUSE_TASK_PREEMPTION_DISABLE=0 |

## Logs

```c
--- Demo 1: Preemption & Yield ---
  [Demo1] Low-priority task spinning on core 0
  [Demo1] High-priority task running on core 0
  [Demo1] Low-priority task resumed on core 0 after preemption
  [PASS] Demo1-A  High-priority task ran
  [PASS] Demo1-A  High-priority ran on core 0 (same core)
  [PASS] Demo1-A  Low-priority was preempted and resumed
  [Demo1] Yield task on core 0 — calling taskYIELD()
  [Demo1] Yield task resumed after taskYIELD()
  [PASS] Demo1-B  taskYIELD()
  [Demo1] Timer CB on core 1 — giving semaphore from ISR
  [Demo1] Task woken by ISR semaphore on core 0
  [PASS] Demo1-C  Yield from ISR (semaphore wakeup)
--- Demo 1 complete ---

--- Demo 2: Interrupt Nesting Storm ---
  [Demo2] TMR2 IRQs: 498,780   TMR3 IRQs: 499,427
  [Demo2] Nesting observed (depth>1): 1,081 times
  [Demo2] Final ullPortInterruptNesting[1] = 0
  [PASS] Demo2-A  TIMER2 interrupts fired
  [PASS] Demo2-B  TIMER3 interrupts fired
  [PASS] Demo2-C  portCHECK_IF_IN_ISR() correct in ISRs
  [PASS] Demo2-D  Nesting depth returned to 0 in task context
  [PASS] Demo2-E  Stack pointer 16-byte aligned in ISRs
  [PASS] Demo2-F  Genuine interrupt nesting observed
--- Demo 2 complete ---

--- Demo 3: Migration & Critical Section ---
  [Demo3] Task A started on core 1, entering 1st critical section
  [Demo3] Task A completed 1st critical section on core 1
  [Demo3] Task A completed 1st critical section on core 1, waiting for migration
  [Demo3] Task B on core 0 trying to enter critical section
  [Demo3] Task B on core 1 entered and exited critical section
  [Demo3] Task A resumed on core 0, entering 2nd critical section
  [Demo3] Task A: core 1 -> core 0
  [PASS] Demo3-A  First critical section completed
  [PASS] Demo3-B  Second critical section completed
  [PASS] Demo3-C  Task A exited both critical sections
  [PASS] Demo3-D  Task B acquired/released critical section
  [PASS] Demo3-E  Task A migrated across cores
--- Demo 3 complete ---

  [Demo4] FPU task on core 1
  [Demo4] FPU task done on core 1  iters=1,000,000  pass=yes
  [Demo4] Disturber preempted FPU task 560 times
  [PASS] Demo4-A  FPU pinned sin^2+cos^2 identity
  [PASS] Demo4-B  FPU task stayed on pinned core
  [PASS] Demo4-C  Disturber actually preempted FPU task

--- Demo 5: Cross-Core Ping-Pong ---
  pings=100 [sender 0], pongs=100 [receiver 1]
  pings=200 [sender 0], pongs=200 [receiver 1]
  pings=300 [sender 0], pongs=300 [receiver 1]
  pings=400 [sender 0], pongs=400 [receiver 1]

  [Demo6] FPU migration task starting on core 0 (unpinned)
  [Demo6] FPU result: accumulated = 1,000.000000  expected = 1,000.000000
  [Demo6] Error = 0.000000000   Migrations = 997
  [PASS] Demo6-A  FPU registers correct after migration
  [PASS] Demo6-B  Task actually migrated across cores
```

--------------------

# FreeRTOS SMP Spinlock Fix — MA35D1 Cortex-A35 Port

## Problem

The FreeRTOS SMP kernel expects the port-layer lock functions (`portGET_TASK_LOCK`, `portRELEASE_TASK_LOCK`, `portGET_ISR_LOCK`, `portRELEASE_ISR_LOCK`) to be **recursive** (re-entrant from the same core). The original port used the BSP's `cpu_spin_lock()` / `cpu_spin_unlock()` directly — these are non-reentrant LDAXR/STXR test-and-set spinlocks that deadlock when the same core tries to acquire a lock it already holds.

### Deadlock Scenario

```
vTaskDelay()
  -> vTaskSuspendAll()
       -> portGET_TASK_LOCK(xCoreID)          // acquires task lock
  -> xTaskResumeAll()
       -> taskENTER_CRITICAL()
            -> vTaskEnterCritical()
                 -> portGET_TASK_LOCK(xCoreID) // DEADLOCK: same core, same lock
```

This affected any API that calls `vTaskSuspendAll()` internally: `vTaskDelay()`, `vTaskDelayUntil()`, `xQueueSend()`, `xQueueReceive()`, and others. The blinky demo (queue send/receive tasks) hit this deadlock immediately.

## Root Cause

The SMP kernel's lock protocol is inherently recursive. `vTaskSuspendAll()` acquires the task lock, then `xTaskResumeAll()` calls `taskENTER_CRITICAL()` which calls `vTaskEnterCritical()`, which tries to acquire the task lock again on the same core. This is by design — the kernel expects the port to handle re-acquisition from the owning core by tracking ownership and a recursion count.

The BSP's `cpu_spin_lock()` (in `Library/Arch/Core_A/Source/cpu.S`) is a standard non-reentrant ARMv8 exclusive-access spinlock with WFE. It spins forever if the same core calls it twice without an intervening unlock.

## Fix

Replaced the 4 thin non-reentrant wrapper functions with a single recursive lock implementation matching the ARM_CR82 SMP reference port's `vPortRecursiveLock()` pattern.

### Files Changed

| File | Change |
|------|--------|
| `portmacro.h` | Added `eLockType_t` enum, `vPortRecursiveLock()` prototype, rewrote lock macros to call it directly with `xCoreID` |
| `port.c` | Removed old `ulTaskLock`/`ulISRLock` variables and 4 wrapper functions, added recursive lock state and `vPortRecursiveLock()` |

### Recursive Lock Design

```c
typedef enum {
    eLockISR = 0,
    eLockTask,
    eLockCount
} eLockType_t;

static unsigned int ulSpinLock[ eLockCount ];    // underlying raw spinlock word
static uint32_t ulRecursionCount[ eLockCount ];  // per-lock recursion depth
static uint32_t ulOwner[ eLockCount ];           // which core owns the lock (0xFF = unowned)
```

**Acquire path:**
1. If this core already owns the lock → increment recursion count (no spin).
2. Otherwise → call `cpu_spin_lock()` to acquire the raw lock, set ownership and count to 1.

**Release path:**
1. Decrement recursion count.
2. If count reaches 0 → clear ownership, call `cpu_spin_unlock()` to release the raw lock.

This ensures:
- **Same core re-acquires without deadlock** (recursion count tracks depth).
- **Cross-core mutual exclusion is preserved** (only one core can own the raw spinlock at a time).
- **The underlying `cpu_spin_lock()`/`cpu_spin_unlock()` are only called when transitioning between unowned↔owned**, so they are never called reentrantly.

### Lock Macros (portmacro.h)

Before (non-reentrant, discarded xCoreID):
```c
#define portGET_TASK_LOCK( xCoreID )  do { (void)(xCoreID); vPortGetTaskLock(); } while(0)
```

After (recursive, passes xCoreID through):
```c
#define portGET_TASK_LOCK( xCoreID )  vPortRecursiveLock( (uint32_t)(xCoreID), eLockTask, pdTRUE )
```

### Lock Ordering

Unchanged — the SMP kernel enforces Task Lock first, ISR Lock second on acquire; ISR Lock first, Task Lock second on release. The recursive wrapper does not alter this ordering.

## Verification

- Confirmed no remaining references to the deleted `ulTaskLock`/`ulISRLock` variables or old wrapper functions anywhere in the port layer or application code.
- Confirmed the recursive lock pattern matches the ARM_CR82 reference SMP port (`ThirdParty/FreeRTOS-SMP/portable/GCC/ARM_CR82/port.c` line 1636).
- Confirmed lock acquire/release balance across all kernel call sites in `tasks.c`.
- Tested on hardware — dual-core blinky demo runs without deadlock.
