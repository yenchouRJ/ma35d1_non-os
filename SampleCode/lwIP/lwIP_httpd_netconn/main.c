/**************************************************************************//**
 * @file     main.c
 *
 * @brief    FreeRTOS-SMP HTTP server (netconn) using LwIP.
 *           The server's IP address could configure statically to
 *           192.168.0.2, or assign by DHCP server.
 *
 *           Core 0 starts the scheduler; core 1 enters via main1().
 *           The ARMv8 Generic Timer (CNTP, PPI 30) is used for the tick.
 *           SGI0 is used for inter-core yield signalling (IPI).
 *
 * @copyright (C) 2026 Nuvoton Technology Corp. All rights reserved.
 *****************************************************************************/

#include "NuMicro.h"
#include "FreeRTOS.h"
#include "task.h"

/* lwIP includes */
#include "lwipopts.h"
#include "lwip/tcpip.h"
#include "netif/ethernetif.h"
#include "httpserver-netconn.h"
#if (LWIP_DHCP == 1)
#include "lwip/dhcp.h"
#endif

#define WEB_TASK_PRIORITY        ( tskIDLE_PRIORITY + 3UL )
#define WEB_THREAD_STACKSIZE     ( 400 )

struct netif netif;

/*-----------------------------------------------------------*/
/* External references needed for secondary core boot.       */
/*-----------------------------------------------------------*/
extern void * volatile pxCurrentTCBs[];
extern void vSafePrintfInit(void);
extern void vPortRestoreTaskContext( void );

#if ( configNUMBER_OF_CORES > 1 )
    extern void RunCore1( void );
#endif

/*-----------------------------------------------------------*/
/* Secondary core (core 1) entry point.                      */
/*-----------------------------------------------------------*/
void main1( void )
{
#if ( configNUMBER_OF_CORES > 1 )
    /* Install SGI0 yield handler on core 1. */
    IRQ_SetHandler( (IRQn_ID_t)portYIELD_SGIn, vSGIYieldHandler );
    IRQ_SetPriority( (IRQn_ID_t)portYIELD_SGIn,
                    configMAX_API_CALL_INTERRUPT_PRIORITY << portPRIORITY_SHIFT );
    IRQ_Enable( (IRQn_ID_t)portYIELD_SGIn );

    /* Spin until the SMP scheduler is fully running. */
    while( ( pxCurrentTCBs[ 1 ] == NULL ) ||
            ( xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED ) )
    {
        __asm volatile ( "yield" );
    }

    __asm volatile ( "DSB SY" ::: "memory" );
    __asm volatile ( "ISB SY" );

    ( void ) portDISABLE_INTERRUPTS();
    vPortRestoreTaskContext();
#endif /* configNUMBER_OF_CORES > 1 */

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

    /* Update System Core Clock */
    /* User can use SystemCoreClockUpdate() to calculate SystemCoreClock. */
    SystemCoreClockUpdate();

    /* Init UART for sysprintf */
    UART0_Init();

    /* Configure EPLL = 500MHz */
    CLK->PLL[EPLL].CTL0 = (6 << CLK_PLLnCTL0_INDIV_Pos) | (250 << CLK_PLLnCTL0_FBDIV_Pos); // M=6, N=250
    CLK->PLL[EPLL].CTL1 = 2 << CLK_PLLnCTL1_OUTDIV_Pos; // EPLL divide by 2 and enable
    CLK_WaitClockReady(CLK_STATUS_STABLE_EPLL);

    /* Lock protected registers */
    SYS_LockReg();
}

static netif_init_fn ethernetif_init(int intf)
{
    netif_init_fn ethernetif_init;

    if(intf == GMACINTF0)
        ethernetif_init = ethernetif_init0;
    else
        ethernetif_init = ethernetif_init1;

    return ethernetif_init;
}

