//
// nicdrv.cpp
//
// EtherCAT NIC driver of SOEM (Simple Open EtherCAT Master) for Circle
//
// Low level interface functions to send and receive EtherCAT frames. This is
// the Circle variant of the RAW socket drivers, which come with SOEM. The
// frames are sent to and received from a Circle net device (CNetDevice)
// directly, without an IP stack in between. The net device must not be used
// by a CNetSubSystem instance at the same time.
//
// EtherCAT has the property, that frames are only sent by the master, and the
// sent frames always return in the receive buffer. There can be multiple
// frames "on the wire" before they return. To combine the received frames
// with the original sent frames, a buffer system is installed. The identifier
// is put in the index item of the EtherCAT header. The index is stored and
// compared, when a frame is received.
//
// Line redundancy (two net devices) is not supported by this port.
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
#include "oshw.h"
#include <circle/netdevice.h>
#include <circle/util.h>
#include <assert.h>

static_assert (EC_CIRCLE_FRAMEBUFSIZE == FRAME_BUFFER_SIZE,
	       "EC_CIRCLE_FRAMEBUFSIZE must match FRAME_BUFFER_SIZE");

/** Redundancy modes */
enum
{
	/** No redundancy, single NIC mode */
	ECT_RED_NONE,
	/** Double redundant NIC connection (not supported) */
	ECT_RED_DOUBLE
};

/** Primary source MAC address used for EtherCAT. */
const uint16 priMAC[3] = EC_PRIMARY_MAC_ARRAY;
/** Secondary source MAC address used for EtherCAT. */
const uint16 secMAC[3] = EC_SECONDARY_MAC_ARRAY;

//
// Net device access
//

/// \param pIfName Name of the net device ("eth0", "eth1", ... or "0", "1", ...)
/// \return Pointer to the net device object (0 if not available)
static CNetDevice *FindNetDevice (const char *pIfName)
{
	if (pIfName == 0)
	{
		return 0;
	}

	const char *pIndex = pIfName;
	if (strncmp (pIndex, "eth", 3) == 0)
	{
		pIndex += 3;
	}

	if (*pIndex == '\0')
	{
		return 0;
	}

	unsigned nIndex = 0;
	while ('0' <= *pIndex && *pIndex <= '9')
	{
		nIndex = nIndex * 10 + (unsigned) (*pIndex++ - '0');
	}

	if (*pIndex != '\0')
	{
		return 0;
	}

	return CNetDevice::GetNetDevice (NetDeviceTypeEthernet, nIndex);
}

/// \brief Send one frame to the net device
/// \return Number of bytes sent (-1 on failure)
static int SendFrame (ecx_portt *port, const void *pBuffer, int nLength)
{
	assert (port != 0);
	CNetDevice *pNetDevice = (CNetDevice *) port->netdevice;
	if (pNetDevice == 0)
	{
		return -1;
	}

	if (!pNetDevice->SendFrame (pBuffer, (unsigned) nLength))
	{
		port->txerrors++;

		return -1;
	}

	port->txframes++;

	return nLength;
}

/// \brief Poll the net device for one received frame
/// \return Number of bytes received (0 if nothing has been received)
static int ReceiveFrame (ecx_portt *port, ec_bufT *pBuffer)
{
	assert (port != 0);
	CNetDevice *pNetDevice = (CNetDevice *) port->netdevice;
	if (pNetDevice == 0)
	{
		return 0;
	}

	unsigned nLength;
	if (!pNetDevice->ReceiveFrame (port->rxframe, &nLength))
	{
		return 0;
	}

	if (nLength > EC_BUFSIZE)
	{
		nLength = EC_BUFSIZE;
	}

	assert (pBuffer != 0);
	memcpy (pBuffer, port->rxframe, nLength);

	return (int) nLength;
}

static void ecx_clear_rxbufstat (int *rxbufstat)
{
	assert (rxbufstat != 0);

	for (int i = 0; i < EC_MAXBUF; i++)
	{
		rxbufstat[i] = EC_BUF_EMPTY;
	}
}

/** Basic setup to connect the NIC to the port.
 * @param[in] port        = port context struct
 * @param[in] ifname      = name of the net device, f.e. "eth0"
 * @param[in] secondary   = if >0 then use secondary stack instead of primary
 * @return >0 if succeeded
 */
