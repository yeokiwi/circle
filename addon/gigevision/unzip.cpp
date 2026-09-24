//
// unzip.cpp
//
// Minimal ZIP archive extraction with DEFLATE decompressor
// (GenICam device description files are often stored zipped in the device)
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
#include <gigevision/unzip.h>
#include <circle/util.h>
#include <assert.h>

#define ZIP_LOCAL_HEADER_SIG	0x04034B50
#define ZIP_CENTRAL_HEADER_SIG	0x02014B50
#define ZIP_END_OF_CENTRAL_SIG	0x06054B50

#define ZIP_METHOD_STORED	0
#define ZIP_METHOD_DEFLATED	8

#define MAX_BITS		15
#define MAX_LIT_CODES		288
#define MAX_DIST_CODES		30

static u16 GetLE16 (const u8 *p)
{
	return p[0] | (u16) p[1] << 8;
}

static u32 GetLE32 (const u8 *p)
{
	return p[0] | (u32) p[1] << 8 | (u32) p[2] << 16 | (u32) p[3] << 24;
}

////////////////////////////////////////////////////////////////////////////////
// DEFLATE decompressor (canonical Huffman decoding, after the method of "puff")

struct THuffman
{
	u16 Count[MAX_BITS+1];		// number of codes of each length
	u16 Symbol[MAX_LIT_CODES];	// symbols ordered by code
};

struct TInflateState
{
	const u8 *pIn;
	size_t nInSize;
	size_t nInPos;
	u32 nBitBuf;
	unsigned nBitCount;

	u8 *pOut;
	size_t nOutSize;
	size_t nOutPos;

	boolean bError;
};

static int GetBits (TInflateState *s, unsigned nBits)
{
	u32 nValue = s->nBitBuf;
	while (s->nBitCount < nBits)
	{
		if (s->nInPos >= s->nInSize)
		{
			s->bError = TRUE;

			return 0;
		}

		nValue |= (u32) s->pIn[s->nInPos++] << s->nBitCount;
		s->nBitCount += 8;
	}

	s->nBitBuf = nValue >> nBits;
	s->nBitCount -= nBits;

	return (int) (nValue & ((1UL << nBits) - 1));
}

static int Decode (TInflateState *s, const THuffman *h)
{
	int nCode = 0;
	int nFirst = 0;
	int nIndex = 0;

	for (unsigned nLen = 1; nLen <= MAX_BITS; nLen++)
	{
		nCode |= GetBits (s, 1);
		if (s->bError)
		{
			return -1;
		}

		int nCount = h->Count[nLen];
		if (nCode - nCount < nFirst)
		{
			return h->Symbol[nIndex + (nCode - nFirst)];
		}

		nIndex += nCount;
		nFirst += nCount;
		nFirst <<= 1;
		nCode <<= 1;
	}

	return -1;		// ran out of codes
}

// returns 0 for a complete code, < 0 for an over-subscribed code, > 0 for incomplete
static int Construct (THuffman *h, const u8 *pLength, unsigned nCodes)
{
	for (unsigned nLen = 0; nLen <= MAX_BITS; nLen++)
	{
		h->Count[nLen] = 0;
	}

	for (unsigned nSymbol = 0; nSymbol < nCodes; nSymbol++)
	{
		h->Count[pLength[nSymbol]]++;
	}

	if (h->Count[0] == nCodes)
	{
		return 0;		// no codes, complete but decoding will fail
	}

	int nLeft = 1;
	for (unsigned nLen = 1; nLen <= MAX_BITS; nLen++)
	{
		nLeft <<= 1;
		nLeft -= h->Count[nLen];
		if (nLeft < 0)
		{
			return nLeft;
		}
	}

	u16 Offs[MAX_BITS+1];
	Offs[1] = 0;
	for (unsigned nLen = 1; nLen < MAX_BITS; nLen++)
	{
		Offs[nLen + 1] = Offs[nLen] + h->Count[nLen];
	}

	for (unsigned nSymbol = 0; nSymbol < nCodes; nSymbol++)
	{
		if (pLength[nSymbol] != 0)
		{
			h->Symbol[Offs[pLength[nSymbol]]++] = nSymbol;
		}
	}

	return nLeft;
}

