#include "nameplates.h"

#include <base/str.h>
#include <base/math.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol7.h>
#include <engine/textrender.h>
#include <engine/client.h>

#include <generated/client_data.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/client/ui_rect.h>

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

enum class ENamePlateLayoutElement
{
	NONE = -1,
	ASYNC = 0,
	IGNORE_MARK,
	FRIEND_MARK,
	CLIENT_ID_INLINE,
	NAME,
	VOICE_CHAT,
	BCLIENT_INDICATOR,
	CLAN,
	REASON,
	CLIENT_ID_SEPARATE,
	HOOK_ICON,
	HOOK_ID,
	DIRECTION_LEFT,
	DIRECTION_UP,
	DIRECTION_RIGHT,
	COUNT,
};

static constexpr int MAX_NAMEPLATE_LAYOUT_OFFSET = 400;

struct SNamePlateLayoutOffsetConfig
{
	int *m_pX;
	int *m_pY;
};

static SNamePlateLayoutOffsetConfig GetNamePlateLayoutOffsetConfig(ENamePlateLayoutElement Element)
{
	switch(Element)
	{
	case ENamePlateLayoutElement::ASYNC: return {&g_Config.m_BcNameplateAsyncOffsetX, &g_Config.m_BcNameplateAsyncOffsetY};
	case ENamePlateLayoutElement::IGNORE_MARK: return {&g_Config.m_BcNameplateIgnoreOffsetX, &g_Config.m_BcNameplateIgnoreOffsetY};
	case ENamePlateLayoutElement::FRIEND_MARK: return {&g_Config.m_BcNameplateFriendOffsetX, &g_Config.m_BcNameplateFriendOffsetY};
	case ENamePlateLayoutElement::CLIENT_ID_INLINE: return {&g_Config.m_BcNameplateClientIdInlineOffsetX, &g_Config.m_BcNameplateClientIdInlineOffsetY};
	case ENamePlateLayoutElement::NAME: return {&g_Config.m_BcNameplateNameOffsetX, &g_Config.m_BcNameplateNameOffsetY};
	case ENamePlateLayoutElement::VOICE_CHAT: return {&g_Config.m_BcNameplateVoiceOffsetX, &g_Config.m_BcNameplateVoiceOffsetY};
	case ENamePlateLayoutElement::BCLIENT_INDICATOR: return {&g_Config.m_BcNameplateClientIndicatorOffsetX, &g_Config.m_BcNameplateClientIndicatorOffsetY};
	case ENamePlateLayoutElement::CLAN: return {&g_Config.m_BcNameplateClanOffsetX, &g_Config.m_BcNameplateClanOffsetY};
	case ENamePlateLayoutElement::REASON: return {&g_Config.m_BcNameplateReasonOffsetX, &g_Config.m_BcNameplateReasonOffsetY};
	case ENamePlateLayoutElement::CLIENT_ID_SEPARATE: return {&g_Config.m_BcNameplateClientIdSeparateOffsetX, &g_Config.m_BcNameplateClientIdSeparateOffsetY};
	case ENamePlateLayoutElement::HOOK_ICON: return {&g_Config.m_BcNameplateHookIconOffsetX, &g_Config.m_BcNameplateHookIconOffsetY};
	case ENamePlateLayoutElement::HOOK_ID: return {&g_Config.m_BcNameplateHookIdOffsetX, &g_Config.m_BcNameplateHookIdOffsetY};
	case ENamePlateLayoutElement::DIRECTION_LEFT: return {&g_Config.m_BcNameplateDirLeftOffsetX, &g_Config.m_BcNameplateDirLeftOffsetY};
	case ENamePlateLayoutElement::DIRECTION_UP: return {&g_Config.m_BcNameplateDirUpOffsetX, &g_Config.m_BcNameplateDirUpOffsetY};
	case ENamePlateLayoutElement::DIRECTION_RIGHT: return {&g_Config.m_BcNameplateDirRightOffsetX, &g_Config.m_BcNameplateDirRightOffsetY};
	case ENamePlateLayoutElement::COUNT:
	case ENamePlateLayoutElement::NONE: return {nullptr, nullptr};
	}
	dbg_assert(false, "invalid nameplate layout element");
	return {nullptr, nullptr};
}

static vec2 GetNamePlateLayoutOffset(ENamePlateLayoutElement Element)
{
	const auto Config = GetNamePlateLayoutOffsetConfig(Element);
	if(Config.m_pX == nullptr || Config.m_pY == nullptr)
		return vec2(0.0f, 0.0f);
	return vec2((float)*Config.m_pX, (float)*Config.m_pY);
}

static void SetNamePlateLayoutOffset(ENamePlateLayoutElement Element, vec2 Offset)
{
	const auto Config = GetNamePlateLayoutOffsetConfig(Element);
	if(Config.m_pX == nullptr || Config.m_pY == nullptr)
		return;
	*Config.m_pX = std::clamp(round_to_int(Offset.x), -MAX_NAMEPLATE_LAYOUT_OFFSET, MAX_NAMEPLATE_LAYOUT_OFFSET);
	*Config.m_pY = std::clamp(round_to_int(Offset.y), -MAX_NAMEPLATE_LAYOUT_OFFSET, MAX_NAMEPLATE_LAYOUT_OFFSET);
}

static void ResetNamePlateLayoutOffset(ENamePlateLayoutElement Element)
{
	SetNamePlateLayoutOffset(Element, vec2(0.0f, 0.0f));
}

enum class EHookStrongWeakState
{
	WEAK,
	NEUTRAL,
	STRONG
};

class CNamePlateData
{
public:
	bool m_Local; // BestClient
	bool m_InGame;
	ColorRGBA m_Color;
	bool m_ShowName;
	char m_aName[std::max<size_t>(MAX_NAME_LENGTH, protocol7::MAX_NAME_ARRAY_SIZE)];
	bool m_ShowFriendMark;
	bool m_ShowClientId;
	int m_ClientId;
	int m_DisplayClientId;
	float m_FontSizeClientId;
	bool m_ClientIdSeparateLine;
	float m_FontSize;
	bool m_ShowClan;
	char m_aClan[std::max<size_t>(MAX_CLAN_LENGTH, protocol7::MAX_CLAN_ARRAY_SIZE)];
	float m_FontSizeClan;
	bool m_ShowDirection;
	bool m_DirLeft;
	bool m_DirJump;
	bool m_DirRight;
	float m_FontSizeDirection;
	bool m_ShowHookStrongWeak;
	EHookStrongWeakState m_HookStrongWeakState;
	bool m_ShowHookStrongWeakId;
	int m_HookStrongWeakId;
	float m_FontSizeHookStrongWeak;

	// BestClient
	int m_ShowBClientIndicator;
	float m_FontSizeBClientIndicator;
	int m_IsUserBClientIndicator;
};

// Part Types

static constexpr float DEFAULT_PADDING = 5.0f;

class CNamePlatePart
{
protected:
	vec2 m_Size = vec2(0.0f, 0.0f);
	vec2 m_Padding = vec2(DEFAULT_PADDING, DEFAULT_PADDING);
	bool m_NewLine = false; // Whether this part is a new line (doesn't do anything else)
	bool m_Visible = true; // Whether this part is visible
	bool m_ShiftOnInvis = false; // Whether when not visible will still take up space
	ENamePlateLayoutElement m_LayoutElement = ENamePlateLayoutElement::NONE;
	CNamePlatePart(CGameClient &This, ENamePlateLayoutElement LayoutElement) :
		m_LayoutElement(LayoutElement)
	{
		(void)This;
	}

public:
	virtual void Update(CGameClient &This, const CNamePlateData &Data) {}
	virtual void Reset(CGameClient &This) {}
	virtual void Render(CGameClient &This, vec2 Pos) const {}
	vec2 Size() const { return m_Size; }
	vec2 Padding() const { return m_Padding; }
	bool NewLine() const { return m_NewLine; }
	bool Visible() const { return m_Visible; }
	bool ShiftOnInvis() const { return m_ShiftOnInvis; }
	ENamePlateLayoutElement LayoutElement() const { return m_LayoutElement; }
	vec2 LayoutOffset() const { return GetNamePlateLayoutOffset(m_LayoutElement); }
	CNamePlatePart() = delete;
	virtual ~CNamePlatePart() = default;
};

using PartsVector = std::vector<std::unique_ptr<CNamePlatePart>>;

class CNamePlatePartText : public CNamePlatePart
{
protected:
	STextContainerIndex m_TextContainerIndex;
	virtual bool UpdateNeeded(CGameClient &This, const CNamePlateData &Data) = 0;
	virtual void UpdateText(CGameClient &This, const CNamePlateData &Data) = 0;
	ColorRGBA m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	CNamePlatePartText(CGameClient &This, ENamePlateLayoutElement LayoutElement) :
		CNamePlatePart(This, LayoutElement)
	{
		Reset(This);
	}

public:
	void Update(CGameClient &This, const CNamePlateData &Data) override
	{
		if(!UpdateNeeded(This, Data) && m_TextContainerIndex.Valid())
			return;

		// Set flags
		unsigned int Flags = ETextRenderFlags::TEXT_RENDER_FLAG_NO_FIRST_CHARACTER_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_LAST_CHARACTER_ADVANCE;
		if(Data.m_InGame)
			Flags |= ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT; // Prevent jittering from rounding
		This.TextRender()->SetRenderFlags(Flags);

		if(Data.m_InGame)
		{
			// Create text at standard zoom
			float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
			This.Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);
			This.Graphics()->MapScreenToInterface(This.m_Camera.m_Center.x, This.m_Camera.m_Center.y);
			This.TextRender()->DeleteTextContainer(m_TextContainerIndex);
			UpdateText(This, Data);
			This.Graphics()->MapScreen(ScreenX0, ScreenY0, ScreenX1, ScreenY1);
		}
		else
		{
			UpdateText(This, Data);
		}

