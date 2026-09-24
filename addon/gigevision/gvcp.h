//
// gvcp.h
//
// GigE Vision Control Protocol (GVCP) and Stream Protocol (GVSP) definitions
//
// Circle - A C++ bare metal environment for Raspberry Pi
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
#ifndef _gigevision_gvcp_h
#define _gigevision_gvcp_h

#include <circle/macros.h>
#include <circle/types.h>

// All multi-byte fields on the wire are big endian (network byte order).

#define GVCP_PORT			3956

#define GVCP_KEY			0x42		// first byte of every command

// Command flags
#define GVCP_FLAG_ACK_REQUIRED		0x01
#define GVCP_FLAG_DISCOVERY_BROADCAST_ACK 0x10	// DISCOVERY_CMD: device may broadcast the ACK
#define GVCP_FLAG_EXTENDED_ID		0x10	// PACKETRESEND_CMD: 64-bit block ID

// Commands and acknowledges
#define GVCP_DISCOVERY_CMD		0x0002
#define GVCP_DISCOVERY_ACK		0x0003
#define GVCP_FORCEIP_CMD		0x0004
#define GVCP_FORCEIP_ACK		0x0005
#define GVCP_PACKETRESEND_CMD		0x0040
#define GVCP_READREG_CMD		0x0080
#define GVCP_READREG_ACK		0x0081
#define GVCP_WRITEREG_CMD		0x0082
#define GVCP_WRITEREG_ACK		0x0083
#define GVCP_READMEM_CMD		0x0084
#define GVCP_READMEM_ACK		0x0085
#define GVCP_WRITEMEM_CMD		0x0086
#define GVCP_WRITEMEM_ACK		0x0087
#define GVCP_PENDING_ACK		0x0089
#define GVCP_EVENT_CMD			0x00C0
#define GVCP_EVENT_ACK			0x00C1
#define GVCP_EVENTDATA_CMD		0x00C2
#define GVCP_EVENTDATA_ACK		0x00C3
#define GVCP_ACTION_CMD			0x0100
#define GVCP_ACTION_ACK			0x0101

// Status codes
#define GVCP_STATUS_SUCCESS			0x0000
#define GVCP_STATUS_PACKET_RESEND		0x0100
#define GVCP_STATUS_NOT_IMPLEMENTED		0x8001
#define GVCP_STATUS_INVALID_PARAMETER		0x8002
#define GVCP_STATUS_INVALID_ADDRESS		0x8003
#define GVCP_STATUS_WRITE_PROTECT		0x8004
#define GVCP_STATUS_BAD_ALIGNMENT		0x8005
#define GVCP_STATUS_ACCESS_DENIED		0x8006
#define GVCP_STATUS_BUSY			0x8007
#define GVCP_STATUS_PACKET_UNAVAILABLE		0x800B
#define GVCP_STATUS_DATA_OVERRUN		0x800C
#define GVCP_STATUS_INVALID_HEADER		0x800D
#define GVCP_STATUS_PACKET_NOT_YET_AVAILABLE	0x800F
#define GVCP_STATUS_PACKET_AND_PREV_REMOVED	0x8010
#define GVCP_STATUS_PACKET_REMOVED		0x8011
#define GVCP_STATUS_NO_REF_TIME			0x8012
#define GVCP_STATUS_PACKET_TEMP_UNAVAILABLE	0x8013
#define GVCP_STATUS_OVERFLOW			0x8014
#define GVCP_STATUS_ACTION_LATE			0x8015
#define GVCP_STATUS_ERROR			0x8FFF

// Limits
#define GVCP_MAX_PAYLOAD		540	// max. payload of a GVCP packet (576 - IP - UDP - header)
#define GVCP_MAX_READMEM		536	// max. data bytes in one READMEM/WRITEMEM
#define GVCP_MAX_READREG		135	// max. registers in one READREG
#define GVCP_MAX_WRITEREG		67	// max. registers in one WRITEREG

