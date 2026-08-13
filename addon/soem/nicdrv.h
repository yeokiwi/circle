//
// nicdrv.h
//
// EtherCAT NIC driver of SOEM (Simple Open EtherCAT Master) for Circle
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2026  R. Stange <rsta2@gmx.net>
//
// This file is part of the Circle port of SOEM (Simple Open EtherCAT Master),
// which is dual-licensed under GPLv3 and a commercial license. See the file
// LICENSE.md in the SOEM sources for full license information.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#ifndef _nicdrvh_
#define _nicdrvh_

#ifdef __cplusplus
extern "C" {
#endif

/** Size of the buffer, which is handed over to CNetDevice::ReceiveFrame().
 * This must be the same as FRAME_BUFFER_SIZE in <circle/netdevice.h>, which
 * cannot be included here, because this is a C header file. */
#define EC_CIRCLE_FRAMEBUFSIZE 1600

/** pointer structure to Tx and Rx stacks */
typedef struct
{
   /** socket connection used */
   int *sock;
   /** tx buffer */
   ec_bufT (*txbuf)[EC_MAXBUF];
   /** tx buffer lengths */
   int (*txbuflength)[EC_MAXBUF];
   /** temporary receive buffer */
   ec_bufT *tempbuf;
   /** rx buffers */
   ec_bufT (*rxbuf)[EC_MAXBUF];
   /** rx buffer status fields */
   int (*rxbufstat)[EC_MAXBUF];
   /** received MAC source address (middle word) */
   int (*rxsa)[EC_MAXBUF];
   /** number of received frames */
   uint64 rxcnt;
} ec_stackT;

/** pointer structure to buffers for redundant port.
 * Line redundancy is not supported by this port, but the structure is
 * required by the SOEM library. */
typedef struct
{
   ec_stackT stack;
   int sockhandle;
   /** rx buffers */
   ec_bufT rxbuf[EC_MAXBUF];
   /** rx buffer status */
   int rxbufstat[EC_MAXBUF];
   /** rx MAC source address */
   int rxsa[EC_MAXBUF];
   /** temporary rx buffer */
   ec_bufT tempinbuf;
} ecx_redportt;

/** pointer structure to buffers, vars and mutexes for port instantiation */
typedef struct
{
   ec_stackT stack;
   int sockhandle;
   /** rx buffers */
   ec_bufT rxbuf[EC_MAXBUF];
   /** rx buffer status */
   int rxbufstat[EC_MAXBUF];
   /** rx MAC source address */
   int rxsa[EC_MAXBUF];
   /** temporary rx buffer */
   ec_bufT tempinbuf;
   /** temporary rx buffer status */
   int tempinbufs;
   /** transmit buffers */
   ec_bufT txbuf[EC_MAXBUF];
   /** transmit buffer lengths */
   int txbuflength[EC_MAXBUF];
   /** temporary tx buffer */
   ec_bufT txbuf2;
   /** temporary tx buffer length */
   int txbuflength2;
   /** last used frame index */
   uint8 lastidx;
   /** current redundancy state */
   int redstate;
   /** pointer to redundancy port and buffers */
   ecx_redportt *redport;
   osal_mutext *getindex_mutex;
   osal_mutext *tx_mutex;
   osal_mutext *rx_mutex;

   /* Circle specific fields */

   /** the net device (CNetDevice *), this port is bound to */
   void *netdevice;
   /** name of the net device (e.g. "eth0") */
   char ifname[EC_MAXLEN_ADAPTERNAME];
   /** buffer for one frame received from the net device */
   uint8 rxframe[EC_CIRCLE_FRAMEBUFSIZE];
   /** number of frames sent to the net device */
   uint64 txframes;
   /** number of frames, which could not be sent */
   uint64 txerrors;
   /** number of received frames, which were not EtherCAT frames */
   uint64 rxdropped;
} ecx_portt;

extern const uint16 priMAC[3];
extern const uint16 secMAC[3];

void ec_setupheader(void *p);
int ecx_setupnic(ecx_portt *port, const char *ifname, int secondary);
int ecx_closenic(ecx_portt *port);
void ecx_setbufstat(ecx_portt *port, uint8 idx, int bufstat);
uint8 ecx_getindex(ecx_portt *port);
int ecx_outframe(ecx_portt *port, uint8 idx, int stacknumber);
int ecx_outframe_red(ecx_portt *port, uint8 idx);
int ecx_inframe(ecx_portt *port, uint8 idx, int stacknumber);
int ecx_waitinframe(ecx_portt *port, uint8 idx, int timeout);
int ecx_srconfirm(ecx_portt *port, uint8 idx, int timeout);

/** Is the link of the net device, the port is bound to, up?
 * @param[in] port = port context struct
 * @return >0 if the link is up
 */
int ecx_linkup(ecx_portt *port);

/** Update the settings of the net device according to the PHY status.
 * This has to be called about every 2 seconds, because the net device is not
 * managed by a TCP/IP stack (CNetSubSystem), which would do this otherwise.
 * @param[in] port = port context struct
 * @return >0 if successful
 */
int ecx_updatephy(ecx_portt *port);

#ifdef __cplusplus
}
#endif

#endif