		This.TextRender()->SetRenderFlags(0);

		if(!m_TextContainerIndex.Valid())
		{
			m_Visible = false;
			return;
		}

		const STextBoundingBox Container = This.TextRender()->GetBoundingBoxTextContainer(m_TextContainerIndex);
		m_Size = vec2(Container.m_W, Container.m_H);
	}
	void Reset(CGameClient &This) override
	{
		This.TextRender()->DeleteTextContainer(m_TextContainerIndex);
	}
	void Render(CGameClient &This, vec2 Pos) const override
	{
		if(!m_TextContainerIndex.Valid())
			return;

		ColorRGBA OutlineColor, Color;
		Color = m_Color;
		OutlineColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f * m_Color.a);
		This.TextRender()->RenderTextContainer(m_TextContainerIndex,
			Color, OutlineColor,
			Pos.x - Size().x / 2.0f, Pos.y - Size().y / 2.0f);
	}
};

class CNamePlatePartIcon : public CNamePlatePart
{
protected:
	IGraphics::CTextureHandle m_Texture;
	float m_Rotation = 0.0f;
	ColorRGBA m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	CNamePlatePartIcon(CGameClient &This, ENamePlateLayoutElement LayoutElement) :
		CNamePlatePart(This, LayoutElement) {}

public:
	void Render(CGameClient &This, vec2 Pos) const override
	{
		IGraphics::CQuadItem QuadItem(Pos.x - Size().x / 2.0f, Pos.y - Size().y / 2.0f, Size().x, Size().y);
		This.Graphics()->TextureSet(m_Texture);
		This.Graphics()->QuadsBegin();
		This.Graphics()->SetColor(m_Color);
		This.Graphics()->QuadsSetRotation(m_Rotation);
		This.Graphics()->QuadsDrawTL(&QuadItem, 1);
		This.Graphics()->QuadsEnd();
		This.Graphics()->QuadsSetRotation(0.0f);
	}
};

class CNamePlatePartSprite : public CNamePlatePart
{
protected:
	IGraphics::CTextureHandle m_Texture;
	int m_Sprite = -1;
	int m_SpriteFlags = 0;
	float m_Rotation = 0.0f;
	ColorRGBA m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	CNamePlatePartSprite(CGameClient &This, ENamePlateLayoutElement LayoutElement) :
		CNamePlatePart(This, LayoutElement) {}

public:
	void Render(CGameClient &This, vec2 Pos) const override
	{
		This.Graphics()->TextureSet(m_Texture);
		This.Graphics()->QuadsSetRotation(m_Rotation);
		This.Graphics()->QuadsBegin();
		This.Graphics()->SetColor(m_Color);
		This.Graphics()->SelectSprite(m_Sprite, m_SpriteFlags);
		This.Graphics()->DrawSprite(Pos.x, Pos.y, Size().x, Size().y);
		This.Graphics()->QuadsEnd();
		This.Graphics()->QuadsSetRotation(0.0f);
	}
};

// Part Definitions

class CNamePlatePartNewLine : public CNamePlatePart
{
public:
	CNamePlatePartNewLine(CGameClient &This) :
		CNamePlatePart(This, ENamePlateLayoutElement::NONE)
	{
		m_NewLine = true;
	}
};

enum Direction
{
	DIRECTION_LEFT,
	DIRECTION_UP,
	DIRECTION_RIGHT
};

static ENamePlateLayoutElement DirectionToLayoutElement(int Direction)
{
	switch(Direction)
	{
	case DIRECTION_LEFT:
	case DIRECTION_UP:
	case DIRECTION_RIGHT:
		return ENamePlateLayoutElement::DIRECTION_LEFT;
	default: break;
	}
	dbg_assert(false, "invalid direction");
	return ENamePlateLayoutElement::NONE;
}

class CNamePlatePartDirection : public CNamePlatePartIcon
{
private:
	int m_Direction;

public:
	CNamePlatePartDirection(CGameClient &This, Direction Dir) :
		CNamePlatePartIcon(This, DirectionToLayoutElement(Dir))
	{
		m_Direction = Dir;
		switch(m_Direction)
		{
		case DIRECTION_LEFT:
			m_Rotation = pi;
			break;
		case DIRECTION_UP:
			m_Rotation = pi / -2.0f;
			break;
		case DIRECTION_RIGHT:
			m_Rotation = 0.0f;
			break;
		}
	}
	void Update(CGameClient &This, const CNamePlateData &Data) override
	{
		m_Texture = This.ArrowTexture();
		if(!Data.m_ShowDirection)
		{
			m_ShiftOnInvis = false;
			m_Visible = false;
			return;
		}
		m_ShiftOnInvis = true; // Only shift (horizontally) the other parts if directions as a whole is visible
		m_Size = vec2(Data.m_FontSizeDirection, Data.m_FontSizeDirection);
		m_Padding.y = m_Size.y / 2.0f;
		switch(m_Direction)
		{
		case DIRECTION_LEFT:
			m_Visible = Data.m_DirLeft;
			break;
		case DIRECTION_UP:
			m_Visible = Data.m_DirJump;
			break;
		case DIRECTION_RIGHT:
			m_Visible = Data.m_DirRight;
			break;
		}
		m_Color.a = Data.m_Color.a;
	}
};

class CNamePlateParBestClientId : public CNamePlatePartText
{
private:
	int m_ClientId = -1;
	static_assert(MAX_CLIENTS <= 999, "Make this buffer bigger");
	char m_aText[5] = "";
	float m_FontSize = -INFINITY;
	bool m_ClientIdSeparateLine = false;

protected:
	bool UpdateNeeded(CGameClient &This, const CNamePlateData &Data) override
	{
		m_Visible = Data.m_ShowClientId && (Data.m_ClientIdSeparateLine == m_ClientIdSeparateLine);
		if(!m_Visible)
			return false;
		m_Color = Data.m_Color;
		return m_FontSize != Data.m_FontSizeClientId || m_ClientId != Data.m_DisplayClientId;
	}
	void UpdateText(CGameClient &This, const CNamePlateData &Data) override
	{
		m_FontSize = Data.m_FontSizeClientId;
		m_ClientId = Data.m_DisplayClientId;
		if(m_ClientIdSeparateLine)
			str_format(m_aText, sizeof(m_aText), "%d", m_ClientId);
		else
			str_format(m_aText, sizeof(m_aText), "%d:", m_ClientId);
		CTextCursor Cursor;
		Cursor.m_FontSize = m_FontSize;
		This.TextRender()->CreateOrAppendTextContainer(m_TextContainerIndex, &Cursor, m_aText);
	}

public:
	CNamePlateParBestClientId(CGameClient &This, bool ClientIdSeparateLine) :
		CNamePlatePartText(This, ClientIdSeparateLine ? ENamePlateLayoutElement::CLIENT_ID_SEPARATE : ENamePlateLayoutElement::CLIENT_ID_INLINE)
	{
		m_ClientIdSeparateLine = ClientIdSeparateLine;
	}
};

class CNamePlatePartFriendMark : public CNamePlatePartText
{
private:
	float m_FontSize = -INFINITY;

protected:
	bool UpdateNeeded(CGameClient &This, const CNamePlateData &Data) override
	{
		m_Visible = Data.m_ShowFriendMark;
		if(!m_Visible)
			return false;
		m_Color.a = Data.m_Color.a;
		return m_FontSize != Data.m_FontSize;
	}
	void UpdateText(CGameClient &This, const CNamePlateData &Data) override
	{
		m_FontSize = Data.m_FontSize;
		CTextCursor Cursor;
		This.TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		Cursor.m_FontSize = m_FontSize;
		This.TextRender()->CreateOrAppendTextContainer(m_TextContainerIndex, &Cursor, FontIcon::HEART);
		This.TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}

public:
	CNamePlatePartFriendMark(CGameClient &This) :
		CNamePlatePartText(This, ENamePlateLayoutElement::FRIEND_MARK)
	{
		m_Color = ColorRGBA(1.0f, 0.0f, 0.0f);
	}
};