// Bootstrap registers
#define GEV_REG_VERSION			0x0000
#define GEV_REG_DEVICE_MODE		0x0004
#define GEV_REG_MAC_HIGH		0x0008
#define GEV_REG_MAC_LOW			0x000C
#define GEV_REG_IP_CONFIG_OPTIONS	0x0010
#define GEV_REG_IP_CONFIG_CURRENT	0x0014
#define GEV_REG_CURRENT_IP		0x0024
#define GEV_REG_CURRENT_SUBNET		0x0034
#define GEV_REG_CURRENT_GATEWAY		0x0044
#define GEV_REG_MANUFACTURER_NAME	0x0048		// 32 bytes
#define GEV_REG_MODEL_NAME		0x0068		// 32 bytes
#define GEV_REG_DEVICE_VERSION		0x0088		// 32 bytes
#define GEV_REG_MANUFACTURER_INFO	0x00A8		// 48 bytes
#define GEV_REG_SERIAL_NUMBER		0x00D8		// 16 bytes
#define GEV_REG_USER_NAME		0x00E8		// 16 bytes
#define GEV_REG_FIRST_URL		0x0200		// 512 bytes
#define GEV_REG_SECOND_URL		0x0400		// 512 bytes
#define GEV_REG_NUM_INTERFACES		0x0600
#define GEV_REG_PERSISTENT_IP		0x064C
#define GEV_REG_PERSISTENT_SUBNET	0x065C
#define GEV_REG_PERSISTENT_GATEWAY	0x066C
#define GEV_REG_NUM_MESSAGE_CHANNELS	0x0900
#define GEV_REG_NUM_STREAM_CHANNELS	0x0904
#define GEV_REG_NUM_ACTION_SIGNALS	0x0908
#define GEV_REG_GVCP_CAPABILITY		0x0934
#define GEV_REG_HEARTBEAT_TIMEOUT	0x0938		// ms
#define GEV_REG_TIMESTAMP_FREQ_HIGH	0x093C
#define GEV_REG_TIMESTAMP_FREQ_LOW	0x0940
#define GEV_REG_TIMESTAMP_CONTROL	0x0944
#define GEV_REG_TIMESTAMP_VALUE_HIGH	0x0948
#define GEV_REG_TIMESTAMP_VALUE_LOW	0x094C
#define GEV_REG_GVCP_CONFIG		0x0954
#define GEV_REG_PENDING_TIMEOUT		0x0958
#define GEV_REG_CCP			0x0A00		// control channel privilege
#define GEV_REG_MCP			0x0B00		// message channel port
#define GEV_REG_MCDA			0x0B10		// message channel destination address
#define GEV_REG_MCTT			0x0B14		// message channel transmission timeout
#define GEV_REG_MCRC			0x0B18		// message channel retry count

// Stream channel registers (n = channel index)
#define GEV_REG_SCP(n)			(0x0D00 + 0x40*(n))	// port (host port in bits 15..0)
#define GEV_REG_SCPS(n)			(0x0D04 + 0x40*(n))	// packet size
#define GEV_REG_SCPD(n)			(0x0D08 + 0x40*(n))	// packet delay
#define GEV_REG_SCDA(n)			(0x0D18 + 0x40*(n))	// destination address
#define GEV_REG_SCSP(n)			(0x0D1C + 0x40*(n))	// source port
#define GEV_REG_SCC(n)			(0x0D20 + 0x40*(n))	// capability
#define GEV_REG_SCCFG(n)		(0x0D24 + 0x40*(n))	// configuration

// CCP bits
#define GEV_CCP_EXCLUSIVE_ACCESS	(1 << 0)
#define GEV_CCP_CONTROL_ACCESS		(1 << 1)
#define GEV_CCP_SWITCHOVER_ENABLE	(1 << 2)

// SCPS bits
#define GEV_SCPS_FIRE_TEST_PACKET	(1U << 31)
#define GEV_SCPS_DO_NOT_FRAGMENT	(1U << 30)
#define GEV_SCPS_BIG_ENDIAN		(1U << 29)
#define GEV_SCPS_PACKET_SIZE_MASK	0xFFFF

// GVCP capability bits (GEV_REG_GVCP_CAPABILITY)
#define GEV_CAP_WRITEMEM		(1 << 1)
#define GEV_CAP_PACKETRESEND		(1 << 2)
#define GEV_CAP_EVENT			(1 << 3)
#define GEV_CAP_EVENTDATA		(1 << 4)
#define GEV_CAP_PENDING_ACK		(1 << 5)
#define GEV_CAP_ACTION			(1 << 6)
#define GEV_CAP_EXTENDED_STATUS_1	(1 << 8)
#define GEV_CAP_EXTENDED_STATUS_2	(1 << 9)
#define GEV_CAP_HEARTBEAT_DISABLE	(1 << 29)
#define GEV_CAP_SERIAL_NUMBER		(1 << 30)
#define GEV_CAP_USER_NAME		(1U << 31)

// GVCP header of a command (8 bytes)
struct TGVCPCommandHeader
{
	u8	Key;			// GVCP_KEY
	u8	Flags;
	u16	Command;
	u16	Length;			// payload length in bytes
	u16	ReqID;			// != 0
}
PACKED;

// GVCP header of an acknowledge (8 bytes)
struct TGVCPAckHeader
{
	u16	Status;
	u16	Acknowledge;
	u16	Length;			// payload length in bytes
	u16	AckID;
}
PACKED;

// Payload of DISCOVERY_ACK (248 bytes, same layout as bootstrap registers 0x0000..0x00F7)
struct TGVCPDiscoveryAck
{
	u16	SpecVersionMajor;
	u16	SpecVersionMinor;
	u32	DeviceMode;
	u16	Reserved1;
	u16	MACHigh;
	u32	MACLow;
	u32	IPConfigOptions;
	u32	IPConfigCurrent;
	u8	Reserved2[12];
	u32	CurrentIP;
	u8	Reserved3[12];
	u32	SubnetMask;
	u8	Reserved4[12];
	u32	DefaultGateway;
	char	ManufacturerName[32];
	char	ModelName[32];
	char	DeviceVersion[32];
	char	ManufacturerInfo[48];
	char	SerialNumber[16];
	char	UserDefinedName[16];
}
PACKED;