static void vWebTask( void *pvParameters )
{
    ip_addr_t ipaddr;
    ip_addr_t netmask;
    ip_addr_t gw;

    /* Remove compiler warning about unused parameter. */
    ( void ) pvParameters;

#if (LWIP_DHCP == 1)
	/* To enable DHCP, set LWIP_DHCP to 1 in lwipopts.h */
    IP4_ADDR(&gw, 0, 0, 0, 0);
    IP4_ADDR(&ipaddr, 0, 0, 0, 0);
    IP4_ADDR(&netmask, 0, 0, 0, 0);
#else
    IP4_ADDR(&gw, 192,168,0,1);
    IP4_ADDR(&ipaddr, 192,168,0,2);
    IP4_ADDR(&netmask, 255,255,255,0);
#endif

    tcpip_init(NULL, NULL);

    netif_add(&netif, &ipaddr, &netmask, &gw, NULL, ethernetif_init(GMAC_INTF), tcpip_input);
    netif_set_default(&netif);
    netif_set_up(&netif);

#if (LWIP_DHCP == 1)
    sysprintf("DHCP starting ...\n");
    if(dhcp_start(&netif) == ERR_OK) {
        while(dhcp_supplied_address(&netif) == 0)
            vTaskDelay(10000);
    } else {
        sysprintf("DHCP fail\n");
        while(1) {}
    }
#endif

    sysprintf("[ httpd_netconn ] \n");
    sysprintf("IP address:      %s\n", ip4addr_ntoa(&netif.ip_addr));
    sysprintf("Subnet mask:     %s\n", ip4addr_ntoa(&netif.netmask));
    sysprintf("Default gateway: %s\n", ip4addr_ntoa(&netif.gw));

    http_server_netconn_init();

    vTaskSuspend( NULL );
}

/*-----------------------------------------------------------*/
/* Primary core (core 0) entry point.                        */
/*-----------------------------------------------------------*/
int main(void)
{
    SYS_Init();

    vSafePrintfInit();

    sysprintf("\nCPU @ %d Hz\n", SystemCoreClock);
    sysprintf("FreeRTOS-SMP starting on %d cores\n", configNUMBER_OF_CORES);

    /* Install SGI0 yield handler on core 0. */
    IRQ_SetHandler( (IRQn_ID_t)portYIELD_SGIn, vSGIYieldHandler );
    IRQ_SetPriority( (IRQn_ID_t)portYIELD_SGIn,
                     configMAX_API_CALL_INTERRUPT_PRIORITY << portPRIORITY_SHIFT );
    IRQ_Enable( (IRQn_ID_t)portYIELD_SGIn );

#if ( configNUMBER_OF_CORES > 1 )
    RunCore1();
#endif

    sysprintf("\n\nCPU @ %d Hz\n", SystemCoreClock);
    sysprintf("FreeRTOS is starting ...\n");

#if ( configNUMBER_OF_CORES > 1 ) && ( configUSE_CORE_AFFINITY == 1 )
    {
        TaskHandle_t xWebTaskHandle = NULL;
        xTaskCreate( vWebTask, "WebTask", WEB_THREAD_STACKSIZE, NULL, WEB_TASK_PRIORITY, &xWebTaskHandle );
        vTaskCoreAffinitySet( xWebTaskHandle, 0x01 );
    }
#else
    xTaskCreate( vWebTask, "WebTask", WEB_THREAD_STACKSIZE, NULL, WEB_TASK_PRIORITY, NULL );
#endif

    /* Start the tasks and timer running. */
    vTaskStartScheduler();

    /* If all is well, the scheduler will now be running, and the following
    line will never be reached.  If the following line does execute, then
    there was either insufficient FreeRTOS heap memory available for the idle
    and/or timer tasks to be created, or vTaskStartScheduler() was called from
    User mode.  See the memory management section on the FreeRTOS web site for
    more details on the FreeRTOS heap http://www.freertos.org/a00111.html.  The
    mode from which main() is called is set in the C start up code and must be
    a privileged mode (not user mode). */
    for( ;; );
}
