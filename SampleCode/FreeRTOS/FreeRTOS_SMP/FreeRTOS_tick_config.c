/*
 * FreeRTOS V202212.01
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/**************************************************************************//**
 * @file     FreeRTOS_tick_config.c
 *
 * @brief    Timer interrupt for FreeRTOS tick using ARMv8 Generic Timer.
 *
 *           Only core 0 runs the OS tick.  The FreeRTOS-SMP kernel's
 *           xTaskIncrementTick() has no per-core gating — it unconditionally
 *           increments xTickCount inside a critical section.
 *           If both cores called FreeRTOS_Tick_Handler the tick would
 *           advance at 2x the intended rate.
 *
 *           Core 0's tick handler already handles time-slicing for all
 *           cores: the kernel calls prvYieldCore(x) (→ SGI) for any
 *           remote core that needs a context switch.
 *
 * @copyright (C) 2023 Nuvoton Technology Corp. All rights reserved.
 *****************************************************************************/

/* Nuvoton includes. */
#include "NuMicro.h"

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"

/*-----------------------------------------------------------*/

/* The generic timer frequency is 12 MHz on MA35D1. */
#define configGENERIC_TIMER_FREQ_HZ     ( 12000000UL )

/* The timer load value to achieve the desired tick rate. */
#define configGENERIC_TIMER_LOAD_VALUE  ( configGENERIC_TIMER_FREQ_HZ / configTICK_RATE_HZ )

/*-----------------------------------------------------------*/

/*
 * vConfigureTickInterrupt() sets up the ARMv8 Non-Secure Physical Timer (CNTP)
 * to generate periodic interrupts at configTICK_RATE_HZ.
 *
 * The CNTP timer is a PPI (Private Peripheral Interrupt, IRQ 30) — each core
 * has its own independent instance.  However, only core 0 runs the tick
 * because the SMP V202110.00 kernel's xTaskIncrementTick() unconditionally
 * increments xTickCount (no per-core gating).  If both cores ran the tick
 * handler, xTickCount would advance at 2x the intended rate.
 *
 * Core 0's tick handler already handles scheduling for all cores: when a
 * remote core needs to yield, the kernel sends it an SGI via prvYieldCore().
 *
 * This function is called once by core 0 during xPortStartScheduler().
 */
void vConfigureTickInterrupt( void )
{
    extern void FreeRTOS_Tick_Handler( void );

    /* Disable the timer while configuring. */
    EL0_SetControl( 0 );

    /* Set the compare value for the next tick.
     * We use the compare value (CNTP_CVAL_EL0) approach:
     * Set it to current physical counter + load value. */
    EL0_SetPhysicalCompareValue( EL0_GetCurrentPhysicalValue() + configGENERIC_TIMER_LOAD_VALUE );

    /* The priority must be the lowest possible for FreeRTOS tick. */
    IRQ_SetPriority( (IRQn_ID_t)NonSecPhysicalTimer_IRQn,
                     portLOWEST_USABLE_INTERRUPT_PRIORITY << portPRIORITY_SHIFT );

    /* Install FreeRTOS_Tick_Handler as the interrupt handler. */
    IRQ_SetHandler( (IRQn_ID_t)NonSecPhysicalTimer_IRQn, FreeRTOS_Tick_Handler );

    /* Enable the timer interrupt. */
    IRQ_Enable( (IRQn_ID_t)NonSecPhysicalTimer_IRQn );

    /* Enable the timer (CNTP_CTL_EL0.ENABLE = 1, IMASK = 0). */
    EL0_SetControl( 1U );
}
/*-----------------------------------------------------------*/

/*
 * vClearTickInterrupt() clears the generic timer interrupt by programming
 * the next compare value. The generic timer fires when CNTPCT_EL0 >= CNTP_CVAL_EL0,
 * so we advance CNTP_CVAL_EL0 by one tick period to clear the condition and
 * schedule the next tick.
 */
void vClearTickInterrupt( void )
{
    /* Advance the compare value by one tick period.
     * Reading the current compare value and adding the load value ensures
     * we don't accumulate drift even if we're slightly late handling the interrupt. */
    EL0_SetPhysicalCompareValue( EL0_GetPhysicalCompareValue() + configGENERIC_TIMER_LOAD_VALUE );

    __asm volatile( "DSB SY" );
    __asm volatile( "ISB SY" );
}
/*-----------------------------------------------------------*/

/* IRQ take over by FreeRTOS kernel */
void vApplicationIRQHandler( uint32_t ulICCIAR )
{
    /* Interrupts cannot be re-enabled until the source of the interrupt is
    cleared. The ID of the interrupt is obtained by bitwise ANDing the ICCIAR
    value with 0x3FF. */

    IRQHandler_t handler;
    IRQn_ID_t num = (int32_t)ulICCIAR;

    /* Call the function installed in the array of installed handler
    functions. */
    handler = IRQ_GetHandler(num);
    if(handler != 0)
        (*handler)();

    /* NOTE: Do NOT call IRQ_EndOfInterrupt() here.
     * FreeRTOS_IRQ_Handler in portASM.S writes the ICCIAR value to
     * ICCEOIR after this function returns (the standard FreeRTOS
     * Cortex-A pattern).  Calling IRQ_EndOfInterrupt() here would
     * result in a double EOI which can cause GICv2 priority-drop
     * issues. */
}
