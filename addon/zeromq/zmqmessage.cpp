//
// zmqmessage.cpp
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
#include <zeromq/zmqmessage.h>
#include <circle/util.h>
#include <assert.h>

CZMQMessage::CZMQMessage (void)
:	m_nFrames (0)
{
}

CZMQMessage::~CZMQMessage (void)
{
	Clear ();
}

boolean CZMQMessage::AddFrame (const void *pData, size_t nLength)
{
	if (m_nFrames >= ZMQ_MAX_FRAMES)
	{
		return FALSE;
	}

	u8 *pBuffer = 0;
	if (nLength > 0)
	{
		assert (pData != 0);

		pBuffer = new u8[nLength];
		if (pBuffer == 0)
		{
			return FALSE;
		}

		memcpy (pBuffer, pData, nLength);
	}

	return AddFrameOwned (pBuffer, nLength);
}

boolean CZMQMessage::AddFrame (const char *pString)
{
	assert (pString != 0);

	return AddFrame (pString, strlen (pString));
}

boolean CZMQMessage::AddFrameOwned (u8 *pBuffer, size_t nLength)
{
	if (m_nFrames >= ZMQ_MAX_FRAMES)
	{
		delete [] pBuffer;

		return FALSE;
	}

	m_pFrame[m_nFrames] = pBuffer;
	m_nFrameSize[m_nFrames] = nLength;
	m_nFrames++;

	return TRUE;
}

unsigned CZMQMessage::GetFrameCount (void) const
{
	return m_nFrames;
}

const u8 *CZMQMessage::GetFrame (unsigned nIndex) const
{
	assert (nIndex < m_nFrames);

	return m_pFrame[nIndex];
}

size_t CZMQMessage::GetFrameSize (unsigned nIndex) const
{
	assert (nIndex < m_nFrames);

	return m_nFrameSize[nIndex];
}

boolean CZMQMessage::FrameEquals (unsigned nIndex, const char *pString) const
{
	assert (pString != 0);

	if (nIndex >= m_nFrames)
	{
		return FALSE;
	}

	size_t nLength = strlen (pString);

	return    m_nFrameSize[nIndex] == nLength
	       && (   nLength == 0
		   || memcmp (m_pFrame[nIndex], pString, nLength) == 0);
}

char *CZMQMessage::GetFrameString (unsigned nIndex, char *pBuffer, size_t nBufferSize) const
{
	assert (pBuffer != 0);
	assert (nBufferSize > 0);

	size_t nLength = 0;
	if (nIndex < m_nFrames)
	{
		nLength = m_nFrameSize[nIndex];
		if (nLength > nBufferSize-1)
		{
			nLength = nBufferSize-1;
		}

		if (nLength > 0)
		{
			memcpy (pBuffer, m_pFrame[nIndex], nLength);
		}
	}

	pBuffer[nLength] = '\0';

	return pBuffer;
}

void CZMQMessage::Clear (void)
{
	for (unsigned i = 0; i < m_nFrames; i++)
	{
		delete [] m_pFrame[i];
		m_pFrame[i] = 0;
	}

	m_nFrames = 0;
}

void CZMQMessage::Swap (CZMQMessage &rOther)
{
	unsigned nFrames = m_nFrames > rOther.m_nFrames ? m_nFrames : rOther.m_nFrames;
	for (unsigned i = 0; i < nFrames; i++)
	{
		u8 *pFrame = m_pFrame[i];
		m_pFrame[i] = rOther.m_pFrame[i];
		rOther.m_pFrame[i] = pFrame;

		size_t nFrameSize = m_nFrameSize[i];
		m_nFrameSize[i] = rOther.m_nFrameSize[i];
		rOther.m_nFrameSize[i] = nFrameSize;
	}

	unsigned nTemp = m_nFrames;
	m_nFrames = rOther.m_nFrames;
	rOther.m_nFrames = nTemp;
}

CZMQTopicSet::CZMQTopicSet (void)
:	m_pFirst (0),
	m_nCount (0)
{
}