int ecx_setupnic (ecx_portt *port, const char *ifname, int secondary)
{
	assert (port != 0);

	if (secondary)
	{
		// line redundancy is not supported by this port
		return 0;
	}

	CNetDevice *pNetDevice = FindNetDevice (ifname);
	if (pNetDevice == 0)
	{
		return 0;
	}

	port->netdevice = pNetDevice;
	strncpy (port->ifname, ifname, sizeof port->ifname - 1);
	port->ifname[sizeof port->ifname - 1] = '\0';
	port->txframes = 0;
	port->txerrors = 0;
	port->rxdropped = 0;

	// the context has to be zeroed, before it is used for the first time, so
	// that the mutexes are created only once, when the port is set up again
	if (port->getindex_mutex == 0)
	{
		port->getindex_mutex = osal_mutex_create ();
	}

	if (port->tx_mutex == 0)
	{
		port->tx_mutex = osal_mutex_create ();
	}

	if (port->rx_mutex == 0)
	{
		port->rx_mutex = osal_mutex_create ();
	}

	port->sockhandle = -1;
	port->lastidx = 0;
	port->redstate = ECT_RED_NONE;
	port->stack.sock = &(port->sockhandle);
	port->stack.txbuf = &(port->txbuf);
	port->stack.txbuflength = &(port->txbuflength);
	port->stack.tempbuf = &(port->tempinbuf);
	port->stack.rxbuf = &(port->rxbuf);
	port->stack.rxbufstat = &(port->rxbufstat);
	port->stack.rxsa = &(port->rxsa);
	port->stack.rxcnt = 0;
	ecx_clear_rxbufstat (&(port->rxbufstat[0]));

	// setup ethernet headers in tx buffers, so we don't have to repeat it
	for (int i = 0; i < EC_MAXBUF; i++)
	{
		ec_setupheader (&(port->txbuf[i]));
		port->rxbufstat[i] = EC_BUF_EMPTY;
	}
	ec_setupheader (&(port->txbuf2));

	return 1;
}

/** Close the port
 * @param[in] port        = port context struct
 * @return 0
 */
int ecx_closenic (ecx_portt *port)
{
	assert (port != 0);

	if (port->getindex_mutex != 0)
	{
		osal_mutex_destroy (port->getindex_mutex);
		port->getindex_mutex = 0;
	}

	if (port->tx_mutex != 0)
	{
		osal_mutex_destroy (port->tx_mutex);
		port->tx_mutex = 0;
	}

	if (port->rx_mutex != 0)
	{
		osal_mutex_destroy (port->rx_mutex);
		port->rx_mutex = 0;
	}

	port->netdevice = 0;

	return 0;
}

/** Fill buffer with ethernet header structure.
 * Destination MAC is always broadcast.
 * Ethertype is always ETH_P_ECAT.
 * @param[out] p = buffer
 */
void ec_setupheader (void *p)
{
	assert (p != 0);
	ec_etherheadert *bp = (ec_etherheadert *) p;

	bp->da0 = oshw_htons (0xffff);
	bp->da1 = oshw_htons (0xffff);
	bp->da2 = oshw_htons (0xffff);
	bp->sa0 = oshw_htons (priMAC[0]);
	bp->sa1 = oshw_htons (priMAC[1]);
	bp->sa2 = oshw_htons (priMAC[2]);
	bp->etype = oshw_htons (ETH_P_ECAT);
}

/** Get new frame identifier index and allocate corresponding rx buffer.
 * @param[in] port        = port context struct
 * @return new index.
 */
uint8 ecx_getindex (ecx_portt *port)
{
	assert (port != 0);

	osal_mutex_lock (port->getindex_mutex);

	uint8 idx = port->lastidx + 1;
	// index can't be larger than buffer array
	if (idx >= EC_MAXBUF)
	{
		idx = 0;
	}

	// try to find unused index
	uint8 cnt = 0;
	while (   port->rxbufstat[idx] != EC_BUF_EMPTY
	       && cnt < EC_MAXBUF)
	{
		idx++;
		cnt++;

		if (idx >= EC_MAXBUF)
		{
			idx = 0;
		}
	}

	port->rxbufstat[idx] = EC_BUF_ALLOC;
	port->lastidx = idx;

	osal_mutex_unlock (port->getindex_mutex);

	return idx;
}

/** Set rx buffer status.
 * @param[in] port     = port context struct
 * @param[in] idx      = index in buffer array
 * @param[in] bufstat  = status to set
 */