class CNamePlatePartName : public CNamePlatePartText
{
private:
	char m_aText[std::max<size_t>(MAX_NAME_LENGTH, protocol7::MAX_NAME_ARRAY_SIZE)] = "";
	float m_FontSize = -INFINITY;

protected:
	bool UpdateNeeded(CGameClient &This, const CNamePlateData &Data) override
	{
		m_Visible = Data.m_ShowName;
		if(!m_Visible)
			return false;
		m_Color = Data.m_Color;
		// BestClient
		if(g_Config.m_TcWarList)
		{
			if(This.m_WarList.GetWarData(Data.m_ClientId).m_WarName)
				m_Color = This.m_WarList.GetNameplateColor(Data.m_ClientId).WithAlpha(Data.m_Color.a);
			else if(This.m_WarList.GetWarData(Data.m_ClientId).m_WarClan)
				m_Color = This.m_WarList.GetClanColor(Data.m_ClientId).WithAlpha(Data.m_Color.a);
		}
		return m_FontSize != Data.m_FontSize || str_comp(m_aText, Data.m_aName) != 0;
	}
	void UpdateText(CGameClient &This, const CNamePlateData &Data) override
	{
		m_FontSize = Data.m_FontSize;
		str_copy(m_aText, Data.m_aName, sizeof(m_aText));
		CTextCursor Cursor;
		Cursor.m_FontSize = m_FontSize;
		This.TextRender()->CreateOrAppendTextContainer(m_TextContainerIndex, &Cursor, m_aText);
	}

public:
	CNamePlatePartName(CGameClient &This) :
		CNamePlatePartText(This, ENamePlateLayoutElement::NAME) {}
};

class CNamePlatePartClan : public CNamePlatePartText
{
private:
	char m_aText[std::max<size_t>(MAX_CLAN_LENGTH, protocol7::MAX_CLAN_ARRAY_SIZE)] = "";
	float m_FontSize = -INFINITY;

protected:
	bool UpdateNeeded(CGameClient &This, const CNamePlateData &Data) override
	{
		m_Visible = Data.m_ShowClan;
		if(!m_Visible && Data.m_aClan[0] != '\0')
			return false;
		m_Color = Data.m_Color;
		// BestClient
		if(This.m_WarList.GetWarData(Data.m_ClientId).m_WarClan)
			m_Color = This.m_WarList.GetClanColor(Data.m_ClientId).WithAlpha(Data.m_Color.a);
		return m_FontSize != Data.m_FontSizeClan || str_comp(m_aText, Data.m_aClan) != 0;
	}
	void UpdateText(CGameClient &This, const CNamePlateData &Data) override
	{
		m_FontSize = Data.m_FontSizeClan;
		str_copy(m_aText, Data.m_aClan, sizeof(m_aText));
		CTextCursor Cursor;
		Cursor.m_FontSize = m_FontSize;
		This.TextRender()->CreateOrAppendTextContainer(m_TextContainerIndex, &Cursor, m_aText);
	}

public:
	CNamePlatePartClan(CGameClient &This) :
		CNamePlatePartText(This, ENamePlateLayoutElement::CLAN) {}
};

class CNamePlatePartHookStrongWeak : public CNamePlatePartSprite
{
protected:
	void Update(CGameClient &This, const CNamePlateData &Data) override
	{
		m_Visible = Data.m_ShowHookStrongWeak;
		if(!m_Visible)
			return;
		m_Size = vec2(Data.m_FontSizeHookStrongWeak + DEFAULT_PADDING, Data.m_FontSizeHookStrongWeak + DEFAULT_PADDING);
		switch(Data.m_HookStrongWeakState)
		{
		case EHookStrongWeakState::STRONG:
			m_Sprite = SPRITE_HOOK_STRONG;
			m_Color = color_cast<ColorRGBA>(ColorHSLA(6401973));
			break;
		case EHookStrongWeakState::NEUTRAL:
			m_Sprite = SPRITE_HOOK_ICON;
			m_Color = ColorRGBA(1.0f, 1.0f, 1.0f);
			break;
		case EHookStrongWeakState::WEAK:
			m_Sprite = SPRITE_HOOK_WEAK;
			m_Color = color_cast<ColorRGBA>(ColorHSLA(41131));
			break;
		}
		m_Color.a = Data.m_Color.a;
	}

public:
	CNamePlatePartHookStrongWeak(CGameClient &This) :
		CNamePlatePartSprite(This, ENamePlateLayoutElement::HOOK_ICON)
	{
		m_Texture = g_pData->m_aImages[IMAGE_STRONGWEAK].m_Id;
		m_Padding = vec2(0.0f, 0.0f);
	}
};

class CNamePlatePartHookStrongWeakId : public CNamePlatePartText
{
private:
	int m_StrongWeakId = -1;
	static_assert(MAX_CLIENTS <= 999, "Make this buffer bigger");
	char m_aText[4] = "";
	float m_FontSize = -INFINITY;

protected:
	bool UpdateNeeded(CGameClient &This, const CNamePlateData &Data) override
	{
		m_Visible = Data.m_ShowHookStrongWeakId;
		if(!m_Visible)
			return false;
		switch(Data.m_HookStrongWeakState)
		{
		case EHookStrongWeakState::STRONG:
			m_Color = color_cast<ColorRGBA>(ColorHSLA(6401973));
			break;
		case EHookStrongWeakState::NEUTRAL:
			m_Color = ColorRGBA(1.0f, 1.0f, 1.0f);
			break;
		case EHookStrongWeakState::WEAK:
			m_Color = color_cast<ColorRGBA>(ColorHSLA(41131));
			break;
		}
		m_Color.a = Data.m_Color.a;
		return m_FontSize != Data.m_FontSizeHookStrongWeak || m_StrongWeakId != Data.m_HookStrongWeakId;
	}
	void UpdateText(CGameClient &This, const CNamePlateData &Data) override
	{
		m_FontSize = Data.m_FontSizeHookStrongWeak;
		m_StrongWeakId = Data.m_HookStrongWeakId;
		str_format(m_aText, sizeof(m_aText), "%d", m_StrongWeakId);
		CTextCursor Cursor;
		Cursor.m_FontSize = m_FontSize;
		This.TextRender()->CreateOrAppendTextContainer(m_TextContainerIndex, &Cursor, m_aText);
	}

public:
	CNamePlatePartHookStrongWeakId(CGameClient &This) :
		CNamePlatePartText(This, ENamePlateLayoutElement::HOOK_ID) {}
};

// ***** BestClient Parts *****

struct CAsyncIndicatorMetrics
{
	int m_ClientMs = 0;
	int m_ServerMs = 0;
	int m_DesyncMs = 0;
	int m_Level = 0;
};

static int GetAsyncIndicatorLevel(int DesyncMs)
{
	if(DesyncMs <= 8)
		return 1;
	if(DesyncMs <= 16)
		return 2;
	if(DesyncMs <= 28)
		return 3;
	if(DesyncMs <= 42)
		return 4;
	if(DesyncMs <= 60)
		return 5;
	return 5;
}

static CAsyncIndicatorMetrics GetAsyncIndicatorMetrics(CGameClient &This, const CNamePlateData &Data)
{
	CAsyncIndicatorMetrics Metrics;
	if(Data.m_InGame)
	{
		Metrics.m_ClientMs = This.Client()->GetPredictionTime();
		if(Metrics.m_ClientMs < 0)
			Metrics.m_ClientMs = -Metrics.m_ClientMs;

		if(Data.m_Local && This.m_Snap.m_pLocalInfo)
			Metrics.m_ServerMs = This.m_Snap.m_pLocalInfo->m_Latency;
		else
			Metrics.m_ServerMs = This.m_Snap.m_apPlayerInfos[Data.m_ClientId]->m_Latency;
	}
	else
	{
		Metrics.m_ClientMs = 28 + Data.m_ClientId * 7;
		Metrics.m_ServerMs = 32 + Data.m_ClientId * 13;
	}

	if(Metrics.m_ServerMs < 0)
		Metrics.m_ServerMs = 0;
	if(Metrics.m_ClientMs < 0)
		Metrics.m_ClientMs = 0;

	Metrics.m_DesyncMs = Metrics.m_ClientMs - Metrics.m_ServerMs;
	if(Metrics.m_DesyncMs < 0)
		Metrics.m_DesyncMs = -Metrics.m_DesyncMs;

	Metrics.m_Level = GetAsyncIndicatorLevel(Metrics.m_DesyncMs);
	return Metrics;
}

static ColorRGBA GetAsyncIndicatorColor(const CAsyncIndicatorMetrics &Metrics, float Alpha)
{
	const float Severity = std::clamp((Metrics.m_Level - 1) / 4.0f, 0.0f, 1.0f);
	const float Hue = (1.0f - Severity) / 3.0f;
	return color_cast<ColorRGBA>(ColorHSLA(Hue, 1.0f, 0.5f, Alpha));
}

class CNamePlatePartAsyncText : public CNamePlatePartText
{
private:
	float m_FontSize = -INFINITY;
	CAsyncIndicatorMetrics m_Metrics = {-1, -1, -1, -1};

protected:
	bool UpdateNeeded(CGameClient &This, const CNamePlateData &Data) override
	{
		m_Visible = g_Config.m_BcNameplateAsyncIndicator > 0 &&
			Data.m_ShowName &&
			(!Data.m_InGame || Data.m_Local);
		if(!m_Visible)
			return false;

		const CAsyncIndicatorMetrics NewMetrics = GetAsyncIndicatorMetrics(This, Data);
		m_Color = GetAsyncIndicatorColor(NewMetrics, Data.m_Color.a);
		const float NewFontSize = Data.m_FontSize * 0.8f;

		const bool Changed = m_FontSize != NewFontSize ||
			m_Metrics.m_ClientMs != NewMetrics.m_ClientMs ||
			m_Metrics.m_ServerMs != NewMetrics.m_ServerMs ||
			m_Metrics.m_DesyncMs != NewMetrics.m_DesyncMs ||
			m_Metrics.m_Level != NewMetrics.m_Level;
		m_FontSize = NewFontSize;
		m_Metrics = NewMetrics;
		return Changed;
	}