static boolean Codes (TInflateState *s, const THuffman *pLenCode, const THuffman *pDistCode)
{
	static const u16 LengthBase[29] =
	{
		3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
		35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
	};
	static const u8 LengthExtra[29] =
	{
		0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
		3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
	};
	static const u16 DistBase[30] =
	{
		1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
		257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
	};
	static const u8 DistExtra[30] =
	{
		0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
		7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
	};

	for (;;)
	{
		int nSymbol = Decode (s, pLenCode);
		if (nSymbol < 0)
		{
			return FALSE;
		}

		if (nSymbol < 256)
		{
			if (s->nOutPos >= s->nOutSize)
			{
				return FALSE;
			}

			s->pOut[s->nOutPos++] = (u8) nSymbol;
		}
		else if (nSymbol == 256)
		{
			return TRUE;		// end of block
		}
		else
		{
			nSymbol -= 257;
			if (nSymbol >= 29)
			{
				return FALSE;
			}

			unsigned nLength = LengthBase[nSymbol] + GetBits (s, LengthExtra[nSymbol]);

			nSymbol = Decode (s, pDistCode);
			if (   nSymbol < 0
			    || nSymbol >= 30)
			{
				return FALSE;
			}

			size_t nDist = DistBase[nSymbol] + GetBits (s, DistExtra[nSymbol]);
			if (s->bError)
			{
				return FALSE;
			}

			if (   nDist > s->nOutPos
			    || nLength > s->nOutSize - s->nOutPos)
			{
				return FALSE;
			}

			// may overlap, so copy byte by byte
			for (u8 *pTo = s->pOut + s->nOutPos; nLength--; pTo++)
			{
				*pTo = *(pTo - nDist);
				s->nOutPos++;
			}
		}
	}
}

static boolean Stored (TInflateState *s)
{
	s->nBitBuf = 0;			// go to byte boundary
	s->nBitCount = 0;

	if (s->nInPos + 4 > s->nInSize)
	{
		return FALSE;
	}

	unsigned nLength = GetLE16 (s->pIn + s->nInPos);
	unsigned nComplement = GetLE16 (s->pIn + s->nInPos + 2);
	s->nInPos += 4;

	if (nLength != (~nComplement & 0xFFFF))
	{
		return FALSE;
	}

	if (   s->nInPos + nLength > s->nInSize
	    || s->nOutPos + nLength > s->nOutSize)
	{
		return FALSE;
	}

	memcpy (s->pOut + s->nOutPos, s->pIn + s->nInPos, nLength);
	s->nInPos += nLength;
	s->nOutPos += nLength;

	return TRUE;
}

static boolean Fixed (TInflateState *s)
{
	static THuffman LenCode, DistCode;
	static boolean bInitialized = FALSE;

	if (!bInitialized)
	{
		u8 Lengths[MAX_LIT_CODES];
		unsigned nSymbol;
		for (nSymbol = 0; nSymbol < 144; nSymbol++)	Lengths[nSymbol] = 8;
		for (; nSymbol < 256; nSymbol++)		Lengths[nSymbol] = 9;
		for (; nSymbol < 280; nSymbol++)		Lengths[nSymbol] = 7;
		for (; nSymbol < MAX_LIT_CODES; nSymbol++)	Lengths[nSymbol] = 8;
		Construct (&LenCode, Lengths, MAX_LIT_CODES);

		for (nSymbol = 0; nSymbol < MAX_DIST_CODES; nSymbol++)
		{
			Lengths[nSymbol] = 5;
		}
		Construct (&DistCode, Lengths, MAX_DIST_CODES);

		bInitialized = TRUE;
	}

	return Codes (s, &LenCode, &DistCode);
}

