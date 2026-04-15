/**************************************************************************//**
 * @file     main.c
 *
 * @brief    FreeRTOS-SMP project for dual-core MA35D1 (Cortex-A35).
 *           Core 0 starts the scheduler; core 1 enters via main1().
 *
 * @note     The ARMv8 Generic Timer (CNTP, PPI 30) is used for the tick.
 *           SGI0 is used for inter-core yield signalling (IPI).
 *
 * @copyright (C) 2026 Nuvoton Technology Corp. All rights reserved.
 *****************************************************************************/

#include "NuMicro.h"
#include "FreeRTOS.h"
#include "task.h"

/* mainSELECTED_APPLICATION is used to select between two demo applications,
 * as described at the top of this file.
 *
 * When mainSELECTED_APPLICATION is set to 0 the simple ping-pong example will
 * be run.
 *
 * When mainSELECTED_APPLICATION is set to 1 the SMP tasking verification
 * demo will be run (critical sections, ISR nesting, multi-core scheduling,
 * FPU affinity).
 */
#define mainSELECTED_APPLICATION	1

/*
 * See the comments at the top of this file and above the
 * mainSELECTED_APPLICATION definition.
 */
#if ( mainSELECTED_APPLICATION == 0 )
extern void main_pingpong_demo( void );
#elif ( mainSELECTED_APPLICATION == 1 )
extern void main_scheduling_demo( void );
#else
#error Invalid mainSELECTED_APPLICATION setting.
#endif

/*-----------------------------------------------------------*/
/* SMP: External references needed for secondary core boot.  */
/*-----------------------------------------------------------*/

/* Per-core yield flag from the portable layer (port.c / portASM.S).
 * Set to pdTRUE by the SGI0 handler so that FreeRTOS_IRQ_Handler
 * triggers a context switch on IRQ exit. */
extern uint64_t ullPortYieldRequired[];

/* The per-core TCB array from the kernel (tasks.c).
 * Core 1 waits for pxCurrentTCBs[1] != NULL before entering the
 * scheduler, ensuring the SMP kernel has assigned a task (idle) to it. */
extern void * volatile pxCurrentTCBs[];

/* Safe printf initialization */
extern void vSafePrintfInit(void);

/* Assembly entry point: installs FreeRTOS vector table and restores
 * the first task context for the calling core (core-aware via MPIDR). */
extern void vPortRestoreTaskContext( void );

/* BSP: Boot secondary core (writes entry to CA35WRBADR1 + SEV). */
extern void RunCore1( void );

/*-----------------------------------------------------------*/
/* SGI0 handler for inter-core yield.                        */
/*-----------------------------------------------------------*/

/*
 * When the SMP kernel wants core N to yield (because a higher-priority
 * task became ready), it calls portYIELD_CORE(N) which sends SGI0 to
 * that core.  The SGI0 interrupt enters FreeRTOS_IRQ_Handler, which
 * dispatches through vApplicationIRQHandler -> IRQ_GetHandler(SGI0).
 *
 * This handler simply sets ullPortYieldRequired[coreID] = pdTRUE so
 * that FreeRTOS_IRQ_Handler performs a context switch on IRQ exit.
 */
static void prvSGI0YieldHandler( void )
{
	/* Determine which core is handling this SGI. */
	uint64_t mpidr;
	BaseType_t xCoreID;

	__asm volatile ( "MRS %0, MPIDR_EL1" : "=r" ( mpidr ) ); // cpuid()
	xCoreID = ( BaseType_t ) ( mpidr & 0x3UL );

	ullPortYieldRequired[ xCoreID ] = pdTRUE;
}

/*-----------------------------------------------------------*/
/* Secondary core (core 1) entry point.                      */
/*-----------------------------------------------------------*/

/*
 * main1() overrides the weak default in system_MA35D1.c.
 *
 * Boot sequence for core 1:
 *   startup.S: SecondaryCore -> set stack -> SystemInit1() -> main1()
 *
 * SystemInit1() has already:
 *   - Set the generic timer frequency
 *   - Initialised the MMU
 *   - Initialised the GIC CPU interface
 *   - Enabled interrupts
 *
 * What we do here:
 *   1. Install the SGI0 yield handler on this core's GIC
 *   2. Wait for core 0 to start the scheduler (pxCurrentTCBs[1] populated)
 *   3. Enter the FreeRTOS scheduler via vPortRestoreTaskContext()
 *      (which installs the FreeRTOS vector table and ERET into the
 *       first task assigned to this core)
 *
 * NOTE: Core 1 does NOT set up a tick timer.  Only core 0 runs the OS
 *       tick because the SMP V202110.00 kernel's xTaskIncrementTick()
 *       unconditionally increments xTickCount — running it on both cores
 *       would advance the tick at 2x speed.  Core 0 handles time-slicing
 *       for all cores via SGI (prvYieldCore).
 */
