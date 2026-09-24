//
// xmlparser.cpp
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
#include <gigevision/xmlparser.h>
#include <circle/util.h>
#include <assert.h>

#define POOL_BLOCK_SIZE		0x10000

const char CXMLDocument::s_EmptyString[] = "";

const char *CXMLElement::GetAttribute (const char *pName) const
{
	for (const TXMLAttribute *pAttr = m_pFirstAttribute; pAttr != 0; pAttr = pAttr->pNext)
	{
		if (strcmp (pAttr->pName, pName) == 0)
		{
			return pAttr->pValue;
		}
	}

	return 0;
}

CXMLElement *CXMLElement::FindChild (const char *pName) const
{
	for (CXMLElement *pChild = m_pFirstChild; pChild != 0; pChild = pChild->m_pNextSibling)
	{
		if (strcmp (pChild->m_pName, pName) == 0)
		{
			return pChild;
		}
	}

	return 0;
}

CXMLElement *CXMLElement::FindNextSibling (const char *pName) const
{
	for (CXMLElement *pNext = m_pNextSibling; pNext != 0; pNext = pNext->m_pNextSibling)
	{
		if (strcmp (pNext->m_pName, pName) == 0)
		{
			return pNext;
		}
	}

	return 0;
}

const char *CXMLElement::GetChildText (const char *pName) const
{
	CXMLElement *pChild = FindChild (pName);

	return pChild != 0 ? pChild->m_pText : 0;
}

CXMLDocument::CXMLDocument (void)
:	m_pText (0),
	m_pRoot (0),
	m_nElements (0),
	m_pPool (0)
{
}

CXMLDocument::~CXMLDocument (void)
{
	Clear ();
}

boolean CXMLDocument::Parse (char *pText, size_t nLength)
{
	assert (pText != 0);

	Clear ();

	m_pText = pText;
	m_pText[nLength] = '\0';

	char *p = m_pText;
	if (   (u8) p[0] == 0xEF		// skip UTF-8 BOM
	    && (u8) p[1] == 0xBB
	    && (u8) p[2] == 0xBF)
	{
		p += 3;
	}

	CXMLElement *pCurrent = 0;		// element, whose content is parsed

	while (*p != '\0')
	{
		if (*p != '<')
		{
			// character data
			char *pStart = p;
			while (*p != '\0' && *p != '<')
			{
				p++;
			}

			if (pCurrent == 0)
			{
				continue;		// ignore text outside the root element
			}

			char *pEnd = p;
			while (pStart < pEnd && IsSpace (*pStart))
			{
				pStart++;
			}
			while (pEnd > pStart && IsSpace (pEnd[-1]))
			{
				pEnd--;
			}

			if (   pEnd > pStart
			    && pCurrent->m_pText == s_EmptyString)
			{
				size_t nTextLength = pEnd - pStart;
				char *pCopy = (char *) Allocate (nTextLength + 1);
				memcpy (pCopy, pStart, nTextLength);
				pCopy[nTextLength] = '\0';
				DecodeEntities (pCopy);

				pCurrent->m_pText = pCopy;
			}

			continue;
		}

		if (strncmp (p, "<!--", 4) == 0)
		{
			char *pEnd = strstr (p + 4, "-->");
			if (pEnd == 0)
			{
				return FALSE;
			}

			p = pEnd + 3;

			continue;
		}

		if (strncmp (p, "<![CDATA[", 9) == 0)
		{
			char *pStart = p + 9;
			char *pEnd = strstr (pStart, "]]>");
			if (pEnd == 0)
			{
				return FALSE;
			}

			if (   pCurrent != 0
			    && pCurrent->m_pText == s_EmptyString)
			{
				*pEnd = '\0';
				pCurrent->m_pText = pStart;
			}

			p = pEnd + 3;

			continue;
		}

		if (   p[1] == '?'
		    || p[1] == '!')
		{
			// processing instruction or declaration (e.g. DOCTYPE)
			char *pEnd = strchr (p, '>');
			if (pEnd == 0)
			{
				return FALSE;
			}

			p = pEnd + 1;

			continue;
		}

		if (p[1] == '/')
		{
			// end tag
			if (pCurrent == 0)
			{
				return FALSE;
			}

			char *pEnd = strchr (p, '>');
			if (pEnd == 0)
			{
				return FALSE;
			}

			pCurrent = pCurrent->m_pParent;
			p = pEnd + 1;

			if (pCurrent == 0)
			{
				break;			// root element closed
			}

			continue;
		}

		// start tag
		if (   pCurrent == 0
		    && m_pRoot != 0)
		{
			return FALSE;			// second root element
		}

		p++;
		char *pName = p;
		while (   *p != '\0'
		       && !IsSpace (*p)
		       && *p != '/'
		       && *p != '>')
		{
			p++;
		}

		if (   *p == '\0'
		    || p == pName)
		{
			return FALSE;
		}

		CXMLElement *pElement = (CXMLElement *) Allocate (sizeof (CXMLElement));
		pElement->m_pName = pName;
		pElement->m_pText = s_EmptyString;
		pElement->m_pFirstAttribute = 0;
		pElement->m_pParent = pCurrent;
		pElement->m_pFirstChild = 0;
		pElement->m_pLastChild = 0;
		pElement->m_pNextSibling = 0;
		m_nElements++;

		if (pCurrent == 0)
		{
			m_pRoot = pElement;
		}
		else
		{
			if (pCurrent->m_pLastChild == 0)
			{
				pCurrent->m_pFirstChild = pElement;
			}
			else
			{
				pCurrent->m_pLastChild->m_pNextSibling = pElement;
			}

			pCurrent->m_pLastChild = pElement;
		}

		char chDelim = *p;
		*p++ = '\0';			// terminate name

		TXMLAttribute *pLastAttribute = 0;
		while (IsSpace (chDelim))
		{
			while (IsSpace (*p))
			{
				p++;
			}

			if (   *p == '/'
			    || *p == '>')
			{
				chDelim = *p++;

				break;
			}

			// attribute
			char *pAttrName = p;
			while (   *p != '\0'
			       && *p != '='
			       && !IsSpace (*p))
			{
				p++;
			}

			char *pAttrNameEnd = p;
			while (IsSpace (*p))
			{
				p++;
			}

			if (*p != '=')
			{
				return FALSE;
			}
			p++;

			while (IsSpace (*p))
			{
				p++;
			}

			char chQuote = *p;
			if (   chQuote != '"'
			    && chQuote != '\'')
			{
				return FALSE;
			}

			char *pValue = ++p;
			while (   *p != '\0'
			       && *p != chQuote)
			{
				p++;
			}

			if (*p == '\0')
			{
				return FALSE;
			}

			*pAttrNameEnd = '\0';
			*p++ = '\0';
			DecodeEntities (pValue);

			TXMLAttribute *pAttr = (TXMLAttribute *) Allocate (sizeof (TXMLAttribute));
			pAttr->pName = pAttrName;
			pAttr->pValue = pValue;
			pAttr->pNext = 0;

			if (pLastAttribute == 0)
			{
				pElement->m_pFirstAttribute = pAttr;
			}
			else
			{
				pLastAttribute->pNext = pAttr;
			}

			pLastAttribute = pAttr;

			chDelim = *p;
			if (IsSpace (chDelim))
			{
				p++;
			}
			else if (   chDelim == '/'
				 || chDelim == '>')
			{
				p++;
			}
			else
			{
				return FALSE;
			}
		}

		if (chDelim == '/')
		{
			// empty element tag
			if (*p != '>')
			{
				return FALSE;
			}

			p++;

			if (pCurrent == 0)
			{
				break;			// root element is empty
			}
		}
		else if (chDelim == '>')
		{
			pCurrent = pElement;
		}
		else
		{
			return FALSE;
		}
	}

	return m_pRoot != 0 && pCurrent == 0;
}