static boolean Dynamic (TInflateState *s)
{
	static const u8 Order[19] =
	{
		16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
	};

	unsigned nLen = GetBits (s, 5) + 257;
	unsigned nDist = GetBits (s, 5) + 1;
	unsigned nCode = GetBits (s, 4) + 4;
	if (   s->bError
	    || nLen > MAX_LIT_CODES
	    || nDist > MAX_DIST_CODES)
	{
		return FALSE;
	}

	u8 Lengths[MAX_LIT_CODES + MAX_DIST_CODES];

	unsigned nIndex;
	for (nIndex = 0; nIndex < nCode; nIndex++)
	{
		Lengths[Order[nIndex]] = GetBits (s, 3);
	}
	for (; nIndex < 19; nIndex++)
	{
		Lengths[Order[nIndex]] = 0;
	}

	if (s->bError)
	{
		return FALSE;
	}

	THuffman LenCode, DistCode;
	if (Construct (&LenCode, Lengths, 19) != 0)
	{
		return FALSE;		// code lengths code must be complete
	}

	nIndex = 0;
	while (nIndex < nLen + nDist)
	{
		int nSymbol = Decode (s, &LenCode);
		if (nSymbol < 0)
		{
			return FALSE;
		}

		if (nSymbol < 16)
		{
			Lengths[nIndex++] = (u8) nSymbol;

			continue;
		}

		u8 nRepeatLength = 0;
		unsigned nRepeat;
		if (nSymbol == 16)
		{
			if (nIndex == 0)
			{
				return FALSE;
			}

			nRepeatLength = Lengths[nIndex - 1];
			nRepeat = 3 + GetBits (s, 2);
		}
		else if (nSymbol == 17)
		{
			nRepeat = 3 + GetBits (s, 3);
		}
		else
		{
			nRepeat = 11 + GetBits (s, 7);
		}

		if (   s->bError
		    || nIndex + nRepeat > nLen + nDist)
		{
			return FALSE;
		}

		while (nRepeat--)
		{
			Lengths[nIndex++] = nRepeatLength;
		}
	}

	if (Lengths[256] == 0)
	{
		return FALSE;		// no end-of-block code
	}

	int nErr = Construct (&LenCode, Lengths, nLen);
	if (   nErr < 0
	    || (nErr > 0 && nLen - LenCode.Count[0] != 1))
	{
		return FALSE;		// incomplete code only allowed for a single length
	}

	nErr = Construct (&DistCode, Lengths + nLen, nDist);
	if (   nErr < 0
	    || (nErr > 0 && nDist - DistCode.Count[0] != 1))
	{
		return FALSE;
	}

	return Codes (s, &LenCode, &DistCode);
}

long CUnzip::Inflate (const u8 *pSource, size_t nSourceSize, u8 *pDest, size_t nDestSize)
{
	assert (pSource != 0);
	assert (pDest != 0);

	TInflateState State;
	State.pIn = pSource;
	State.nInSize = nSourceSize;
	State.nInPos = 0;
	State.nBitBuf = 0;
	State.nBitCount = 0;
	State.pOut = pDest;
	State.nOutSize = nDestSize;
	State.nOutPos = 0;
	State.bError = FALSE;

	int nLast;
	do
	{
		nLast = GetBits (&State, 1);
		int nType = GetBits (&State, 2);
		if (State.bError)
		{
			return -1;
		}

		boolean bOK;
		switch (nType)
		{
		case 0:		bOK = Stored (&State);		break;
		case 1:		bOK = Fixed (&State);		break;
		case 2:		bOK = Dynamic (&State);		break;
		default:	bOK = FALSE;			break;
		}

		if (   !bOK
		    || State.bError)
		{
			return -1;
		}
	}
	while (!nLast);

	return (long) State.nOutPos;
}

////////////////////////////////////////////////////////////////////////////////
// ZIP archive