CZMQTopicSet::~CZMQTopicSet (void)
{
	Clear ();
}

boolean CZMQTopicSet::Add (const void *pTopic, size_t nLength, boolean *pbNew)
{
	if (pbNew != 0)
	{
		*pbNew = FALSE;
	}

	TTopic *pEntry = Find (pTopic, nLength);
	if (pEntry != 0)
	{
		pEntry->nRefCount++;

		return TRUE;
	}

	pEntry = new TTopic;
	if (pEntry == 0)
	{
		return FALSE;
	}

	pEntry->pData = 0;
	if (nLength > 0)
	{
		assert (pTopic != 0);

		pEntry->pData = new u8[nLength];
		if (pEntry->pData == 0)
		{
			delete pEntry;

			return FALSE;
		}

		memcpy (pEntry->pData, pTopic, nLength);
	}

	pEntry->nRefCount = 1;
	pEntry->nLength = nLength;
	pEntry->pNext = m_pFirst;
	m_pFirst = pEntry;
	m_nCount++;

	if (pbNew != 0)
	{
		*pbNew = TRUE;
	}

	return TRUE;
}

boolean CZMQTopicSet::Remove (const void *pTopic, size_t nLength, boolean *pbDeleted)
{
	if (pbDeleted != 0)
	{
		*pbDeleted = FALSE;
	}

	TTopic *pPrev = 0;
	for (TTopic *pEntry = m_pFirst; pEntry != 0; pPrev = pEntry, pEntry = pEntry->pNext)
	{
		if (   pEntry->nLength != nLength
		    || (   nLength > 0
			&& memcmp (pEntry->pData, pTopic, nLength) != 0))
		{
			continue;
		}

		assert (pEntry->nRefCount > 0);
		if (--pEntry->nRefCount > 0)
		{
			return TRUE;
		}

		if (pPrev == 0)
		{
			m_pFirst = pEntry->pNext;
		}
		else
		{
			pPrev->pNext = pEntry->pNext;
		}

		delete [] pEntry->pData;
		delete pEntry;

		assert (m_nCount > 0);
		m_nCount--;

		if (pbDeleted != 0)
		{
			*pbDeleted = TRUE;
		}

		return TRUE;
	}

	return FALSE;
}

boolean CZMQTopicSet::Matches (const void *pData, size_t nLength) const
{
	for (const TTopic *pEntry = m_pFirst; pEntry != 0; pEntry = pEntry->pNext)
	{
		if (pEntry->nLength == 0)
		{
			return TRUE;
		}

		if (   pEntry->nLength <= nLength
		    && memcmp (pEntry->pData, pData, pEntry->nLength) == 0)
		{
			return TRUE;
		}
	}

	return FALSE;
}

unsigned CZMQTopicSet::GetCount (void) const
{
	return m_nCount;
}

const u8 *CZMQTopicSet::GetTopic (unsigned nIndex, size_t *pLength) const
{
	assert (pLength != 0);

	const TTopic *pEntry = m_pFirst;
	for (unsigned i = 0; pEntry != 0 && i < nIndex; i++)
	{
		pEntry = pEntry->pNext;
	}

	assert (pEntry != 0);
	*pLength = pEntry->nLength;

	return pEntry->pData;
}

void CZMQTopicSet::Clear (void)
{
	while (m_pFirst != 0)
	{
		TTopic *pNext = m_pFirst->pNext;

		delete [] m_pFirst->pData;
		delete m_pFirst;

		m_pFirst = pNext;
	}

	m_nCount = 0;
}

CZMQTopicSet::TTopic *CZMQTopicSet::Find (const void *pTopic, size_t nLength) const
{
	for (TTopic *pEntry = m_pFirst; pEntry != 0; pEntry = pEntry->pNext)
	{
		if (   pEntry->nLength == nLength
		    && (   nLength == 0
			|| memcmp (pEntry->pData, pTopic, nLength) == 0))
		{
			return pEntry;
		}
	}

	return 0;
}