void main1( void )
{
	/* 1. Install the SGI0 yield handler on this core.
	 *    The GIC CPU interface was already initialised by SystemInit1().
	 *    IRQ_SetHandler / IRQ_Enable work on the calling core's PPI/SGI. */
	IRQ_SetHandler( (IRQn_ID_t)SGI0_IRQn, prvSGI0YieldHandler );
	IRQ_SetPriority( (IRQn_ID_t)SGI0_IRQn,
	                 configMAX_API_CALL_INTERRUPT_PRIORITY << portPRIORITY_SHIFT );
	IRQ_Enable( (IRQn_ID_t)SGI0_IRQn );

	/* 2. Spin until the SMP scheduler is fully running.
	 *
	 *    Two conditions must be true before we enter portRESTORE_CONTEXT:
	 *
	 *    a) pxCurrentTCBs[1] != NULL — the idle task for this core has been
	 *       created and assigned.  Without this, portRESTORE_CONTEXT would
	 *       dereference a NULL stack pointer.
	 *
	 *    b) xSchedulerRunning == pdTRUE — the SMP kernel is ready.  Without
	 *       this, the first IRQ on core 1 would call vTaskSwitchContext() →
	 *       prvSelectHighestPriorityTask() which asserts xSchedulerRunning.
	 *
	 *    pxCurrentTCBs[1] is populated during prvCreateIdleTasks() which runs
	 *    before xSchedulerRunning is set.  So checking only pxCurrentTCBs[1]
	 *    is insufficient — there is a window where the TCB is assigned but
	 *    the scheduler is not yet marked as running.
	 *
	 *    xTaskGetSchedulerState() is safe to call here: when the scheduler
	 *    has not started, it returns taskSCHEDULER_NOT_STARTED immediately
	 *    without acquiring any SMP locks. */
	while( ( pxCurrentTCBs[ 1 ] == NULL ) ||
	       ( xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED ) )
	{
		__asm volatile ( "yield" );
	}

	/* Ensure we see all writes from core 0 (pxCurrentTCBs, ready lists,
	 * xSchedulerRunning, etc.) before we proceed. */
	__asm volatile ( "DSB SY" ::: "memory" );
	__asm volatile ( "ISB SY" );

	/* 3. Disable interrupts before entering the scheduler, matching
	 *    xPortStartScheduler() on core 0 (which calls portDISABLE_INTERRUPTS
	 *    before vPortRestoreTaskContext).  The ERET into the first task will
	 *    restore SPSR_EL3 which has interrupts unmasked, so they get
	 *    re-enabled atomically when the task starts running. */
	( void ) portDISABLE_INTERRUPTS();

	/* 4. Enter the scheduler.  vPortRestoreTaskContext() installs the
	 *    FreeRTOS vector table (VBAR_EL3) and ERETs into the task
	 *    stored in pxCurrentTCBs[1].  This function never returns. */
	vPortRestoreTaskContext();

	/* Should never reach here. */
	for( ;; );
}

void UART0_Init()
{
    /* Enable UART0 clock */
    CLK_EnableModuleClock(UART0_MODULE);
    CLK_SetModuleClock(UART0_MODULE, CLK_CLKSEL2_UART0SEL_HXT, CLK_CLKDIV1_UART0(1));

    /* Set multi-function pins */
    SYS->GPE_MFPH &= ~(SYS_GPE_MFPH_PE14MFP_Msk | SYS_GPE_MFPH_PE15MFP_Msk);
    SYS->GPE_MFPH |= (SYS_GPE_MFPH_PE14MFP_UART0_TXD | SYS_GPE_MFPH_PE15MFP_UART0_RXD);

    /* Init UART to 115200-8n1 for print message */
    UART_Open(UART0, 115200);
}

void SYS_Init()
{
    /* Unlock protected registers */
    SYS_UnlockReg();

    /* Enable GPIO clock for LED */
    CLK_EnableModuleClock(GPK_MODULE);

    /* GPIO output mode */
    GPIO_SetMode(PJ, BIT14, GPIO_MODE_OUTPUT);

    /* Update System Core Clock */
    /* User can use SystemCoreClockUpdate() to calculate SystemCoreClock. */
    SystemCoreClockUpdate();

    /* Init UART for sysprintf */
    UART0_Init();

    /* Lock protected registers */
    SYS_LockReg();
}

/* main function - runs on core 0 */
int main(void)
{
    SYS_Init();

    /* Create the recursive mutex used by the thread-safe sysprintf().
     * Must be called before any FreeRTOS task calls sysprintf(). */
    vSafePrintfInit();

    sysprintf("\n\nCPU @ %d Hz\n", SystemCoreClock);
    sysprintf("FreeRTOS-SMP starting on %d cores\n", configNUMBER_OF_CORES);

    /* Install SGI0 yield handler on core 0 as well. */
    IRQ_SetHandler( (IRQn_ID_t)SGI0_IRQn, prvSGI0YieldHandler );
    IRQ_SetPriority( (IRQn_ID_t)SGI0_IRQn,
                     configMAX_API_CALL_INTERRUPT_PRIORITY << portPRIORITY_SHIFT );
    IRQ_Enable( (IRQn_ID_t)SGI0_IRQn );

    /* Boot core 1.  It will enter main1() and spin-wait until the
     * scheduler populates pxCurrentTCBs[1].  We must boot it before
     * calling the selected demo entry point because vTaskStartScheduler()
     * never returns.
     *
     * IMPORTANT: Do NOT define the EnSecondaryCore preprocessor symbol.
     * We control core 1 boot timing explicitly here rather than letting
     * it start automatically during SystemInit0(). */
    RunCore1();

    /* The mainSELECTED_APPLICATION setting is described at the top
    of this file. */
#if( mainSELECTED_APPLICATION == 0 )
    {
        main_pingpong_demo();
    }
#elif( mainSELECTED_APPLICATION == 1 )
    {
        main_scheduling_demo();
    }
#endif

    /* Should never be reached */
    return 0;
}