boolean CUnzip::Extract (const u8 *pArchive, size_t nArchiveSize, const char *pExtension,
			 u8 **ppData, size_t *pSize)
{
	assert (pArchive != 0);
	assert (ppData != 0);
	assert (pSize != 0);

	// find "end of central directory" record (22 bytes + comment of up to 64K)
	if (nArchiveSize < 22)
	{
		return FALSE;
	}

	size_t nEOCD = nArchiveSize - 22;
	size_t nLowest = nArchiveSize > 22 + 0xFFFF ? nArchiveSize - 22 - 0xFFFF : 0;
	while (GetLE32 (pArchive + nEOCD) != ZIP_END_OF_CENTRAL_SIG)
	{
		if (nEOCD == nLowest)
		{
			return FALSE;
		}

		nEOCD--;
	}

	unsigned nEntries = GetLE16 (pArchive + nEOCD + 10);
	size_t nCentral = GetLE32 (pArchive + nEOCD + 16);

	size_t nExtLen = pExtension != 0 ? strlen (pExtension) : 0;

	for (unsigned i = 0; i < nEntries; i++)
	{
		if (   nCentral + 46 > nArchiveSize
		    || GetLE32 (pArchive + nCentral) != ZIP_CENTRAL_HEADER_SIG)
		{
			return FALSE;
		}

		const u8 *pEntry = pArchive + nCentral;
		unsigned nMethod = GetLE16 (pEntry + 10);
		u32 nCRC = GetLE32 (pEntry + 16);
		size_t nCompSize = GetLE32 (pEntry + 20);
		size_t nUncompSize = GetLE32 (pEntry + 24);
		unsigned nNameLen = GetLE16 (pEntry + 28);
		unsigned nExtraLen = GetLE16 (pEntry + 30);
		unsigned nCommentLen = GetLE16 (pEntry + 32);
		size_t nLocalOffset = GetLE32 (pEntry + 42);
		const char *pName = (const char *) pEntry + 46;

		nCentral += 46 + nNameLen + nExtraLen + nCommentLen;

		if (   nExtLen > 0
		    && (   nNameLen < nExtLen
			|| strncasecmp (pName + nNameLen - nExtLen, pExtension, nExtLen) != 0))
		{
			continue;
		}

		if (   nLocalOffset + 30 > nArchiveSize
		    || GetLE32 (pArchive + nLocalOffset) != ZIP_LOCAL_HEADER_SIG)
		{
			return FALSE;
		}

		size_t nData =   nLocalOffset + 30
			       + GetLE16 (pArchive + nLocalOffset + 26)
			       + GetLE16 (pArchive + nLocalOffset + 28);
		if (nData + nCompSize > nArchiveSize)
		{
			return FALSE;
		}

		u8 *pData = new u8[nUncompSize + 1];
		if (pData == 0)
		{
			return FALSE;
		}

		if (nMethod == ZIP_METHOD_STORED)
		{
			if (nCompSize != nUncompSize)
			{
				delete [] pData;

				return FALSE;
			}

			memcpy (pData, pArchive + nData, nUncompSize);
		}
		else if (nMethod == ZIP_METHOD_DEFLATED)
		{
			long nResult = Inflate (pArchive + nData, nCompSize, pData, nUncompSize);
			if (nResult != (long) nUncompSize)
			{
				delete [] pData;

				return FALSE;
			}
		}
		else
		{
			delete [] pData;

			return FALSE;
		}

		if (CRC32 (pData, nUncompSize) != nCRC)
		{
			delete [] pData;

			return FALSE;
		}

		pData[nUncompSize] = 0;

		*ppData = pData;
		*pSize = nUncompSize;

		return TRUE;
	}

	return FALSE;
}

u32 CUnzip::CRC32 (const u8 *pData, size_t nSize)
{
	static u32 Table[256];
	static boolean bInitialized = FALSE;

	if (!bInitialized)
	{
		for (u32 i = 0; i < 256; i++)
		{
			u32 c = i;
			for (unsigned k = 0; k < 8; k++)
			{
				c = c & 1 ? 0xEDB88320 ^ (c >> 1) : c >> 1;
			}

			Table[i] = c;
		}

		bInitialized = TRUE;
	}

	u32 nCRC = 0xFFFFFFFF;
	while (nSize--)
	{
		nCRC = Table[(nCRC ^ *pData++) & 0xFF] ^ (nCRC >> 8);
	}

	return nCRC ^ 0xFFFFFFFF;
}

boolean CUnzip::IsZipArchive (const u8 *pData, size_t nSize)
{
	return    nSize >= 4
	       && GetLE32 (pData) == ZIP_LOCAL_HEADER_SIG;
}
