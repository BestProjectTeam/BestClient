#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_COLORED_PARTS_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_COLORED_PARTS_H

#include <base/color.h>
#include <base/str.h>

#include <engine/shared/console.h>
#include <engine/textrender.h>

#include <vector>

class CColoredParts
{
public:
	class CColoredPart
	{
	public:
		int m_Index;
		ColorRGBA m_Color;
	};

private:
	char *m_pBuffer = nullptr; // Owned buffer if allocated
	const char *m_pText = nullptr; // Points to valid string (owned or original)
	std::vector<CColoredPart> m_vColors;

public:
	const char *Text() const { return m_pText; }
	const std::vector<CColoredPart> &Colors() const { return m_vColors; }

	~CColoredParts()
	{
		delete[] m_pBuffer;
	}
	CColoredParts(const char *pInput, bool Parse)
	{
		if(!Parse || !str_find(pInput, "[["))
		{
			m_pText = pInput;
			return;
		}

		// Allocate a writable buffer
		const int Length = str_length(pInput);
		m_pBuffer = new char[Length + 1];
		m_pText = m_pBuffer;

		char *pWrite = m_pBuffer;
		const char *pRead = pInput;
		while(*pRead)
		{
			const char *pMarkerStart = str_find(pRead, "[[");
			if(!pMarkerStart)
			{
				while((*pWrite++ = *pRead++))
					;
				break;
			}

			// Copy plain text before the marker
			while(pRead < pMarkerStart)
				*pWrite++ = *pRead++;

			const char *pMarkerEnd = str_find(pMarkerStart + 2, "]]");
			if(!pMarkerEnd)
			{
				// No closing marker, copy rest
				while((*pWrite++ = *pRead++))
					;
				break;
			}

			// Extract marker content
			char aBuf[128];
			str_copy(aBuf, pMarkerStart + 2, std::min<int>(sizeof(aBuf), pMarkerEnd - pMarkerStart - 2 + 1));
			const auto Color = CConsole::ColorParse(aBuf, 0.0f);
			if(Color.has_value())
				m_vColors.emplace_back(pWrite - m_pBuffer, color_cast<ColorRGBA>(*Color));

			// Skip over marker
			pRead = pMarkerEnd + 2;
		}

		*pWrite = '\0';
	}

	void AddSplitsToCursor(CTextCursor &Cursor) const
	{
		Cursor.m_vColorSplits.reserve(Cursor.m_vColorSplits.size() + Colors().size());
		for(int PartIndex = 0; PartIndex < (int)Colors().size(); ++PartIndex)
		{
			const auto &Part = Colors()[PartIndex];
			int Length;
			if(PartIndex == (int)Colors().size() - 1)
				Length = 9999;
			else
				Length = Colors()[PartIndex + 1].m_Index - Part.m_Index;
			Cursor.m_vColorSplits.emplace_back(Cursor.m_CharCount + Part.m_Index, Length, Part.m_Color);
		}
	}
};

#endif
