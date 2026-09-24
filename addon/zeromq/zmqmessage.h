//
// zmqmessage.h
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
#ifndef _zeromq_zmqmessage_h
#define _zeromq_zmqmessage_h

#include <circle/types.h>

#define ZMQ_MAX_FRAMES		16		///< Maximum number of frames per message

/// \brief A ZeroMQ multipart message (a sequence of 1..ZMQ_MAX_FRAMES frames)
///
/// For PUB/SUB, the first frame is used as the topic. Subscriptions are
/// matched as byte prefixes of this frame.
class CZMQMessage
{
public:
	CZMQMessage (void);
	~CZMQMessage (void);

	/// \brief Append a frame (the data is copied)
	/// \param pData Pointer to the frame data (may be 0, if nLength is 0)
	/// \param nLength Length of the frame in bytes (may be 0)
	/// \return FALSE, if the message has already ZMQ_MAX_FRAMES frames
	boolean AddFrame (const void *pData, size_t nLength);

	/// \brief Append a frame with a C string (without the terminating null byte)
	boolean AddFrame (const char *pString);

	/// \brief Append a frame, which buffer has been allocated with new u8[]
	/// \note The message takes over the ownership of the buffer, even on failure.
	boolean AddFrameOwned (u8 *pBuffer, size_t nLength);

	/// \return Number of frames in this message
	unsigned GetFrameCount (void) const;

	/// \param nIndex Index of the frame (0..GetFrameCount()-1)
	/// \return Pointer to the frame data (may be 0 for an empty frame)
	const u8 *GetFrame (unsigned nIndex) const;

	/// \param nIndex Index of the frame (0..GetFrameCount()-1)
	/// \return Length of the frame in bytes
	size_t GetFrameSize (unsigned nIndex) const;

	/// \return TRUE, if the frame has exactly the contents of the given C string
	boolean FrameEquals (unsigned nIndex, const char *pString) const;

	/// \brief Copy a frame to a buffer as a null-terminated C string
	/// \param nIndex Index of the frame
	/// \param pBuffer Destination buffer
	/// \param nBufferSize Size of pBuffer (the frame is truncated to nBufferSize-1 bytes)
	/// \return pBuffer
	char *GetFrameString (unsigned nIndex, char *pBuffer, size_t nBufferSize) const;

	/// \brief Remove all frames
	void Clear (void);

	/// \brief Exchange the contents of two messages (without copying the frames)
	void Swap (CZMQMessage &rOther);

private:
	CZMQMessage (const CZMQMessage &);		// not copyable
	CZMQMessage &operator= (const CZMQMessage &);

private:
	unsigned m_nFrames;
	u8	*m_pFrame[ZMQ_MAX_FRAMES];
	size_t	 m_nFrameSize[ZMQ_MAX_FRAMES];
};

/// \brief A set of subscription topics (byte string prefixes) with reference counts
///
/// Like libzmq, a topic which has been added twice must be removed twice.
/// An empty topic matches every message.
class CZMQTopicSet
{
public:
	CZMQTopicSet (void);
	~CZMQTopicSet (void);

	/// \brief Add a topic (increments its reference count, if it exists already)
	/// \param pbNew Set to TRUE, if the topic was not in the set before (optional)
	/// \return FALSE on insufficient memory
	boolean Add (const void *pTopic, size_t nLength, boolean *pbNew = 0);

	/// \brief Remove a topic (decrements its reference count, if it is > 1)
	/// \param pbDeleted Set to TRUE, if the topic has been deleted from the set (optional)
	/// \return FALSE, if the topic was not in the set
	boolean Remove (const void *pTopic, size_t nLength, boolean *pbDeleted = 0);

	/// \return TRUE, if one of the topics is a prefix of the given data
	boolean Matches (const void *pData, size_t nLength) const;

	/// \return Number of different topics in the set
	unsigned GetCount (void) const;

	/// \return Pointer to the topic with the given index (0..GetCount()-1)
	const u8 *GetTopic (unsigned nIndex, size_t *pLength) const;

	void Clear (void);

private:
	struct TTopic
	{
		TTopic	*pNext;
		unsigned nRefCount;
		size_t	 nLength;
		u8	*pData;
	};

	TTopic *Find (const void *pTopic, size_t nLength) const;

private:
	CZMQTopicSet (const CZMQTopicSet &);		// not copyable
	CZMQTopicSet &operator= (const CZMQTopicSet &);

private:
	TTopic	*m_pFirst;
	unsigned m_nCount;
};

#endif