	void UpdateText(CGameClient &This, const CNamePlateData &) override
	{
		char aText[16];
		str_format(aText, sizeof(aText), "%d/5", m_Metrics.m_Level);

		CTextCursor Cursor;
		Cursor.m_FontSize = m_FontSize;
		This.TextRender()->CreateOrAppendTextContainer(m_TextContainerIndex, &Cursor, aText);
	}

public:
	CNamePlatePartAsyncText(CGameClient &This) :
		CNamePlatePartText(This, ENamePlateLayoutElement::ASYNC) {}
};

class CNamePlatePartReason : public CNamePlatePartText
{
private:
	char m_aText[MAX_WARLIST_REASON_LENGTH] = "";
	float m_FontSize = -INFINITY;

protected:
	bool UpdateNeeded(CGameClient &This, const CNamePlateData &Data) override
	{
		m_Visible = Data.m_InGame;
		if(!m_Visible)
			return false;
		const char *pReason = This.m_WarList.GetWarData(Data.m_ClientId).m_aReason;
		m_Visible = pReason[0] != '\0' && !This.m_Snap.m_apPlayerInfos[Data.m_ClientId]->m_Local;
		if(!m_Visible)
			return false;
		m_Color = ColorRGBA(0.7f, 0.7f, 0.7f, Data.m_Color.a);
		return m_FontSize != Data.m_FontSizeClan || str_comp(m_aText, pReason) != 0;
	}
	void UpdateText(CGameClient &This, const CNamePlateData &Data) override
	{
		m_FontSize = Data.m_FontSizeClan;
		const char *pReason = This.m_WarList.GetWarData(Data.m_ClientId).m_aReason;
		str_copy(m_aText, pReason, sizeof(m_aText));
		CTextCursor Cursor;
		Cursor.m_FontSize = m_FontSize;
		This.TextRender()->CreateOrAppendTextContainer(m_TextContainerIndex, &Cursor, m_aText);
	}

public:
	CNamePlatePartReason(CGameClient &This) :
		CNamePlatePartText(This, ENamePlateLayoutElement::REASON) {}
};

class CNamePlatePartIgnoreMark : public CNamePlatePartText
{
private:
	float m_FontSize = -INFINITY;

protected:
	bool UpdateNeeded(CGameClient &This, const CNamePlateData &Data) override
	{
		m_Visible = (Data.m_InGame && Data.m_ShowName && This.Client()->State() != IClient::STATE_DEMOPLAYBACK && (This.m_aClients[Data.m_ClientId].m_Foe || This.m_aClients[Data.m_ClientId].m_ChatIgnore));
		if(!m_Visible)
			return false;
		m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, Data.m_Color.a);
		return m_FontSize != Data.m_FontSize;
	}
	void UpdateText(CGameClient &This, const CNamePlateData &Data) override
	{
		m_FontSize = Data.m_FontSize;
		CTextCursor Cursor;
		This.TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		Cursor.m_FontSize = m_FontSize;
		This.TextRender()->CreateOrAppendTextContainer(m_TextContainerIndex, &Cursor, FontIcon::COMMENT_SLASH);
		This.TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}

public:
	CNamePlatePartIgnoreMark(CGameClient &This) :
		CNamePlatePartText(This, ENamePlateLayoutElement::IGNORE_MARK) {}
};

class CNamePlatePartBClientIndicator : public CNamePlatePartIcon
{
protected:
	void Update(CGameClient &This, const CNamePlateData &Data) override
	{
		if(!Data.m_ShowBClientIndicator)
		{
			m_ShiftOnInvis = false;
			m_Visible = false;
			return;
		}
		m_ShiftOnInvis = !g_Config.m_BcClientIndicatorInNamePlateDynamic; // Only shift (horizontally) the other parts if directions as a whole is visible
		m_Size = vec2(Data.m_FontSizeBClientIndicator + DEFAULT_PADDING, Data.m_FontSizeBClientIndicator + DEFAULT_PADDING);
		m_Visible = Data.m_IsUserBClientIndicator;
		m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, Data.m_Color.a);
	}

public:
	CNamePlatePartBClientIndicator(CGameClient &This) :
		CNamePlatePartIcon(This, ENamePlateLayoutElement::BCLIENT_INDICATOR)
	{
		m_Texture = g_pData->m_aImages[IMAGE_BCICON].m_Id;
		m_Padding = vec2(0.0f, 0.0f);
	}
};

// ***** Name Plates *****

class CNamePlate
{
private:
	bool m_Inited = false;
	bool m_InGame = false;
	bool m_UseLayoutOffsets = false;
	PartsVector m_vpParts;

public:
	struct SPreviewPartRect
	{
		ENamePlateLayoutElement m_Element = ENamePlateLayoutElement::NONE;
		CUIRect m_Rect{};
		vec2 m_BaseCenter = vec2(0.0f, 0.0f);
	};

private:
	vec2 LayoutOffset(const CNamePlatePart &Part) const
	{
		return m_UseLayoutOffsets ? Part.LayoutOffset() : vec2(0.0f, 0.0f);
	}

	static bool IsAnchoredVoiceChatPart(const CNamePlatePart &Part, bool LineHasVisibleName)
	{
		return LineHasVisibleName && Part.Visible() && Part.LayoutElement() == ENamePlateLayoutElement::VOICE_CHAT;
	}