void CXMLDocument::Clear (void)
{
	while (m_pPool != 0)
	{
		TPoolBlock *pNext = m_pPool->pNext;

		delete [] (u8 *) m_pPool;

		m_pPool = pNext;
	}

	delete [] m_pText;
	m_pText = 0;

	m_pRoot = 0;
	m_nElements = 0;
}

void *CXMLDocument::Allocate (size_t nSize)
{
	nSize = (nSize + 15) & ~(size_t) 15;

	if (   m_pPool == 0
	    || m_pPool->nUsed + nSize > m_pPool->nSize)
	{
		size_t nBlockSize = nSize > POOL_BLOCK_SIZE ? nSize : POOL_BLOCK_SIZE;
		size_t nHeaderSize = (sizeof (TPoolBlock) + 15) & ~(size_t) 15;

		TPoolBlock *pBlock = (TPoolBlock *) new u8[nHeaderSize + nBlockSize];
		assert (pBlock != 0);

		pBlock->pNext = m_pPool;
		pBlock->nUsed = nHeaderSize;
		pBlock->nSize = nHeaderSize + nBlockSize;

		m_pPool = pBlock;
	}

	void *pResult = (u8 *) m_pPool + m_pPool->nUsed;
	m_pPool->nUsed += nSize;

	return pResult;
}

void CXMLDocument::DecodeEntities (char *pString)
{
	char *pOut = pString;
	for (char *pIn = pString; *pIn != '\0';)
	{
		if (*pIn != '&')
		{
			*pOut++ = *pIn++;

			continue;
		}

		static const struct
		{
			const char *pEntity;
			char chChar;
		}
		Entities[] =
		{
			{"&lt;", '<'}, {"&gt;", '>'}, {"&amp;", '&'}, {"&quot;", '"'}, {"&apos;", '\''}
		};

		boolean bFound = FALSE;
		for (unsigned i = 0; i < sizeof Entities / sizeof Entities[0]; i++)
		{
			size_t nLen = strlen (Entities[i].pEntity);
			if (strncmp (pIn, Entities[i].pEntity, nLen) == 0)
			{
				*pOut++ = Entities[i].chChar;
				pIn += nLen;
				bFound = TRUE;

				break;
			}
		}

		if (   !bFound
		    && pIn[1] == '#')
		{
			// numeric character reference (only ASCII range supported)
			char *pEnd;
			unsigned long ulChar =   pIn[2] == 'x' || pIn[2] == 'X'
					       ? strtoul (pIn + 3, &pEnd, 16)
					       : strtoul (pIn + 2, &pEnd, 10);
			if (*pEnd == ';')
			{
				*pOut++ = ulChar < 0x80 ? (char) ulChar : '?';
				pIn = pEnd + 1;
				bFound = TRUE;
			}
		}

		if (!bFound)
		{
			*pOut++ = *pIn++;
		}
	}

	*pOut = '\0';
}

boolean CXMLDocument::IsSpace (char chChar)
{
	return    chChar == ' '
	       || chChar == '\t'
	       || chChar == '\r'
	       || chChar == '\n';
}
