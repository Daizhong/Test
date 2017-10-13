/******************************************************************************
*
* Copyright (C) 2009 - 2014 Xilinx, Inc.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* Use of the Software is limited solely to applications:
* (a) running on a Xilinx device, or
* (b) that interact with a Xilinx device through a bus or interconnect.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
* OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Except as contained in this notice, the name of the Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Xilinx.
*
******************************************************************************/

#include <stdio.h>
#include "xil_printf.h"

#include "xparameters.h"

#include "netif/xadapter.h"

#include "platform.h"
#include "platform_config.h"
#if defined (__arm__) || defined(__aarch64__)
#include "xil_printf.h"
#endif

#include "lwip/tcp.h"
#include "xil_cache.h"


//debug

#include "xgpiops.h"
#include "sleep.h"
#include "xil_printf.h"
#define MIO_BASE 0xE000A000

#define DATA1_RO 0x64
#define DATA2 0x48
#define DATA2_RO 0x284
#define OEN_2 0x288

#define GTC_BASE 0xF8F00200
#define GTC_CTRL 0x08
#define GTC_DATL 0x00
#define GTC_DATH 0x04

#define CLK_3x2x 333333333 //计数器达到该值,即为1s

int tcp_trans_done = 0;
unsigned int tcp_client_connected = 0;
struct tcp_pcb* connected_pcb;

void delay_05ms()
{
	int i = CLK_3x2x / 200 , j;
	*((volatile int*)(GTC_BASE + GTC_CTRL)) = 0x00;          //清零定时器使能位，定时器停止
	*((volatile int*)(GTC_BASE + GTC_DATL)) = 0x00000000;    //写入低32bit
	*((volatile int*)(GTC_BASE + GTC_DATH)) = 0x00000000;    //写入高32bit
	*((volatile int*)(GTC_BASE + GTC_CTRL)) = 0x01;        //开启定时器
	do
	{
		j = *((volatile int*)(GTC_BASE + GTC_DATL));
	}
	while(j < i);
}

void delay_1s(int t)
{
	int i = CLK_3x2x / 2 , j;
	*((volatile int*)(GTC_BASE + GTC_CTRL)) = 0x00;          //清零定时器使能位，定时器停止
	*((volatile int*)(GTC_BASE + GTC_DATL)) = 0x00000000;    //写入低32bit
	*((volatile int*)(GTC_BASE + GTC_DATH)) = 0x00000000;    //写入高32bit
	*((volatile int*)(GTC_BASE + GTC_CTRL)) = 0x01;        //开启定时器
	do
	{
		j = *((volatile int*)(GTC_BASE + GTC_DATL));
	}
	while(j < i);
}

void tic(void)
{
	*((volatile int*)(GTC_BASE + GTC_CTRL)) = 0x00;          //清零定时器使能位，定时器停止
	*((volatile int*)(GTC_BASE + GTC_DATL)) = 0x00000000;    //写入低32bit
	*((volatile int*)(GTC_BASE + GTC_DATH)) = 0x00000000;    //写入高32bit
	*((volatile int*)(GTC_BASE + GTC_CTRL)) = 0x01;          //开启定时器
}

double toc(void)
{
	*((volatile int*)(GTC_BASE + GTC_CTRL)) = 0x00;
	long long j = *((volatile int*)(GTC_BASE + GTC_DATH));
	double elapsed_time = j << 32;
	j = *((volatile int*)(GTC_BASE + GTC_DATL));
	elapsed_time += j;
	elapsed_time = elapsed_time / CLK_3x2x;
	elapsed_time = elapsed_time * 1000;
	printf("Elapsed time is %f ms. \r\n", elapsed_time);
	return elapsed_time;
}

//end debug



err_t Mysent_callback(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
	tcp_trans_done ++ ;

	printf("tcp_trans_done = %d ! \n", tcp_trans_done);

    return ERR_OK;
}

err_t Myrecv_callback(void *arg, struct tcp_pcb *tpcb,
                               struct pbuf *p, err_t err)
{
	/* do not read the packet if we are not in ESTABLISHED state */
	if (!p) {
		tcp_close(tpcb);
		tcp_recv(tpcb, NULL);
		return ERR_OK;
	}