	template<typename F>
	void ForEachRenderedPart(const vec2 &PositionBottomMiddle, F &&Fn) const
	{
		vec2 Position = PositionBottomMiddle;
		vec2 LineSize = vec2(0.0f, 0.0f);
		bool Empty = true;
		auto Start = m_vpParts.begin();
		auto FlushLine = [&](PartsVector::const_iterator End) {
			bool LineHasVisibleName = false;
			for(auto PartIt = Start; PartIt != End; ++PartIt)
			{
				const CNamePlatePart &Part = **PartIt;
				if(Part.Visible() && Part.LayoutElement() == ENamePlateLayoutElement::NAME)
				{
					LineHasVisibleName = true;
					break;
				}
			}

			float EffectiveLineWidth = 0.0f;
			for(auto PartIt = Start; PartIt != End; ++PartIt)
			{
				const CNamePlatePart &Part = **PartIt;
				if((Part.Visible() || Part.ShiftOnInvis()) && !IsAnchoredVoiceChatPart(Part, LineHasVisibleName))
					EffectiveLineWidth += Part.Size().x + Part.Padding().x;
			}

			vec2 Pos = Position;
			Pos.x -= EffectiveLineWidth / 2.0f;
			vec2 NamePos = vec2(0.0f, 0.0f);
			float NameWidth = 0.0f;
			bool HasNamePos = false;
			for(auto PartIt = Start; PartIt != End; ++PartIt)
			{
				const CNamePlatePart &Part = **PartIt;
				const bool AnchoredVoiceChat = IsAnchoredVoiceChatPart(Part, LineHasVisibleName);
				if(Part.Visible())
				{
					vec2 PartPos(
						Pos.x + (Part.Padding().x + Part.Size().x) / 2.0f,
						Pos.y - std::max(LineSize.y, Part.Padding().y + Part.Size().y) / 2.0f);
					if(AnchoredVoiceChat && HasNamePos)
						PartPos.x = NamePos.x + NameWidth / 2.0f + Part.Padding().x + Part.Size().x / 2.0f;
					Fn(Part, PartPos);
					if(Part.LayoutElement() == ENamePlateLayoutElement::NAME)
					{
						NamePos = PartPos;
						NameWidth = Part.Size().x;
						HasNamePos = true;
					}
				}
				if((Part.Visible() || Part.ShiftOnInvis()) && !AnchoredVoiceChat)
					Pos.x += Part.Size().x + Part.Padding().x;
			}
		};

		for(auto PartIt = m_vpParts.begin(); PartIt != m_vpParts.end(); ++PartIt)
		{
			const CNamePlatePart &Part = **PartIt;
			if(Part.NewLine())
			{
				if(!Empty)
				{
					FlushLine(std::next(PartIt));
					Position.y -= LineSize.y;
				}
				Start = std::next(PartIt);
				LineSize = vec2(0.0f, 0.0f);
			}
			else if(Part.Visible() || Part.ShiftOnInvis())
			{
				Empty = false;
				LineSize.x += Part.Size().x + Part.Padding().x;
				LineSize.y = std::max(LineSize.y, Part.Size().y + Part.Padding().y);
			}
		}
		if(!Empty)
			FlushLine(m_vpParts.end());
	}
	template<typename PartType, typename... ArgsType>
	void AddPart(CGameClient &This, ArgsType &&...Args)
	{
		m_vpParts.push_back(std::make_unique<PartType>(This, std::forward<ArgsType>(Args)...));
	}
	void Init(CGameClient &This)
	{
		if(m_Inited)
			return;
		m_Inited = true;

		AddPart<CNamePlatePartAsyncText>(This); // BestClient
		AddPart<CNamePlatePartIgnoreMark>(This); // BestClient
		AddPart<CNamePlatePartFriendMark>(This);
		AddPart<CNamePlateParBestClientId>(This, false);
		AddPart<CNamePlatePartBClientIndicator>(This);
		AddPart<CNamePlatePartName>(This);
		AddPart<CNamePlatePartNewLine>(This);

		AddPart<CNamePlatePartClan>(This);
		AddPart<CNamePlatePartNewLine>(This);

		AddPart<CNamePlatePartReason>(This); // BestClient
		AddPart<CNamePlatePartNewLine>(This); // BestClient

		AddPart<CNamePlateParBestClientId>(This, true);
		AddPart<CNamePlatePartNewLine>(This);

		AddPart<CNamePlatePartHookStrongWeak>(This);
		AddPart<CNamePlatePartHookStrongWeakId>(This);
		AddPart<CNamePlatePartNewLine>(This);

		AddPart<CNamePlatePartDirection>(This, DIRECTION_LEFT);
		AddPart<CNamePlatePartDirection>(This, DIRECTION_UP);
		AddPart<CNamePlatePartDirection>(This, DIRECTION_RIGHT);
	}

public:
	bool IsInited() const { return m_Inited; }
	CNamePlate() = default;
	CNamePlate(CGameClient &This, const CNamePlateData &Data)
	{
		// Convenience constructor
		Update(This, Data);
	}
	void Reset(CGameClient &This)
	{
		for(auto &Part : m_vpParts)
			Part->Reset(This);
	}
	void Update(CGameClient &This, const CNamePlateData &Data)
	{
		Init(This);
		m_InGame = Data.m_InGame;
		m_UseLayoutOffsets = !Data.m_InGame || Data.m_Local;
		for(auto &Part : m_vpParts)
			Part->Update(This, Data);
	}
	void Render(CGameClient &This, const vec2 &PositionBottomMiddle)
	{
		dbg_assert(m_Inited, "Tried to render uninited nameplate");
		ForEachRenderedPart(PositionBottomMiddle, [&](const CNamePlatePart &Part, vec2 BasePos) {
			Part.Render(This, BasePos + LayoutOffset(Part));
		});
		This.Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
	void CollectPreviewPartRects(const vec2 &PositionBottomMiddle, std::vector<SPreviewPartRect> &vOut) const
	{
		dbg_assert(m_Inited, "Tried to get rects of uninited nameplate");
		vOut.clear();
		ForEachRenderedPart(PositionBottomMiddle, [&](const CNamePlatePart &Part, vec2 BasePos) {
			if(Part.LayoutElement() == ENamePlateLayoutElement::NONE)
				return;

			const vec2 Offset = LayoutOffset(Part);
			const vec2 RenderPos = BasePos + Offset;
			CUIRect Rect;
			Rect.x = RenderPos.x - Part.Size().x / 2.0f;
			Rect.y = RenderPos.y - Part.Size().y / 2.0f;
			Rect.w = Part.Size().x;
			Rect.h = Part.Size().y;
			bool Merged = false;
			for(auto &PreviewRect : vOut)
			{
				if(PreviewRect.m_Element != Part.LayoutElement())
					continue;

				const float MinX = minimum(PreviewRect.m_Rect.x, Rect.x);
				const float MinY = minimum(PreviewRect.m_Rect.y, Rect.y);
				const float MaxX = maximum(PreviewRect.m_Rect.x + PreviewRect.m_Rect.w, Rect.x + Rect.w);
				const float MaxY = maximum(PreviewRect.m_Rect.y + PreviewRect.m_Rect.h, Rect.y + Rect.h);
				PreviewRect.m_Rect.x = MinX;
				PreviewRect.m_Rect.y = MinY;
				PreviewRect.m_Rect.w = MaxX - MinX;
				PreviewRect.m_Rect.h = MaxY - MinY;
				PreviewRect.m_BaseCenter = PreviewRect.m_Rect.Center() - Offset;
				Merged = true;
				break;
			}

			if(!Merged)
				vOut.push_back({Part.LayoutElement(), Rect, Rect.Center() - Offset});
		});
	}
	vec2 Size() const
	{
		dbg_assert(m_Inited, "Tried to get size of uninited nameplate");
		// X: Total width including padding of line, Y: Max height of line parts
		vec2 LineSize = vec2(0.0f, 0.0f);
		float WMax = 0.0f;
		float HTotal = 0.0f;
		bool Empty = true;
		auto Start = m_vpParts.begin();
		for(auto PartIt = m_vpParts.begin(); PartIt != m_vpParts.end(); ++PartIt) // NOLINT(modernize-loop-convert) For consistency with Render
		{
			CNamePlatePart &Part = **PartIt;
			if(Part.NewLine())
			{
				if(!Empty)
				{
					bool LineHasVisibleName = false;
					for(auto VisiblePartIt = Start; VisiblePartIt != PartIt; ++VisiblePartIt)
					{
						const CNamePlatePart &VisiblePart = **VisiblePartIt;
						if(VisiblePart.Visible() && VisiblePart.LayoutElement() == ENamePlateLayoutElement::NAME)
						{
							LineHasVisibleName = true;
							break;
						}
					}

					float EffectiveLineWidth = 0.0f;
					for(auto VisiblePartIt = Start; VisiblePartIt != PartIt; ++VisiblePartIt)
					{
						const CNamePlatePart &VisiblePart = **VisiblePartIt;
						if((VisiblePart.Visible() || VisiblePart.ShiftOnInvis()) && !IsAnchoredVoiceChatPart(VisiblePart, LineHasVisibleName))
							EffectiveLineWidth += VisiblePart.Size().x + VisiblePart.Padding().x;
					}

					if(EffectiveLineWidth > WMax)
						WMax = EffectiveLineWidth;
					HTotal += LineSize.y;
				}
				Start = std::next(PartIt);
				LineSize = vec2(0.0f, 0.0f);
			}
			else if(Part.Visible() || Part.ShiftOnInvis())
			{
				Empty = false;
				LineSize.x += Part.Size().x + Part.Padding().x;
				LineSize.y = std::max(LineSize.y, Part.Size().y + Part.Padding().y);
			}
		}
		if(!Empty)
		{
			bool LineHasVisibleName = false;
			for(auto VisiblePartIt = Start; VisiblePartIt != m_vpParts.end(); ++VisiblePartIt)
			{
				const CNamePlatePart &VisiblePart = **VisiblePartIt;
				if(VisiblePart.Visible() && VisiblePart.LayoutElement() == ENamePlateLayoutElement::NAME)
				{
					LineHasVisibleName = true;
					break;
				}
			}

			float EffectiveLineWidth = 0.0f;
			for(auto VisiblePartIt = Start; VisiblePartIt != m_vpParts.end(); ++VisiblePartIt)
			{
				const CNamePlatePart &VisiblePart = **VisiblePartIt;
				if((VisiblePart.Visible() || VisiblePart.ShiftOnInvis()) && !IsAnchoredVoiceChatPart(VisiblePart, LineHasVisibleName))
					EffectiveLineWidth += VisiblePart.Size().x + VisiblePart.Padding().x;
			}

			if(EffectiveLineWidth > WMax)
				WMax = EffectiveLineWidth;
			HTotal += LineSize.y;
		}
		return vec2(WMax, HTotal);
	}
};

class CNamePlates::CNamePlatesData
{
public:
	CNamePlate m_aNamePlates[MAX_CLIENTS];
};

static float ClampPreviewCenter(float Center, float HalfExtent, float MinEdge, float MaxEdge)
{
	if(HalfExtent * 2.0f >= MaxEdge - MinEdge)
		return (MinEdge + MaxEdge) / 2.0f;
	return std::clamp(Center, MinEdge + HalfExtent, MaxEdge - HalfExtent);
}

static float SnapPreviewAxis(float Center, float HalfExtent, float MinEdge, float MaxEdge, float ContainerCenter, bool HasExtraCenter, float ExtraCenter, float Threshold, bool &Snapped, float &Guide)
{
	struct SCandidate
	{
		float m_Center;
		float m_Guide;
	};

	std::array<SCandidate, 4> aCandidates = {{
		{MinEdge + HalfExtent, MinEdge},
		{ContainerCenter, ContainerCenter},
		{MaxEdge - HalfExtent, MaxEdge},
		{HasExtraCenter ? ExtraCenter : Center, HasExtraCenter ? ExtraCenter : Center},
	}};

	float BestCenter = Center;
	float BestGuide = 0.0f;
	float BestDistance = Threshold + 1.0f;
	const int CandidateCount = HasExtraCenter ? 4 : 3;
	for(int i = 0; i < CandidateCount; ++i)
	{
		const float Distance = absolute(Center - aCandidates[i].m_Center);
		if(Distance < BestDistance)
		{
			BestDistance = Distance;
			BestCenter = aCandidates[i].m_Center;
			BestGuide = aCandidates[i].m_Guide;
		}
	}

	Snapped = BestDistance <= Threshold;
	if(Snapped)
	{
		Guide = BestGuide;
		return BestCenter;
	}
	return Center;
}

void CNamePlates::RenderNamePlateGame(vec2 Position, const CNetObj_PlayerInfo *pPlayerInfo, float Alpha)
{
	// Get screen edges to avoid rendering offscreen
	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	// Assume that the name plate fits into a 800x800 box placed directly above the tee
	const float CullingScreenX0 = ScreenX0 - 400.0f;
	const float CullingScreenX1 = ScreenX1 + 400.0f;
	const float CullingScreenY1 = ScreenY1 + 800.0f;
	if(!(in_range(Position.x, CullingScreenX0, CullingScreenX1) && in_range(Position.y, ScreenY0, CullingScreenY1)))
		return;

	CNamePlateData Data;

	const auto &ClientData = GameClient()->m_aClients[pPlayerInfo->m_ClientId];
	const bool OtherTeam = GameClient()->IsOtherTeam(pPlayerInfo->m_ClientId);

	Data.m_InGame = true;
	// Check focus mode settings
	if(g_Config.m_ClFocusMode && g_Config.m_ClFocusModeHideNames)
		return; // Don't render nameplate at all

	Data.m_ShowName = pPlayerInfo->m_Local ? g_Config.m_ClNamePlatesOwn : g_Config.m_ClNamePlates;
	str_copy(Data.m_aName, GameClient()->m_aClients[pPlayerInfo->m_ClientId].m_aName);
	Data.m_ShowFriendMark = Data.m_ShowName && g_Config.m_ClNamePlatesFriendMark && GameClient()->m_aClients[pPlayerInfo->m_ClientId].m_Friend;
	Data.m_ShowClientId = Data.m_ShowName && (g_Config.m_Debug || g_Config.m_ClNamePlatesIds);
	Data.m_FontSize = 18.0f + 20.0f * g_Config.m_ClNamePlatesSize / 100.0f;

	Data.m_ClientId = pPlayerInfo->m_ClientId;
	Data.m_DisplayClientId = pPlayerInfo->m_ClientId;
	Data.m_ClientIdSeparateLine = g_Config.m_ClNamePlatesIdsSeparateLine;
	Data.m_FontSizeClientId = Data.m_ClientIdSeparateLine ? (18.0f + 20.0f * g_Config.m_ClNamePlatesIdsSize / 100.0f) : Data.m_FontSize;

	Data.m_ShowClan = Data.m_ShowName && g_Config.m_ClNamePlatesClan;
	str_copy(Data.m_aClan, GameClient()->m_aClients[pPlayerInfo->m_ClientId].m_aClan);
	Data.m_FontSizeClan = 18.0f + 20.0f * g_Config.m_ClNamePlatesClanSize / 100.0f;

	Data.m_FontSizeHookStrongWeak = 18.0f + 20.0f * g_Config.m_ClNamePlatesStrongSize / 100.0f;
	Data.m_FontSizeDirection = 18.0f + 20.0f * g_Config.m_ClDirectionSize / 100.0f;
	
	// BestClient
	Data.m_FontSizeBClientIndicator = 18.0f + 20.0f * g_Config.m_BcClientIndicatorInNamePlateSize / 100.0f;

	if(g_Config.m_ClNamePlatesAlways == 0)
		Alpha *= std::clamp(1.0f - std::pow(distance(GameClient()->m_Controls.m_aTargetPos[g_Config.m_ClDummy], Position) / 200.0f, 16.0f), 0.0f, 1.0f);
	if(OtherTeam)
		Alpha *= (float)g_Config.m_ClShowOthersAlpha / 100.0f;
	if(g_Config.m_ClNamePlatesSmartFade)
	{
		const float ScreenHalfW = (ScreenX1 - ScreenX0) * 0.5f;
		const float ScreenHalfH = (ScreenY1 - ScreenY0) * 0.5f;
		if(ScreenHalfW > 0.0f && ScreenHalfH > 0.0f)
		{
			const float ScreenCenterX = (ScreenX0 + ScreenX1) * 0.5f;
			const float ScreenCenterY = (ScreenY0 + ScreenY1) * 0.5f;
			const float EdgeProximityX = absolute((Position.x - ScreenCenterX) / ScreenHalfW);
			const float EdgeProximityY = absolute((Position.y - ScreenCenterY) / ScreenHalfH);
			const float EdgeProximity = std::max(EdgeProximityX, EdgeProximityY);
			const float FadeStartShift = (float)g_Config.m_ClNamePlatesSmartFadeSize / 100.0f;
			const float FadeStart = std::clamp(0.75f - FadeStartShift, 0.0f, 0.99f);
			const float FadeProgress = std::clamp((EdgeProximity - FadeStart) / (1.0f - FadeStart), 0.0f, 1.0f);
			const float FadeStrength = (float)g_Config.m_ClNamePlatesSmartFadeStrength / 100.0f;
			Alpha *= 1.0f - FadeProgress * FadeStrength;
		}
	}
	if(GameClient()->m_FastPractice.Enabled() && !GameClient()->m_FastPractice.IsPracticeParticipant(pPlayerInfo->m_ClientId))
		Alpha = std::min(Alpha, 0.5f);
	if(Alpha <= 0.01f)
		return;

	Data.m_Color = ColorRGBA(1.0f, 1.0f, 1.0f);
	if(g_Config.m_ClNamePlatesTeamcolors)
	{
		if(GameClient()->IsTeamPlay())
		{
			if(ClientData.m_Team == TEAM_RED)
				Data.m_Color = ColorRGBA(1.0f, 0.5f, 0.5f);
			else if(ClientData.m_Team == TEAM_BLUE)
				Data.m_Color = ColorRGBA(0.7f, 0.7f, 1.0f);
		}
		else
		{
			const int Team = GameClient()->m_Teams.Team(pPlayerInfo->m_ClientId);
			if(Team)
				Data.m_Color = GameClient()->GetDDTeamColor(Team, 0.75f);
		}
	}
	Data.m_Color.a = Alpha;

	int ShowDirectionConfig = g_Config.m_ClShowDirection;
#if defined(CONF_VIDEORECORDER)
	if(IVideo::Current())
		ShowDirectionConfig = g_Config.m_ClVideoShowDirection;
#endif
	Data.m_DirLeft = Data.m_DirJump = Data.m_DirRight = false;
	switch(ShowDirectionConfig)
	{
	case 0: // Off
		Data.m_ShowDirection = false;
		break;
	case 1: // Others
		Data.m_ShowDirection = !pPlayerInfo->m_Local;
		break;
	case 2: // Everyone
		Data.m_ShowDirection = true;
		break;
	case 3: // Only self
		Data.m_ShowDirection = pPlayerInfo->m_Local;
		break;
	default:
		dbg_assert_failed("ShowDirectionConfig invalid");
	}
	if(Data.m_ShowDirection)
	{
		if(Client()->State() != IClient::STATE_DEMOPLAYBACK &&
			pPlayerInfo->m_ClientId == GameClient()->m_aLocalIds[!g_Config.m_ClDummy])
		{
			const auto &InputData = GameClient()->m_Controls.m_aInputData[!g_Config.m_ClDummy];
			Data.m_DirLeft = InputData.m_Direction == -1;
			Data.m_DirJump = InputData.m_Jump == 1;
			Data.m_DirRight = InputData.m_Direction == 1;
		}
		else if(Client()->State() != IClient::STATE_DEMOPLAYBACK && pPlayerInfo->m_Local) // Always render local input when not in demo playback
		{
			const auto &InputData = GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];
			Data.m_DirLeft = InputData.m_Direction == -1;
			Data.m_DirJump = InputData.m_Jump == 1;
			Data.m_DirRight = InputData.m_Direction == 1;
		}
		else
		{
			const auto &Character = GameClient()->m_Snap.m_aCharacters[pPlayerInfo->m_ClientId];
			Data.m_DirLeft = Character.m_Cur.m_Direction == -1;
			Data.m_DirJump = Character.m_Cur.m_Jumped & 1;
			Data.m_DirRight = Character.m_Cur.m_Direction == 1;
		}
	}

