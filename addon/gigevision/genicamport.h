//
// genicamport.h
//
// Register access interface used by the GenICam node map
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
#ifndef _gigevision_genicamport_h
#define _gigevision_genicamport_h

#include <circle/types.h>

class CGenICamPort	/// Abstract device register space, addressed by byte
{
public:
	virtual ~CGenICamPort (void) {}

	/// \brief Read a block of bytes from the device address space
	/// \param nAddress Byte address in the device
	/// \param pBuffer Data is returned here (device byte order, unchanged)
	/// \param nLength Number of bytes to read
	/// \return Operation successful?
	virtual boolean Read (u64 nAddress, void *pBuffer, unsigned nLength) = 0;

	/// \brief Write a block of bytes to the device address space
	/// \param nAddress Byte address in the device
	/// \param pBuffer Data to be written (device byte order)
	/// \param nLength Number of bytes to write
	/// \return Operation successful?
	virtual boolean Write (u64 nAddress, const void *pBuffer, unsigned nLength) = 0;
};

#endif