	/* indicate that the packet has been received */
	tcp_recved(tpcb, p->len);
	toc();
	printf("%s",(char*)p->payload);

	/* free the received pbuf */
	pbuf_free(p);

	return ERR_OK;
}

err_t Myconnect_callback(void *arg, struct tcp_pcb *pTCPpcb, err_t errNum)
{
    tcp_arg(pTCPpcb, NULL);
    tcp_sent(pTCPpcb, Mysent_callback);
    tcp_recv(pTCPpcb, Myrecv_callback);
    tcp_client_connected = 1;
    connected_pcb = pTCPpcb;
    return ERR_OK;
}

static int GB_InitTcpClnt(void)
{
    struct tcp_pcb *pClntPcb = NULL;
    err_t errNum = 0;
    err_t cRet = 0;
    struct ip_addr RemoteIP;

    pClntPcb = tcp_new();
    if (NULL == pClntPcb)
    {
		xil_printf("Error creating PCB. Out of Memory\n\r");
		return -1;
    }

    cRet = tcp_bind(pClntPcb, IPADDR_ANY, 8);
    if (ERR_OK != cRet)
    {
		xil_printf("Error tcp_bind PCB. Out of Memory\n\r");
		return -1;
    }

    //tcp_arg(pClntPcb, NULL);
    //tcp_setprio(pClntPcb, TCP_PRIO_NORMAL+1);

    IP4_ADDR(&RemoteIP, 192,168,1,120);

    errNum = tcp_connect(pClntPcb, &RemoteIP, (u16_t)(8888), Myconnect_callback);
    xil_printf("block or not \n\r");
    if (ERR_OK != errNum)
    {
    	xil_printf("Error connect request has not been sent is . \n\r");
        return -1;
    }

    return 0;

}


int transfer_data() {
	//
	struct tcp_pcb* tpcb = connected_pcb;
	char *buffer = malloc(sizeof(char)*5);
	buffer[0] = '2';
	buffer[1] = '0';
	buffer[2] = 'd';
	buffer[3] = 'B';
	buffer[4] = '\0';
	int len = 5;
	err_t err;
    	/* echo back the payload */
	/* in this case, we assume that the payload is < TCP_SND_BUF */
	//if (tcp_sndbuf(tpcb) > SEND_SIZE) {
		err = tcp_write(tpcb, buffer, len, 1);
		if(err != ERR_OK)
		{
			xil_printf("Error on tcp_write !\n");
			return -1;
		}
		//tic();
	//} else
		//xil_printf("no space in tcp_sndbuf\n\r");

	err = tcp_output(tpcb);
	tic();
	if(err != ERR_OK)
	{
		xil_printf("Error on tcp_output !\n");
		return -1;
	}
	free(buffer);


	//
	return 0;
}




#if LWIP_DHCP==1
#include "lwip/dhcp.h"
#endif

/* defined by each RAW mode application */
void print_app_header();
int start_application();
int transfer_data();
void tcp_fasttmr(void);
void tcp_slowtmr(void);

/* missing declaration in lwIP */
void lwip_init();

#if LWIP_DHCP==1
extern volatile int dhcp_timoutcntr;
err_t dhcp_start(struct netif *netif);
#endif

extern volatile int TcpFastTmrFlag;
extern volatile int TcpSlowTmrFlag;
static struct netif server_netif;
struct netif *echo_netif;

void
print_ip(char *msg, struct ip_addr *ip) 
{
	print(msg);
	xil_printf("%d.%d.%d.%d\n\r", ip4_addr1(ip), ip4_addr2(ip), 
			ip4_addr3(ip), ip4_addr4(ip));
}

void
print_ip_settings(struct ip_addr *ip, struct ip_addr *mask, struct ip_addr *gw)
{

	print_ip("Board IP: ", ip);
	print_ip("Netmask : ", mask);
	print_ip("Gateway : ", gw);
}

#if defined (__arm__) && !defined (ARMR5)
#if XPAR_GIGE_PCS_PMA_SGMII_CORE_PRESENT == 1 || XPAR_GIGE_PCS_PMA_1000BASEX_CORE_PRESENT == 1
int ProgramSi5324(void);
int ProgramSfpPhy(void);
#endif
#endif