	Data.m_ShowHookStrongWeak = false;
	Data.m_HookStrongWeakState = EHookStrongWeakState::NEUTRAL;
	Data.m_ShowHookStrongWeakId = false;
	Data.m_HookStrongWeakId = 0;

	Data.m_ShowBClientIndicator = false;
	Data.m_IsUserBClientIndicator = false;
	Data.m_ShowBClientIndicator = g_Config.m_BcClientIndicator && g_Config.m_BcClientIndicatorInNamePlate &&
		(!pPlayerInfo->m_Local || g_Config.m_BcClientIndicatorInNamePlateAboveSelf);
	if(Data.m_ShowBClientIndicator)
	{
		// Check if this player is using BClient
		Data.m_IsUserBClientIndicator = GameClient()->m_ClientIndicator.IsPlayerBClient(pPlayerInfo->m_ClientId);
	}

	const bool Following = (GameClient()->m_Snap.m_SpecInfo.m_Active && !GameClient()->m_MultiViewActivated && GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW);
	if(GameClient()->m_Snap.m_LocalClientId != -1 || Following)
	{
		const int SelectedId = Following ? GameClient()->m_Snap.m_SpecInfo.m_SpectatorId : GameClient()->m_Snap.m_LocalClientId;
		const CGameClient::CSnapState::CCharacterInfo &Selected = GameClient()->m_Snap.m_aCharacters[SelectedId];
		const CGameClient::CSnapState::CCharacterInfo &Other = GameClient()->m_Snap.m_aCharacters[pPlayerInfo->m_ClientId];

		if((Selected.m_HasExtendedData || GameClient()->m_aClients[SelectedId].m_SpecCharPresent) && Other.m_HasExtendedData)
		{
			int SelectedStrongWeakId = Selected.m_HasExtendedData ? Selected.m_ExtendedData.m_StrongWeakId : 0;
			Data.m_HookStrongWeakId = Other.m_ExtendedData.m_StrongWeakId;
			Data.m_ShowHookStrongWeakId = g_Config.m_Debug || g_Config.m_ClNamePlatesStrong == 2;
			if(SelectedId == pPlayerInfo->m_ClientId)
				Data.m_ShowHookStrongWeak = Data.m_ShowHookStrongWeakId;
			else
			{
				Data.m_HookStrongWeakState = SelectedStrongWeakId > Other.m_ExtendedData.m_StrongWeakId ? EHookStrongWeakState::STRONG : EHookStrongWeakState::WEAK;
				Data.m_ShowHookStrongWeak = g_Config.m_Debug || g_Config.m_ClNamePlatesStrong > 0;
			}
		}
	}

