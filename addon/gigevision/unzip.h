//
// unzip.h
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
#ifndef _gigevision_unzip_h
#define _gigevision_unzip_h

#include <circle/types.h>

class CUnzip
{
public:
	/// \brief Extract a file from a ZIP archive in memory
	/// \param pArchive ZIP archive data
	/// \param nArchiveSize Size of the archive in bytes
	/// \param pExtension File name extension to look for (e.g. ".xml", 0 for first file)
	/// \param ppData Pointer to the extracted data (allocated with new [], one extra
	///		  0-byte appended) is returned here, the caller has to delete [] it
	/// \param pSize Size of the extracted data (without the extra 0-byte) is returned here
	/// \return Operation successful (and CRC OK)?
	static boolean Extract (const u8 *pArchive, size_t nArchiveSize, const char *pExtension,
				u8 **ppData, size_t *pSize);

	/// \brief Decompress raw DEFLATE (RFC 1951) data
	/// \param pSource Compressed data
	/// \param nSourceSize Size of compressed data
	/// \param pDest Output buffer
	/// \param nDestSize Size of output buffer
	/// \return Number of decompressed bytes (< 0 on error)
	static long Inflate (const u8 *pSource, size_t nSourceSize, u8 *pDest, size_t nDestSize);

	/// \return CRC-32 (IEEE 802.3) of the data
	static u32 CRC32 (const u8 *pData, size_t nSize);

	/// \return Is this a ZIP archive (starts with a local file header)?
	static boolean IsZipArchive (const u8 *pData, size_t nSize);
};

#endif