// Payload of FORCEIP_CMD (56 bytes)
struct TGVCPForceIPCmd
{
	u16	Reserved1;
	u16	MACHigh;
	u32	MACLow;
	u8	Reserved2[12];
	u32	StaticIP;
	u8	Reserved3[12];
	u32	SubnetMask;
	u8	Reserved4[12];
	u32	DefaultGateway;
}
PACKED;

// Payload of PACKETRESEND_CMD (standard ID, 12 bytes)
struct TGVCPPacketResendCmd
{
	u16	StreamChannel;
	u16	BlockID;
	u32	FirstPacketID;		// bits 23..0
	u32	LastPacketID;		// bits 23..0
}
PACKED;

// Payload of PENDING_ACK
struct TGVCPPendingAck
{
	u16	Reserved;
	u16	TimeToCompletion;	// ms
}
PACKED;

////////////////////////////////////////////////////////////////////////////////
// GVSP (stream protocol)

// Packet formats
#define GVSP_FORMAT_LEADER		1
#define GVSP_FORMAT_TRAILER		2
#define GVSP_FORMAT_PAYLOAD		3
#define GVSP_FORMAT_ALL_IN		4
#define GVSP_FORMAT_H264		5
#define GVSP_FORMAT_MULTIZONE		6
#define GVSP_FORMAT_MULTIPART		7
#define GVSP_FORMAT_GENDC		8

// Payload types
#define GVSP_PAYLOAD_IMAGE		0x0001
#define GVSP_PAYLOAD_RAWDATA		0x0002
#define GVSP_PAYLOAD_FILE		0x0003
#define GVSP_PAYLOAD_CHUNK_DATA		0x0004
#define GVSP_PAYLOAD_EXT_CHUNK_DATA	0x0005	// deprecated
#define GVSP_PAYLOAD_JPEG		0x0006
#define GVSP_PAYLOAD_JPEG2000		0x0007
#define GVSP_PAYLOAD_H264		0x0008
#define GVSP_PAYLOAD_MULTIZONE_IMAGE	0x0009
#define GVSP_PAYLOAD_MULTIPART		0x000A
#define GVSP_PAYLOAD_EXTENDED_CHUNK	0x4000	// flag: chunk data follows the payload

#define GVSP_HEADER_SIZE		8
#define GVSP_EXT_HEADER_SIZE		20

// Standard GVSP header (8 bytes)
struct TGVSPHeader
{
	u16	Status;
	u16	BlockID;		// 16-bit block ID (0 is invalid)
	u32	FormatAndPacketID;	// bit 31: EI, bits 27..24: format, bits 23..0: packet ID
}
PACKED;

// Extended ID GVSP header (20 bytes, EI == 1)
struct TGVSPExtHeader
{
	u16	Status;
	u16	Flags;
	u32	Format;			// bit 31: EI, bits 27..24: format
	u32	BlockIDHigh;
	u32	BlockIDLow;
	u32	PacketID;
}
PACKED;

#define GVSP_EI_FLAG			(1U << 31)
#define GVSP_FORMAT(word)		(((word) >> 24) & 0x0F)
#define GVSP_PACKET_ID(word)		((word) & 0xFFFFFF)

// Image leader (follows the GVSP header)
struct TGVSPImageLeader
{
	u16	FieldInfo;
	u16	PayloadType;
	u32	TimestampHigh;
	u32	TimestampLow;
	u32	PixelFormat;
	u32	SizeX;
	u32	SizeY;
	u32	OffsetX;
	u32	OffsetY;
	u16	PaddingX;
	u16	PaddingY;
}
PACKED;

// Generic leader (first fields common to all payload types)
struct TGVSPGenericLeader
{
	u16	FieldInfo;
	u16	PayloadType;
	u32	TimestampHigh;
	u32	TimestampLow;
}
PACKED;

// Trailer (follows the GVSP header)
struct TGVSPTrailer
{
	u16	Reserved;
	u16	PayloadType;
	u32	SizeY;			// image payload only: number of lines actually sent
}
PACKED;

// Pixel formats (PFNC) frequently used with GigE Vision
#define GVSP_PIX_MONO8			0x01080001
#define GVSP_PIX_MONO10			0x01100003
#define GVSP_PIX_MONO10_PACKED		0x010C0004
#define GVSP_PIX_MONO12			0x01100005
#define GVSP_PIX_MONO12_PACKED		0x010C0006
#define GVSP_PIX_MONO16			0x01100007
#define GVSP_PIX_BAYER_GR8		0x01080008
#define GVSP_PIX_BAYER_RG8		0x01080009
#define GVSP_PIX_BAYER_GB8		0x0108000A
#define GVSP_PIX_BAYER_BG8		0x0108000B
#define GVSP_PIX_RGB8			0x02180014
#define GVSP_PIX_BGR8			0x02180015
#define GVSP_PIX_YUV422_8_UYVY		0x0210001F
#define GVSP_PIX_YUV422_8		0x02100032

#define GVSP_PIX_BITS_PER_PIXEL(fmt)	(((fmt) >> 16) & 0xFF)

#endif