	// BestClient
	if(g_Config.m_TcWarList && g_Config.m_TcWarListShowClan && GameClient()->m_WarList.GetWarData(pPlayerInfo->m_ClientId).m_WarClan)
		Data.m_ShowClan = true;
	Data.m_Local = pPlayerInfo->m_Local;

	// Check if the nameplate is actually on screen
	CNamePlate &NamePlate = m_pData->m_aNamePlates[pPlayerInfo->m_ClientId];
	NamePlate.Update(*GameClient(), Data);
	NamePlate.Render(*GameClient(), Position - vec2(0.0f, (float)g_Config.m_ClNamePlatesOffset));
}

void CNamePlates::RenderNamePlatePreview(const CUIRect &PreviewRect, int Dummy)
{
	const float FontSize = 18.0f + 20.0f * g_Config.m_ClNamePlatesSize / 100.0f;
	const float FontSizeClan = 18.0f + 20.0f * g_Config.m_ClNamePlatesClanSize / 100.0f;

	const float FontSizeDirection = 18.0f + 20.0f * g_Config.m_ClDirectionSize / 100.0f;
	const float FontSizeHookStrongWeak = 18.0f + 20.0f * g_Config.m_ClNamePlatesStrongSize / 100.0f;

	// BestClient
	const float FontSizeBClientIndicatorDetection = 18.0f + 20.0f * g_Config.m_BcClientIndicatorInNamePlateSize / 100.0f;

	CNamePlateData Data;
	const bool HasPreviewClient = GameClient()->m_aLocalIds[Dummy] >= 0;
	const int PreviewClientId = Dummy;
	const int PreviewDisplayClientId = HasPreviewClient ? GameClient()->m_aLocalIds[Dummy] : Dummy;

	Data.m_InGame = false;
	Data.m_Local = true;
	Data.m_Color = g_Config.m_ClNamePlatesTeamcolors ? GameClient()->GetDDTeamColor(13, 0.75f) : TextRender()->DefaultTextColor();
	Data.m_Color.a = 1.0f;

	Data.m_ShowName = g_Config.m_ClNamePlates || g_Config.m_ClNamePlatesOwn;
	const char *pName = Dummy == 0 ? Client()->PlayerName() : Client()->DummyName();
	str_copy(Data.m_aName, str_utf8_skip_whitespaces(pName));
	str_utf8_trim_right(Data.m_aName);
	Data.m_FontSize = FontSize;

	Data.m_ShowFriendMark = Data.m_ShowName && g_Config.m_ClNamePlatesFriendMark && HasPreviewClient && GameClient()->m_aClients[PreviewDisplayClientId].m_Friend;

	Data.m_ShowClientId = Data.m_ShowName && (g_Config.m_Debug || g_Config.m_ClNamePlatesIds);
	Data.m_ClientId = PreviewClientId;
	Data.m_DisplayClientId = PreviewDisplayClientId;
	Data.m_ClientIdSeparateLine = g_Config.m_ClNamePlatesIdsSeparateLine;
	Data.m_FontSizeClientId = Data.m_ClientIdSeparateLine ? (18.0f + 20.0f * g_Config.m_ClNamePlatesIdsSize / 100.0f) : Data.m_FontSize;

	Data.m_ShowClan = Data.m_ShowName && g_Config.m_ClNamePlatesClan;
	const char *pClan = Dummy == 0 ? g_Config.m_PlayerClan : g_Config.m_ClDummyClan;
	str_copy(Data.m_aClan, str_utf8_skip_whitespaces(pClan));
	str_utf8_trim_right(Data.m_aClan);
	if(Data.m_aClan[0] == '\0')
		str_copy(Data.m_aClan, "Clan Name");
	Data.m_FontSizeClan = FontSizeClan;

	Data.m_ShowDirection = g_Config.m_ClShowDirection != 0 ? true : false;
	Data.m_DirLeft = Data.m_DirJump = Data.m_DirRight = true;
	Data.m_FontSizeDirection = FontSizeDirection;

	// BestClient
	Data.m_FontSizeBClientIndicator = FontSizeBClientIndicatorDetection;
	Data.m_ShowBClientIndicator = g_Config.m_BcClientIndicator && g_Config.m_BcClientIndicatorInNamePlate &&
		(Dummy != 0 || g_Config.m_BcClientIndicatorInNamePlateAboveSelf);
	Data.m_IsUserBClientIndicator = Data.m_ShowBClientIndicator && (HasPreviewClient ? GameClient()->m_ClientIndicator.IsPlayerBClient(PreviewDisplayClientId) : true);

	Data.m_FontSizeHookStrongWeak = FontSizeHookStrongWeak;
	Data.m_HookStrongWeakId = Data.m_DisplayClientId;
	Data.m_ShowHookStrongWeakId = g_Config.m_ClNamePlatesStrong == 2;
	if(Dummy == g_Config.m_ClDummy)
	{
		Data.m_HookStrongWeakState = EHookStrongWeakState::NEUTRAL;
		Data.m_ShowHookStrongWeak = Data.m_ShowHookStrongWeakId;
	}
	else
	{
		Data.m_HookStrongWeakState = Data.m_HookStrongWeakId == 2 ? EHookStrongWeakState::STRONG : EHookStrongWeakState::WEAK;
		Data.m_ShowHookStrongWeak = g_Config.m_ClNamePlatesStrong > 0;
	}

	CTeeRenderInfo TeeRenderInfo;
	if(Dummy == 0)
	{
		TeeRenderInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClPlayerSkin));
		TeeRenderInfo.ApplyColors(g_Config.m_ClPlayerUseCustomColor, g_Config.m_ClPlayerColorBody, g_Config.m_ClPlayerColorFeet);
	}
	else
	{
		TeeRenderInfo.Apply(GameClient()->m_Skins.Find(g_Config.m_ClDummySkin));
		TeeRenderInfo.ApplyColors(g_Config.m_ClDummyUseCustomColor, g_Config.m_ClDummyColorBody, g_Config.m_ClDummyColorFeet);
	}
	TeeRenderInfo.m_Size = 64.0f;

	CNamePlate NamePlate(*GameClient(), Data);

	CUIRect RenderRect = PreviewRect;
	RenderRect.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.03f), IGraphics::CORNER_ALL, 5.0f);
	RenderRect.DrawOutline(ColorRGBA(1.0f, 1.0f, 1.0f, 0.09f));

	CUIRect SnapRect;
	RenderRect.Margin(6.0f, &SnapRect);

	vec2 Position = SnapRect.Center();
	Position.y += NamePlate.Size().y / 2.0f;
	Position.y += (float)g_Config.m_ClNamePlatesOffset / 2.0f;

	const vec2 TeePosition = Position;
	const vec2 NamePlatePosition = TeePosition - vec2(0.0f, (float)g_Config.m_ClNamePlatesOffset);

	std::vector<CNamePlate::SPreviewPartRect> vPartRects;
	NamePlate.CollectPreviewPartRects(NamePlatePosition, vPartRects);

	enum EDragOperation
	{
		OP_NONE,
		OP_DRAGGING,
		OP_CLICKED,
	};
	static std::array<int, (int)ENamePlateLayoutElement::COUNT> s_aDragIds{};
	static EDragOperation s_DragOperation = OP_NONE;
	static ENamePlateLayoutElement s_DragElement = ENamePlateLayoutElement::NONE;
	static vec2 s_InitialMouse = vec2(0.0f, 0.0f);

	int DragRectIndex = -1;
	for(int i = 0; i < (int)vPartRects.size(); ++i)
	{
		if(vPartRects[i].m_Element == s_DragElement)
		{
			DragRectIndex = i;
			break;
		}
	}
	if(DragRectIndex < 0)
	{
		s_DragOperation = OP_NONE;
		s_DragElement = ENamePlateLayoutElement::NONE;
	}

	const vec2 MousePos = Ui()->MousePos();
	int HoveredRectIndex = -1;
	if(DragRectIndex < 0)
	{
		for(int i = (int)vPartRects.size() - 1; i >= 0; --i)
		{
			CUIRect HandleRect = vPartRects[i].m_Rect;
			HandleRect.x -= 4.0f;
			HandleRect.y -= 4.0f;
			HandleRect.w += 8.0f;
			HandleRect.h += 8.0f;
			if(HandleRect.Inside(MousePos))
			{
				HoveredRectIndex = i;
				break;
			}
		}
	}

	bool ShowVerticalGuide = false;
	bool ShowHorizontalGuide = false;
	float VerticalGuideX = 0.0f;
	float HorizontalGuideY = 0.0f;

	const int LogicRectIndex = DragRectIndex >= 0 ? DragRectIndex : HoveredRectIndex;
	if(LogicRectIndex >= 0)
	{
		const ENamePlateLayoutElement LogicElement = vPartRects[LogicRectIndex].m_Element;
		const int ElementIndex = (int)LogicElement;
		bool Clicked = false;
		bool Abrupted = false;
		if(int Result = Ui()->DoDraggableButtonLogic(&s_aDragIds[ElementIndex], 1, &vPartRects[LogicRectIndex].m_Rect, &Clicked, &Abrupted))
		{
			if(s_DragOperation == OP_NONE && Result == 1)
			{
				s_InitialMouse = Ui()->MousePos();
				s_DragOperation = OP_CLICKED;
				s_DragElement = LogicElement;
			}

			if(Clicked || Abrupted)
			{
				s_DragOperation = OP_NONE;
				s_DragElement = ENamePlateLayoutElement::NONE;
			}

			if(s_DragOperation == OP_CLICKED && s_DragElement == LogicElement && length(Ui()->MousePos() - s_InitialMouse) > 5.0f)
			{
				s_DragOperation = OP_DRAGGING;
				s_InitialMouse -= GetNamePlateLayoutOffset(LogicElement);
			}

			if(s_DragOperation == OP_DRAGGING && s_DragElement == LogicElement)
			{
				const CNamePlate::SPreviewPartRect &ActiveRect = vPartRects[LogicRectIndex];
				vec2 DesiredOffset = Ui()->MousePos() - s_InitialMouse;
				vec2 DesiredCenter = ActiveRect.m_BaseCenter + DesiredOffset;
				const vec2 HalfSize = ActiveRect.m_Rect.Size() / 2.0f;
				DesiredCenter.x = ClampPreviewCenter(DesiredCenter.x, HalfSize.x, SnapRect.x, SnapRect.x + SnapRect.w);
				DesiredCenter.y = ClampPreviewCenter(DesiredCenter.y, HalfSize.y, SnapRect.y, SnapRect.y + SnapRect.h);
				DesiredCenter.x = SnapPreviewAxis(DesiredCenter.x, HalfSize.x, SnapRect.x, SnapRect.x + SnapRect.w, SnapRect.Center().x, true, TeePosition.x, 8.0f, ShowVerticalGuide, VerticalGuideX);
				DesiredCenter.y = SnapPreviewAxis(DesiredCenter.y, HalfSize.y, SnapRect.y, SnapRect.y + SnapRect.h, SnapRect.Center().y, true, TeePosition.y, 8.0f, ShowHorizontalGuide, HorizontalGuideY);
				SetNamePlateLayoutOffset(LogicElement, DesiredCenter - ActiveRect.m_BaseCenter);
				NamePlate.CollectPreviewPartRects(NamePlatePosition, vPartRects);
				for(int i = 0; i < (int)vPartRects.size(); ++i)
				{
					if(vPartRects[i].m_Element == LogicElement)
					{
						DragRectIndex = i;
						break;
					}
				}
			}
		}

		if(HoveredRectIndex >= 0 && Ui()->MouseButtonClicked(1))
		{
			ResetNamePlateLayoutOffset(vPartRects[HoveredRectIndex].m_Element);
			if(s_DragElement == vPartRects[HoveredRectIndex].m_Element)
			{
				s_DragOperation = OP_NONE;
				s_DragElement = ENamePlateLayoutElement::NONE;
			}
			NamePlate.CollectPreviewPartRects(NamePlatePosition, vPartRects);
		}
	}

	// tee looking towards cursor, and it is happy when you touch it
	const vec2 DeltaPosition = Ui()->MousePos() - TeePosition;
	const float Distance = length(DeltaPosition);
	const float InteractionDistance = 20.0f;
	const vec2 TeeDirection = Distance < InteractionDistance ? normalize(vec2(DeltaPosition.x, maximum(DeltaPosition.y, 0.5f))) : normalize(DeltaPosition);
	const int TeeEmote = Distance < InteractionDistance ? EMOTE_HAPPY : (Dummy ? g_Config.m_ClDummyDefaultEyes : g_Config.m_ClPlayerDefaultEyes);
	RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, TeeEmote, TeeDirection, TeePosition);
	NamePlate.Render(*GameClient(), NamePlatePosition);

	if(ShowVerticalGuide)
		Graphics()->DrawRect(VerticalGuideX - Ui()->PixelSize(), SnapRect.y, Ui()->PixelSize() * 2.0f, SnapRect.h, ColorRGBA(1.0f, 0.87f, 0.32f, 0.55f), IGraphics::CORNER_NONE, 0.0f);
	if(ShowHorizontalGuide)
		Graphics()->DrawRect(SnapRect.x, HorizontalGuideY - Ui()->PixelSize(), SnapRect.w, Ui()->PixelSize() * 2.0f, ColorRGBA(1.0f, 0.87f, 0.32f, 0.55f), IGraphics::CORNER_NONE, 0.0f);

	for(int i = 0; i < (int)vPartRects.size(); ++i)
	{
		CUIRect OutlineRect = vPartRects[i].m_Rect;
		OutlineRect.x -= 2.0f;
		OutlineRect.y -= 2.0f;
		OutlineRect.w += 4.0f;
		OutlineRect.h += 4.0f;

		const bool Active = (s_DragElement == vPartRects[i].m_Element) && s_DragOperation == OP_DRAGGING;
		const bool Hovered = i == HoveredRectIndex;
		const ColorRGBA FillColor = Active ? ColorRGBA(0.98f, 0.78f, 0.22f, 0.16f) :
			Hovered ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.10f) :
			ColorRGBA(1.0f, 1.0f, 1.0f, 0.035f);
		const ColorRGBA OutlineColor = Active ? ColorRGBA(1.0f, 0.87f, 0.32f, 0.9f) :
			Hovered ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.55f) :
			ColorRGBA(1.0f, 1.0f, 1.0f, 0.16f);
		OutlineRect.Draw(FillColor, IGraphics::CORNER_ALL, 3.0f);
		OutlineRect.DrawOutline(OutlineColor);
	}

	NamePlate.Reset(*GameClient());
}