void ecx_setbufstat (ecx_portt *port, uint8 idx, int bufstat)
{
	assert (port != 0);
	assert (idx < EC_MAXBUF);

	port->rxbufstat[idx] = bufstat;
}

/** Transmit buffer over the net device (non blocking).
 * @param[in] port        = port context struct
 * @param[in] idx         = index in tx buffer array
 * @param[in] stacknumber = 0=Primary 1=Secondary stack
 * @return number of bytes sent or -1
 */
int ecx_outframe (ecx_portt *port, uint8 idx, int stacknumber)
{
	assert (port != 0);
	assert (idx < EC_MAXBUF);
	(void) stacknumber;

	ec_stackT *stack = &(port->stack);

	int lp = (*stack->txbuflength)[idx];
	(*stack->rxbufstat)[idx] = EC_BUF_TX;

	return SendFrame (port, (*stack->txbuf)[idx], lp);
}

/** Transmit buffer over the net device (non blocking).
 * @param[in] port        = port context struct
 * @param[in] idx         = index in tx buffer array
 * @return number of bytes sent or -1
 */
int ecx_outframe_red (ecx_portt *port, uint8 idx)
{
	assert (port != 0);

	// rewrite MAC source address 1 to primary
	ec_etherheadert *ehp = (ec_etherheadert *) &(port->txbuf[idx]);
	ehp->sa1 = oshw_htons (priMAC[1]);

	return ecx_outframe (port, idx, 0);
}

/** Non blocking read of the net device. Put frame in temporary buffer.
 * @param[in] port        = port context struct
 * @param[in] stacknumber = 0=primary 1=secondary stack
 * @return >0 if frame is available and read
 */
static int ecx_recvpkt (ecx_portt *port, int stacknumber)
{
	assert (port != 0);
	(void) stacknumber;

	ec_stackT *stack = &(port->stack);

	port->tempinbufs = ReceiveFrame (port, stack->tempbuf);

	return port->tempinbufs > 0;
}

/** Non blocking receive frame function. Uses RX buffer and index to combine
 * read frame with transmitted frame. To compensate for received frames that
 * are out-of-order all frames are stored in their respective indexed buffer.
 *
 * @param[in] port        = port context struct
 * @param[in] idx         = requested index of frame
 * @param[in] stacknumber = 0=primary 1=secondary stack
 * @return Workcounter if a frame is found with corresponding index, otherwise
 * EC_NOFRAME or EC_OTHERFRAME.
 */
int ecx_inframe (ecx_portt *port, uint8 idx, int stacknumber)
{
	assert (port != 0);

	ec_stackT *stack = &(port->stack);
	int rval = EC_NOFRAME;
	ec_bufT *rxbuf = &(*stack->rxbuf)[idx];
	uint16 l;

	// check if requested index is already in buffer ?
	if (   idx < EC_MAXBUF
	    && (*stack->rxbufstat)[idx] == EC_BUF_RCVD)
	{
		l = (*rxbuf)[0] + ((uint16) ((*rxbuf)[1] & 0x0F) << 8);
		// return WKC
		rval = (*rxbuf)[l] + ((uint16) (*rxbuf)[l+1] << 8);
		// mark as completed
		(*stack->rxbufstat)[idx] = EC_BUF_COMPLETE;

		return rval;
	}

	osal_mutex_lock (port->rx_mutex);

	// check again, another task might have received it before we grabbed the mutex
	if (   idx < EC_MAXBUF
	    && (*stack->rxbufstat)[idx] == EC_BUF_RCVD)
	{
		l = (*rxbuf)[0] + ((uint16) ((*rxbuf)[1] & 0x0F) << 8);
		// return WKC
		rval = (*rxbuf)[l] + ((uint16) (*rxbuf)[l+1] << 8);
		// mark as completed
		(*stack->rxbufstat)[idx] = EC_BUF_COMPLETE;
	}
	// non blocking call to retrieve frame from the net device
	else if (ecx_recvpkt (port, stacknumber))
	{
		rval = EC_OTHERFRAME;

		ec_etherheadert *ehp = (ec_etherheadert *) (stack->tempbuf);
		// check if it is an EtherCAT frame
		if (ehp->etype == oshw_htons (ETH_P_ECAT))
		{
			stack->rxcnt++;

			ec_comt *ecp = (ec_comt *) (&(*stack->tempbuf)[ETH_HEADERSIZE]);
			l = etohs (ecp->elength) & 0x0FFF;
			uint8 idxf = ecp->index;

			// found index equals requested index ?
			if (idxf == idx)
			{
				// yes, put it in the buffer array (strip ethernet header)
				memcpy (rxbuf, &(*stack->tempbuf)[ETH_HEADERSIZE],
					(*stack->txbuflength)[idx] - ETH_HEADERSIZE);
				// return WKC
				rval = (*rxbuf)[l] + ((uint16) ((*rxbuf)[l+1]) << 8);
				// mark as completed
				(*stack->rxbufstat)[idx] = EC_BUF_COMPLETE;
				// store MAC source word 1 for redundant routing info
				(*stack->rxsa)[idx] = oshw_ntohs (ehp->sa1);
			}
			else
			{
				// check if index exist and someone is waiting for it
				if (   idxf < EC_MAXBUF
				    && (*stack->rxbufstat)[idxf] == EC_BUF_TX)
				{
					rxbuf = &(*stack->rxbuf)[idxf];
					// put it in the buffer array (strip ethernet header)
					memcpy (rxbuf, &(*stack->tempbuf)[ETH_HEADERSIZE],
						(*stack->txbuflength)[idxf] - ETH_HEADERSIZE);
					// mark as received
					(*stack->rxbufstat)[idxf] = EC_BUF_RCVD;
					(*stack->rxsa)[idxf] = oshw_ntohs (ehp->sa1);
				}
			}
		}
		else
		{
			// the net device is not used by an IP stack, so all other
			// frames (e.g. broadcasts) are dropped here
			port->rxdropped++;
		}
	}

	osal_mutex_unlock (port->rx_mutex);

	// WKC if matching frame found
	return rval;
}

