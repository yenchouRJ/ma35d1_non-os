

/* Includes ------------------------------------------------------------------*/
#include "lwip/opt.h"
#include "lwip/arch.h"
#include "lwip/api.h"
#include "string.h"
#include "udp_echoserver-netconn.h"
#include "arch/sys_arch.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
#define UDPECHOSERVER_THREAD_PRIO       ( tskIDLE_PRIORITY + 2UL )
#define UDPECHOSERVER_THREAD_STACKSIZE  ( 200UL )

#define UDPECHO_STACK   2048
#define UDPECHO_PRIO    ( tskIDLE_PRIORITY + 3 )
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/


/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/**
  * @brief  UDP server thread
  * @param arg pointer on argument(not used here)
  * @retval None
  */
static void udp_echoserver_netconn_thread(void *arg)
{
    struct netconn *conn;
    err_t err;
    struct netbuf *buf, *buf_send;
    char *data, *payload_data;
    static ip_addr_t *get_addr;
    static unsigned short get_port;
    unsigned int payload_len;

    /* Create a new UDP connection handle */
    conn = netconn_new(NETCONN_UDP);
    if (conn!= NULL)
    {
        /* Bind to port 80 with default IP address */
        err = netconn_bind(conn, NULL, 80);

        if (err == ERR_OK)
        {
            while(1)
            {

                sysprintf("Wait for UDP data ...\n");

                while(netconn_recv(conn, &buf) != ERR_OK);

                sysprintf("Received UDP packet!\n");


                /* Get destination ip address and port*/
                get_addr = netbuf_fromaddr(buf);
                get_port = netbuf_fromport(buf);

                sysprintf("From %s:%d\n",
                          ipaddr_ntoa(get_addr),
                          get_port);


                /* Get the payload and length */
                payload_len = buf->p->len;
                payload_data = buf->p->payload;

                sysprintf("Payload length: %d\n", payload_len);

                sysprintf("Payload data: ");
                for (int i = 0; i < payload_len; i++)
                    sysprintf("%c", payload_data[i]);
                sysprintf("\n");

                /* Prepare data */
                buf_send = netbuf_new();
                data = netbuf_alloc(buf_send, payload_len);
                memcpy (data, payload_data, payload_len);

                sysprintf("Sending echo back...\n");

                /* Send the packet */
                netconn_sendto(conn, buf_send, get_addr, get_port);

                /* Free the buffer */
                netbuf_delete(buf_send);
                netbuf_delete(buf);
            }
        }
    }
}

/**
  * @brief  Initialize the UDP echo server (start its thread)
  * @param  none
  * @retval None
  */
void udp_echoserver_netconn_init()
{
    sys_thread_new("UDPECHO", udp_echoserver_netconn_thread, NULL, UDPECHOSERVER_THREAD_STACKSIZE, UDPECHOSERVER_THREAD_PRIO);

}
