//
// xmlparser.h
//
// Minimal non-validating XML parser (sufficient for GenICam device descriptions)
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
#ifndef _gigevision_xmlparser_h
#define _gigevision_xmlparser_h

#include <circle/types.h>

struct TXMLAttribute
{
	const char	*pName;
	const char	*pValue;
	TXMLAttribute	*pNext;
};

class CXMLElement	/// An element node of the XML tree
{
public:
	/// \return Tag name (without namespace prefix handling)
	const char *GetName (void) const		{ return m_pName; }
	/// \return Text content with leading/trailing white space removed ("" if none)
	const char *GetText (void) const		{ return m_pText; }
	/// \return Attribute value or 0 if not present
	const char *GetAttribute (const char *pName) const;

	CXMLElement *GetParent (void) const		{ return m_pParent; }
	CXMLElement *GetFirstChild (void) const		{ return m_pFirstChild; }
	CXMLElement *GetNextSibling (void) const	{ return m_pNextSibling; }

	/// \return First direct child with the given tag name (or 0)
	CXMLElement *FindChild (const char *pName) const;
	/// \return Next sibling with the given tag name (or 0)
	CXMLElement *FindNextSibling (const char *pName) const;

	/// \return Text of first direct child with the given tag name (or 0)
	const char *GetChildText (const char *pName) const;

private:
	const char	*m_pName;
	const char	*m_pText;
	TXMLAttribute	*m_pFirstAttribute;
	CXMLElement	*m_pParent;
	CXMLElement	*m_pFirstChild;
	CXMLElement	*m_pLastChild;
	CXMLElement	*m_pNextSibling;

	friend class CXMLDocument;
};

class CXMLDocument	/// XML document tree, parsed in place from a text buffer
{
public:
	CXMLDocument (void);
	~CXMLDocument (void);

	/// \brief Parse an XML text
	/// \param pText XML text (will be modified; ownership is taken, must be allocated with new [])
	/// \param nLength Length of the text in bytes (the buffer must have nLength+1 bytes)
	/// \return Operation successful?
	boolean Parse (char *pText, size_t nLength);

	/// \return Root element (or 0 if not parsed)
	CXMLElement *GetRoot (void) const		{ return m_pRoot; }

	/// \return Number of elements in the tree
	unsigned GetElementCount (void) const		{ return m_nElements; }

private:
	void Clear (void);

	void *Allocate (size_t nSize);		// from memory pool, freed in Clear()

	static void DecodeEntities (char *pString);
	static boolean IsSpace (char chChar);

private:
	char *m_pText;
	CXMLElement *m_pRoot;
	unsigned m_nElements;

	struct TPoolBlock
	{
		TPoolBlock *pNext;
		size_t nUsed;
		size_t nSize;
		// data follows
	};
	TPoolBlock *m_pPool;

	static const char s_EmptyString[];
};

#endif