/** Blocking receive frame function.
 * @param[in] port  = port context struct
 * @param[in] idx   = requested index of frame
 * @param[in] timer = absolute timeout time
 * @return Workcounter if a frame is found with corresponding index, otherwise
 * EC_NOFRAME.
 */
static int ecx_waitinframe_red (ecx_portt *port, uint8 idx, osal_timert timer)
{
	int wkc;

	do
	{
		wkc = ecx_inframe (port, idx, 0);
	}
	while (   wkc <= EC_NOFRAME
	       && osal_timer_is_expired (&timer) == FALSE);

	return wkc;
}

/** Blocking receive frame function. Calls ecx_waitinframe_red().
 * @param[in] port    = port context struct
 * @param[in] idx     = requested index of frame
 * @param[in] timeout = timeout in us
 * @return Workcounter if a frame is found with corresponding index, otherwise
 * EC_NOFRAME.
 */
int ecx_waitinframe (ecx_portt *port, uint8 idx, int timeout)
{
	osal_timert timer;

	osal_timer_start (&timer, timeout);

	return ecx_waitinframe_red (port, idx, timer);
}

/** Blocking send and receive frame function. Used for non processdata frames.
 * A datagram is build into a frame and transmitted via this function. It waits
 * for an answer and returns the workcounter. The function retries if time is
 * left and the result is WKC=0 or no frame received.
 *
 * @param[in] port    = port context struct
 * @param[in] idx     = index of frame
 * @param[in] timeout = timeout in us
 * @return Workcounter or EC_NOFRAME
 */
int ecx_srconfirm (ecx_portt *port, uint8 idx, int timeout)
{
	int wkc = EC_NOFRAME;
	osal_timert timer;

	osal_timer_start (&timer, timeout);

	do
	{
		osal_timert read_timer;

		ecx_outframe_red (port, idx);

		osal_timer_start (&read_timer, timeout < EC_TIMEOUTRET ? timeout : EC_TIMEOUTRET);

		wkc = ecx_waitinframe_red (port, idx, read_timer);
	}
	while (   wkc <= EC_NOFRAME
	       && osal_timer_is_expired (&timer) == FALSE);

	return wkc;
}

int ecx_linkup (ecx_portt *port)
{
	assert (port != 0);
	CNetDevice *pNetDevice = (CNetDevice *) port->netdevice;

	return    pNetDevice != 0
	       && pNetDevice->IsLinkUp ();
}

int ecx_updatephy (ecx_portt *port)
{
	assert (port != 0);
	CNetDevice *pNetDevice = (CNetDevice *) port->netdevice;

	return    pNetDevice != 0
	       && pNetDevice->UpdatePHY ();
}