void CNamePlates::ResetNamePlateElementOffsets()
{
	for(int i = 0; i < (int)ENamePlateLayoutElement::COUNT; ++i)
		ResetNamePlateLayoutOffset((ENamePlateLayoutElement)i);
}

void CNamePlates::ResetNamePlates()
{
	for(CNamePlate &NamePlate : m_pData->m_aNamePlates)
		NamePlate.Reset(*GameClient());
}

void CNamePlates::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	int ShowDirection = g_Config.m_ClShowDirection;
#if defined(CONF_VIDEORECORDER)
	if(IVideo::Current())
		ShowDirection = g_Config.m_ClVideoShowDirection;
#endif
	if(!g_Config.m_ClNamePlates && !g_Config.m_ClNamePlatesOwn && ShowDirection == 0)
		return;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apPlayerInfos[i];
		if(!pInfo)
			continue;

		// Each player can also have a spectator char whose name plate is displayed independently
		if(GameClient()->m_aClients[i].m_SpecCharPresent)
		{
			const vec2 RenderPos = GameClient()->m_aClients[i].m_SpecChar;
			if(GameClient()->m_BestClient.OptimizerAllowRenderPos(RenderPos))
				RenderNamePlateGame(RenderPos, pInfo, 0.4f);
		}
		// Only render name plates for active characters
		if(GameClient()->m_Snap.m_aCharacters[i].m_Active)
		{
			// BestClient
			if(GameClient()->m_aClients[i].m_IsVolleyBall)
				continue;
			// if(g_Config.m_TcRenderNameplateSpec > 0)
			//	continue;
			const vec2 RenderPos = GameClient()->m_aClients[i].m_RenderPos;
			if(!GameClient()->m_BestClient.OptimizerAllowRenderPos(RenderPos))
				continue;
			RenderNamePlateGame(RenderPos, pInfo, 1.0f);
		}
	}
}

void CNamePlates::OnWindowResize()
{
	ResetNamePlates();
}

CNamePlates::CNamePlates() :
	m_pData(new CNamePlates::CNamePlatesData()) {}

CNamePlates::~CNamePlates()
{
	delete m_pData;
}

float CNamePlates::GetNamePlateOffset(int ClientId) const
{
	if(!m_pData || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return 0.0f;

	const CNamePlate &NamePlate = m_pData->m_aNamePlates[ClientId];
	if(!NamePlate.IsInited())
		return 0.0f;

	return NamePlate.Size().y;
}