#ifdef XPS_BOARD_ZCU102
#ifdef XPAR_XIICPS_0_DEVICE_ID
int IicPhyReset(void);
#endif
#endif

int main()
{

	//struct netif *pGB_netif, GB_netif;
	struct ip_addr IPaddr, NetMask, GateWay;

	/* the mac address of the board. this should be unique per board */
	unsigned char MacAddr[] =
	{ 0x00, 0x0a, 0x35, 0x00, 0x01, 0x02 };

	echo_netif = &server_netif;
#if defined (__arm__) && !defined (ARMR5)
#if XPAR_GIGE_PCS_PMA_SGMII_CORE_PRESENT == 1 || XPAR_GIGE_PCS_PMA_1000BASEX_CORE_PRESENT == 1
	ProgramSi5324();
	ProgramSfpPhy();
#endif
#endif

/* Define this board specific macro in order perform PHY reset on ZCU102 */
#ifdef XPS_BOARD_ZCU102
	IicPhyReset();
#endif
	xil_printf("\r\n\r\n");
	xil_printf("------------- Main vernsion 1.8-------------\r\n");
    xil_printf("-----lwIP RAW Mode Demo Application ------\r\n");
	init_platform();

#if LWIP_DHCP==1
    ipaddr.addr = 0;
	gw.addr = 0;
	netmask.addr = 0;
#else
	/* initliaze IP addresses to be used */
	IP4_ADDR(&IPaddr,  192, 168,   1, 10);
	IP4_ADDR(&NetMask, 255, 255, 255,  0);
	IP4_ADDR(&GateWay,      192, 168,   1,  1);
#endif	
	print_app_header();

	lwip_init();

  	/* Add network interface to the netif_list, and set it as default */
	if (!xemac_add(echo_netif, &IPaddr, &NetMask,
						&GateWay, MacAddr,
						PLATFORM_EMAC_BASEADDR)) {
		xil_printf("Error adding N/W interface\n\r");
		return -1;
	}
	netif_set_default(echo_netif);

	/* now enable interrupts */
	platform_enable_interrupts();

	/* specify that the network if is up */
	netif_set_up(echo_netif);

#if (LWIP_DHCP==1)
	/* Create a new DHCP client for this interface.
	 * Note: you must call dhcp_fine_tmr() and dhcp_coarse_tmr() at
	 * the predefined regular intervals after starting the client.
	 */
	dhcp_start(echo_netif);
	dhcp_timoutcntr = 24;

	while(((echo_netif->ip_addr.addr) == 0) && (dhcp_timoutcntr > 0))
		xemacif_input(echo_netif);

	if (dhcp_timoutcntr <= 0) {
		if ((echo_netif->ip_addr.addr) == 0) {
			xil_printf("DHCP Timeout\r\n");
			xil_printf("Configuring default IP of 192.168.1.10\r\n");
			IP4_ADDR(&(echo_netif->ip_addr),  192, 168,   1, 10);
			IP4_ADDR(&(echo_netif->netmask), 255, 255, 255,  0);
			IP4_ADDR(&(echo_netif->gw),      192, 168,   1,  1);
		}
	}

	ipaddr.addr = echo_netif->ip_addr.addr;
	gw.addr = echo_netif->gw.addr;
	netmask.addr = echo_netif->netmask.addr;
#endif

	print_ip_settings(&IPaddr, &NetMask, &GateWay);

	/* start the application (web server, rxtest, txtest, etc..) */
	GB_InitTcpClnt();

	print("After GB_InitTcpClnt()! \n");
	printf("tcp_client_connected = %d \n", tcp_client_connected);

	/* receive and process packets */
	while (1) {
		if (TcpFastTmrFlag) {
			tcp_fasttmr();
			TcpFastTmrFlag = 0;
		}
		if (TcpSlowTmrFlag) {
			tcp_slowtmr();
			TcpSlowTmrFlag = 0;
		}
		xemacif_input(echo_netif);
		if(tcp_client_connected)
		{
			delay_05ms();
			transfer_data();
			//tcp_client_connected = 0;
		}
	}
  
	/* never reached */
	cleanup_platform();

	return 0;
}


