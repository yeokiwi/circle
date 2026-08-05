//
// filemanager.h
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2025  R. Stange <rsta2@o2online.de>
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
#ifndef _filemanager_h
#define _filemanager_h

#include <circle/sched/mutex.h>
#include <circle/string.h>
#include <circle/types.h>
#include <fatfs/ff.h>

// FatFs drive to operate on (the SD card, see addon/fatfs/diskio.cpp)
#define FILE_DRIVE		"SD:"

// A single entry returned by ListFiles()
struct TFileManagerEntry
{
	char	 Name[FF_MAX_LFN + 1];		// 0-terminated file name
	unsigned nSize;				// file size in bytes (0 for directories)
	boolean	 bDirectory;			// is this entry a sub-directory?
};

class CFileManager	/// Read/write/delete access to the files on the SD card or eMMC memory
{
public:
	CFileManager (void);
	~CFileManager (void);

	/// \brief Mount the file system of the SD card or eMMC memory
	/// \return Operation successful?
	/// \note The device (CEMMCDevice) must be initialized before.
	boolean Mount (void);

	/// \param pDirectory Directory to be listed ("" for the root directory)
	/// \param pEntries Array to be filled with the directory entries
	/// \param nMaxEntries Size of the array
	/// \return Number of entries written to pEntries (directories come first)
	unsigned ListFiles (const char *pDirectory, TFileManagerEntry *pEntries,
			    unsigned nMaxEntries);

	/// \brief Create (or overwrite) a file and write the given data to it
	/// \param pPath File path relative to the root directory
	/// \param pData Data to be written
	/// \param nLength Number of bytes to write
	/// \return Operation successful?
	boolean WriteFile (const char *pPath, const u8 *pData, unsigned nLength);

	/// \brief Read a whole file into a buffer
	/// \param pPath File path relative to the root directory
	/// \param pBuffer Buffer to receive the file content
	/// \param nMaxLength Size of the buffer
	/// \return Number of bytes read, or -1 on error, or -2 if the file does not fit
	int ReadFile (const char *pPath, u8 *pBuffer, unsigned nMaxLength);

	/// \brief Delete a file
	/// \param pPath File path relative to the root directory
	/// \return Operation successful?
	boolean DeleteFile (const char *pPath);

	/// \brief Check that a name is a plain file name (one path component)
	/// \param pName File name to check
	/// \return TRUE if the name is safe to use (no path components)
	static boolean IsValidName (const char *pName);

	/// \brief Check that a path is a relative path below the root directory
	/// \param pPath Path to check ("" is the root directory)
	/// \return TRUE if the path is safe to use (no "..", no drive, not absolute)
	static boolean IsValidPath (const char *pPath);

	/// \brief Build the path "<directory>/<name>" (without drive)
	/// \param rPath Is set to the resulting path
	/// \param pDirectory Directory path ("" for the root directory)
	/// \param pName File or directory name to be appended
	static void ConcatPath (CString &rPath, const char *pDirectory, const char *pName);

private:
	// build "SD:/<path>" into rPath
	static void MakePath (CString &rPath, const char *pPath);

private:
	FATFS	m_FileSystem;
	boolean	m_bMounted;
	CMutex	m_Mutex;			// serializes access across HTTP worker tasks
};

#endif
