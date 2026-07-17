/* Copyright © 2026 BestProject Team */
#include "gif_wheel.h"
#include "cherry_gifs_cache.h"

#include <base/math.h>
#include <base/mem.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/http.h>
#include <engine/storage.h>

#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/components/media_decoder.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cmath>

// Slots are only ever meant to hold CherryGifs links (added via drag&drop from the browser, or
// through add_gifwheel). This is checked again here regardless, so a hand-edited/corrupted config
// entry can't make the client fetch from - or blast a link to - somewhere else entirely.
static bool IsCherryGifsUrl(const char *pUrl)
{
	const char *pHost = str_startswith(pUrl, "https://");
	if(!pHost)
		return false;
	const char *pAfterHost = str_startswith(pHost, "gifs.teeworlds.xyz");
	if(!pAfterHost)
		return false;
	return *pAfterHost == '\0' || *pAfterHost == '/' || *pAfterHost == ':' || *pAfterHost == '?';
}

// Same magic-byte allowlist as cherry_gifs.cpp: downloaded bytes are only ever handed to the
// decoder if they actually look like one of these image formats, whatever the server claims.
static bool IsGifSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 6 && (mem_comp(pData, "GIF87a", 6) == 0 || mem_comp(pData, "GIF89a", 6) == 0);
}

static bool IsPngSignature(const unsigned char *pData, size_t DataSize)
{
	static const unsigned char s_aPngSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	return DataSize >= 8 && mem_comp(pData, s_aPngSig, 8) == 0;
}

static bool IsJpegSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 3 && pData[0] == 0xff && pData[1] == 0xd8 && pData[2] == 0xff;
}

static bool IsWebpSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 12 && mem_comp(pData, "RIFF", 4) == 0 && mem_comp(pData + 8, "WEBP", 4) == 0;
}

static bool IsBmpSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 2 && pData[0] == 'B' && pData[1] == 'M';
}

static bool IsRecognizedImageSignature(const unsigned char *pData, size_t DataSize)
{
	return IsGifSignature(pData, DataSize) || IsPngSignature(pData, DataSize) || IsJpegSignature(pData, DataSize) || IsWebpSignature(pData, DataSize) || IsBmpSignature(pData, DataSize);
}

// Console command name for each eye emote (lowercase, as expected by "/emote <name> <seconds>").
static const char *EyeEmoteCommandName(int Emote)
{
	switch(Emote)
	{
	case EMOTE_NORMAL: return "normal";
	case EMOTE_PAIN: return "pain";
	case EMOTE_HAPPY: return "happy";
	case EMOTE_SURPRISE: return "surprise";
	case EMOTE_ANGRY: return "angry";
	case EMOTE_BLINK: return "blink";
	default: return nullptr;
	}
}

// Click zone (in the same world-space units as the segment layout below) for the switch-back-to-
// emotes button, dead center. Must match EMOTICON_CENTER_RADIUS in emoticon.cpp.
static constexpr float GIFWHEEL_CENTER_BUTTON_RADIUS = 40.0f;

CGifWheel::CGifWheel()
{
	OnReset();
}

void CGifWheel::ConGifWheelExecuteHover(IConsole::IResult *pResult, void *pUserData)
{
	CGifWheel *pThis = (CGifWheel *)pUserData;
	pThis->ExecuteHoveredSlot();
}

void CGifWheel::ConAddGifWheel(IConsole::IResult *pResult, void *pUserData)
{
	const char *pGifId = pResult->GetString(0);
	const char *pUrl = pResult->GetString(1);
	const char *pCaption = pResult->GetString(2);
	const int EyeEmote = pResult->NumArguments() >= 4 ? pResult->GetInteger(3) : -1;

	CGifWheel *pThis = static_cast<CGifWheel *>(pUserData);
	pThis->AddSlot(pGifId, pUrl, pCaption, EyeEmote >= 0 && EyeEmote < NUM_EMOTES ? EyeEmote : -1);
}

void CGifWheel::ConRemoveGifWheel(IConsole::IResult *pResult, void *pUserData)
{
	const char *pGifId = pResult->GetString(0);

	CGifWheel *pThis = static_cast<CGifWheel *>(pUserData);
	for(int i = 0; i < (int)pThis->m_vSlots.size(); i++)
	{
		if(str_comp(pThis->m_vSlots[i].m_aGifId, pGifId) == 0)
		{
			pThis->RemoveSlot(i);
			return;
		}
	}
}

void CGifWheel::ConRemoveAllGifWheelSlots(IConsole::IResult *pResult, void *pUserData)
{
	CGifWheel *pThis = static_cast<CGifWheel *>(pUserData);
	pThis->RemoveAllSlots();
}

void CGifWheel::AddSlot(const char *pGifId, const char *pUrl, const char *pCaption, int EyeEmote)
{
	if((pGifId[0] == '\0' && pUrl[0] == '\0') || m_vSlots.size() >= GIFWHEEL_MAX_SLOTS)
		return;

	CSlot Slot;
	str_copy(Slot.m_aGifId, pGifId);
	str_copy(Slot.m_aUrl, pUrl);
	str_copy(Slot.m_aCaption, pCaption);
	Slot.m_EyeEmote = EyeEmote;
	m_vSlots.push_back(std::move(Slot));
}

void CGifWheel::SetSlotEyeEmote(int Index, int EyeEmote)
{
	if(Index < 0 || Index >= (int)m_vSlots.size())
		return;
	m_vSlots[Index].m_EyeEmote = EyeEmote;
}

void CGifWheel::RemoveSlot(int Index)
{
	if(Index < 0 || Index >= (int)m_vSlots.size())
		return;
	Graphics()->UnloadTexture(&m_vSlots[Index].m_Thumbnail);
	m_vSlots.erase(m_vSlots.begin() + Index);
	if(m_SelectedSlot >= (int)m_vSlots.size())
		m_SelectedSlot = -1;
}

void CGifWheel::RemoveAllSlots()
{
	for(CSlot &Slot : m_vSlots)
		Graphics()->UnloadTexture(&Slot.m_Thumbnail);
	m_vSlots.clear();
	m_SelectedSlot = -1;
}

void CGifWheel::OnConsoleInit()
{
	IConfigManager *pConfigManager = Kernel()->RequestInterface<IConfigManager>();
	if(pConfigManager)
		pConfigManager->RegisterCallback(ConfigSaveCallback, this, ConfigDomain::BESTCLIENT);

	Console()->Register("+gifwheel_execute_hover", "", CFGFLAG_CLIENT, ConGifWheelExecuteHover, this, "Execute hovered gif wheel slot");

	Console()->Register("add_gifwheel", "s[gif_id] s[url] s[caption] ?i[eye_emote]", CFGFLAG_CLIENT, ConAddGifWheel, this, "Add a gif to the gif wheel");
	Console()->Register("remove_gifwheel", "s[gif_id]", CFGFLAG_CLIENT, ConRemoveGifWheel, this, "Remove a gif from the gif wheel by id");
	Console()->Register("delete_all_gifwheel_slots", "", CFGFLAG_CLIENT, ConRemoveAllGifWheelSlots, this, "Removes all gif wheel slots");
}

void CGifWheel::OnReset()
{
	m_WasActive = false;
	m_Active = false;
	m_SelectedSlot = -1;
	m_SelectedEyeEmote = -1;
}

void CGifWheel::OnRelease()
{
	m_Active = false;
}

void CGifWheel::Activate()
{
	m_Active = true;
	EnsureThumbnails();
}

bool CGifWheel::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_Active)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	GameClient()->m_Emoticon.m_SelectorMouse += vec2(x, y);
	return true;
}

bool CGifWheel::OnInput(const IInput::CEvent &Event)
{
	if(!IsActive())
		return false;

	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_ESCAPE)
	{
		OnRelease();
		return true;
	}

	// Aiming at the center (no segment/eye selected either way) and clicking switches back to the
	// emote wheel, staying within the same held-key session.
	const float WheelScale = std::clamp(g_Config.m_BcWheelScale / 100.0f, 0.5f, 2.0f);
	if(Event.m_Flags & IInput::FLAG_PRESS && Event.m_Key == KEY_MOUSE_1 && length(GameClient()->m_Emoticon.m_SelectorMouse) < GIFWHEEL_CENTER_BUTTON_RADIUS * WheelScale)
	{
		GameClient()->m_Emoticon.m_PreferGifWheel = false;
		GameClient()->m_Emoticon.Activate();
		OnRelease();
		return true;
	}

	return false;
}

void CGifWheel::EnsureThumbnail(CSlot &Slot)
{
	if(Slot.m_Thumbnail.IsValid() || Slot.m_ThumbnailRequested || Slot.m_ThumbnailFailed || Slot.m_aUrl[0] == '\0')
		return;
	if(!IsCherryGifsUrl(Slot.m_aUrl))
	{
		Slot.m_ThumbnailFailed = true;
		return;
	}

	// Disk cache hit: the browser (or a previous session) may have already downloaded this exact
	// gif - decode straight from disk, no network round trip.
	std::vector<unsigned char> vCached;
	if(CherryGifsCache::Load(Storage(), Slot.m_aGifId, vCached) && IsRecognizedImageSignature(vCached.data(), vCached.size()))
	{
		Slot.m_ThumbnailRequested = true;
		SMediaDecodedFrames DecodedFrames;
		if(MediaDecoder::DecodeStaticImageCpu(Graphics(), vCached.data(), vCached.size(), Slot.m_aGifId, DecodedFrames, 256))
		{
			std::vector<SMediaFrame> vFrames;
			if(MediaDecoder::UploadFrames(Graphics(), DecodedFrames, vFrames, Slot.m_aGifId) && !vFrames.empty())
			{
				Slot.m_Thumbnail = vFrames.front().m_Texture;
				return;
			}
		}
		Slot.m_ThumbnailFailed = true;
		return;
	}

	Slot.m_ThumbnailRequested = true;

	std::shared_ptr<CHttpRequest> pGet = HttpGet(Slot.m_aUrl);
	pGet->Timeout(CTimeout{8000, 0, 4096, 8});
	pGet->MaxResponseSize(8 * 1024 * 1024);
	pGet->FailOnErrorStatus(false);
	pGet->LogProgress(HTTPLOG::NONE);
	Slot.m_pThumbnailRequest = pGet;
	Http()->Run(pGet);
}

void CGifWheel::PollThumbnails()
{
	for(CSlot &Slot : m_vSlots)
	{
		if(!Slot.m_pThumbnailRequest || !Slot.m_pThumbnailRequest->Done())
			continue;

		if(Slot.m_pThumbnailRequest->State() == EHttpState::DONE && Slot.m_pThumbnailRequest->StatusCode() == 200)
		{
			unsigned char *pData = nullptr;
			size_t DataSize = 0;
			Slot.m_pThumbnailRequest->Result(&pData, &DataSize);
			if(pData && DataSize > 0 && IsRecognizedImageSignature(pData, DataSize))
			{
				SMediaDecodedFrames DecodedFrames;
				if(MediaDecoder::DecodeStaticImageCpu(Graphics(), pData, DataSize, Slot.m_aGifId, DecodedFrames, 256))
				{
					std::vector<SMediaFrame> vFrames;
					if(MediaDecoder::UploadFrames(Graphics(), DecodedFrames, vFrames, Slot.m_aGifId) && !vFrames.empty())
					{
						Slot.m_Thumbnail = vFrames.front().m_Texture;
						CherryGifsCache::Save(Storage(), Slot.m_aGifId, pData, DataSize);
					}
					else
					{
						Slot.m_ThumbnailFailed = true;
					}
				}
				else
				{
					Slot.m_ThumbnailFailed = true;
				}
			}
			else
			{
				Slot.m_ThumbnailFailed = true;
			}
		}
		else
		{
			Slot.m_ThumbnailFailed = true;
		}
		Slot.m_pThumbnailRequest = nullptr;
	}
}

void CGifWheel::EnsureThumbnails()
{
	for(CSlot &Slot : m_vSlots)
		EnsureThumbnail(Slot);
}

void CGifWheel::OnRender()
{
	// Runs regardless of connection state: the settings UI (offline, main menu) also needs
	// thumbnails to load and their downloads to be polled to completion.
	PollThumbnails();

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	static const auto QuadEaseInOut = [](float t) -> float {
		if(t == 0.0f)
			return 0.0f;
		if(t == 1.0f)
			return 1.0f;
		return (t < 0.5f) ? (2.0f * t * t) : (1.0f - std::pow(-2.0f * t + 2.0f, 2) / 2.0f);
	};
	static const auto PositiveMod = [](float x, float y) -> float {
		return std::fmod(x + y, y);
	};

	const float WheelScale = std::clamp(g_Config.m_BcWheelScale / 100.0f, 0.5f, 2.0f);
	const float s_InnerOuterMouseBoundaryRadius = 110.0f * WheelScale;
	const float s_OuterMouseLimitRadius = 170.0f * WheelScale;
	const float s_OuterItemRadius = 140.0f * WheelScale;
	const float s_OuterCircleRadius = 190.0f * WheelScale;
	const float s_ThumbnailSize = 64.0f * WheelScale;
	const float s_ThumbnailSizeSelected = 88.0f * WheelScale;
	const float s_InnerCircleRadius = 100.0f * WheelScale;
	const float s_InnerItemRadius = 70.0f * WheelScale;

	const float AnimationTime = (float)g_Config.m_TcAnimateWheelTime / 1000.0f;
	const float ItemAnimationTime = AnimationTime / 2.0f;

	if(AnimationTime != 0.0f)
	{
		for(float &Time : m_aAnimationTimeItems)
			Time = std::max(0.0f, Time - Client()->RenderFrameTime());
		for(float &Time : m_aAnimationTimeEyeEmotes)
			Time = std::max(0.0f, Time - Client()->RenderFrameTime());
	}

	if(!m_Active)
	{
		if(m_WasActive && m_SelectedSlot != -1)
			ExecuteSlot(m_SelectedSlot);
		else if(m_WasActive && m_SelectedEyeEmote != -1)
			GameClient()->m_Emoticon.EyeEmote(m_SelectedEyeEmote);
		m_WasActive = false;

		if(AnimationTime == 0.0f)
			return;

		m_AnimationTime -= Client()->RenderFrameTime() * 3.0f; // Close animation 3x faster
		if(m_AnimationTime <= 0.0f)
		{
			m_AnimationTime = 0.0f;
			return;
		}
	}
	else
	{
		if(!m_WasActive)
		{
			for(CSlot &Slot : m_vSlots)
				EnsureThumbnail(Slot);
		}
		m_AnimationTime += Client()->RenderFrameTime();
		if(m_AnimationTime > AnimationTime)
			m_AnimationTime = AnimationTime;
		m_WasActive = true;
	}

	const CUIRect Screen = *Ui()->Screen();

	std::array<float, 2> aAnimationPhase;
	if(AnimationTime == 0.0f)
	{
		aAnimationPhase.fill(1.0f);
	}
	else
	{
		aAnimationPhase[0] = QuadEaseInOut(m_AnimationTime / AnimationTime);
		aAnimationPhase[1] = aAnimationPhase[0] * aAnimationPhase[0];
	}

	if(length(GameClient()->m_Emoticon.m_SelectorMouse) > s_OuterMouseLimitRadius)
		GameClient()->m_Emoticon.m_SelectorMouse = normalize(GameClient()->m_Emoticon.m_SelectorMouse) * s_OuterMouseLimitRadius;

	const float MouseDistance = length(GameClient()->m_Emoticon.m_SelectorMouse);
	const float SelectedAngle = angle(GameClient()->m_Emoticon.m_SelectorMouse);

	const int SegmentCount = (int)m_vSlots.size();
	if(SegmentCount == 0 || MouseDistance <= s_InnerOuterMouseBoundaryRadius)
		m_SelectedSlot = -1;
	else
		m_SelectedSlot = (int)PositiveMod(std::round(SelectedAngle / (2.0f * pi) * SegmentCount), SegmentCount);

	// Inner ring: live eye emote pick, same mechanic as the regular emote wheel's own eye ring -
	// lets you change your eyes independently of (or without) sending a gif this same session.
	const bool EyeWheelAllowed = GameClient()->m_GameInfo.m_AllowEyeWheel && g_Config.m_ClEyeWheel && GameClient()->m_aLocalIds[g_Config.m_ClDummy] >= 0;
	if(EyeWheelAllowed && MouseDistance > GIFWHEEL_CENTER_BUTTON_RADIUS * WheelScale && MouseDistance <= s_InnerOuterMouseBoundaryRadius)
		m_SelectedEyeEmote = (int)PositiveMod(std::round(SelectedAngle / (2.0f * pi) * (float)NUM_EMOTES), NUM_EMOTES);
	else
		m_SelectedEyeEmote = -1;

	if(m_SelectedSlot != -1)
	{
		m_aAnimationTimeItems[m_SelectedSlot] += Client()->RenderFrameTime() * 2.0f;
		if(m_aAnimationTimeItems[m_SelectedSlot] >= ItemAnimationTime)
			m_aAnimationTimeItems[m_SelectedSlot] = ItemAnimationTime;
	}
	if(m_SelectedEyeEmote != -1)
	{
		m_aAnimationTimeEyeEmotes[m_SelectedEyeEmote] += Client()->RenderFrameTime() * 2.0f;
		if(m_aAnimationTimeEyeEmotes[m_SelectedEyeEmote] >= ItemAnimationTime)
			m_aAnimationTimeEyeEmotes[m_SelectedEyeEmote] = ItemAnimationTime;
	}

	Ui()->MapScreen();

	Graphics()->BlendNormal();
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.3f * aAnimationPhase[0]);
	Graphics()->DrawCircle(Screen.w / 2.0f, Screen.h / 2.0f, s_OuterCircleRadius * aAnimationPhase[0], 64);
	Graphics()->QuadsEnd();

	const vec2 ScreenCenter = Screen.Center();
	const float Theta = pi * 2.0f / std::max<float>(1.0f, (float)m_vSlots.size());
	for(int i = 0; i < (int)m_vSlots.size(); i++)
	{
		CSlot &Slot = m_vSlots[i];
		const float Angle = Theta * i;
		const float Phase = ItemAnimationTime == 0.0f ? (i == m_SelectedSlot ? 1.0f : 0.0f) : QuadEaseInOut(m_aAnimationTimeItems[i] / ItemAnimationTime);
		const float Size = (s_ThumbnailSize + Phase * (s_ThumbnailSizeSelected - s_ThumbnailSize)) * aAnimationPhase[1];
		const vec2 Pos = ScreenCenter + direction(Angle) * s_OuterItemRadius * aAnimationPhase[1];

		if(Slot.m_Thumbnail.IsValid())
		{
			Graphics()->WrapClamp();
			Graphics()->TextureSet(Slot.m_Thumbnail);
			Graphics()->QuadsSetSubset(0, 0, 1, 1);
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, aAnimationPhase[1]);
			IGraphics::CQuadItem QuadItem(Pos.x - Size / 2.0f, Pos.y - Size / 2.0f, Size, Size);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
			Graphics()->WrapNormal();
		}
		else
		{
			Graphics()->DrawRect(Pos.x - Size / 2.0f, Pos.y - Size / 2.0f, Size, Size, ColorRGBA(0.15f, 0.15f, 0.15f, 0.6f * aAnimationPhase[1]), IGraphics::CORNER_ALL, Size * 0.15f);
		}
	}

	// Inner eye ring - same visual as the regular emote wheel's own eye ring, lets you pick an
	// eye emote live without necessarily sending a gif.
	if(EyeWheelAllowed)
	{
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.3f * aAnimationPhase[1]);
		Graphics()->DrawCircle(ScreenCenter.x, ScreenCenter.y, s_InnerCircleRadius * aAnimationPhase[1], 64);
		Graphics()->QuadsEnd();

		CTeeRenderInfo TeeInfo = GameClient()->m_aClients[GameClient()->m_aLocalIds[g_Config.m_ClDummy]].m_RenderInfo;
		for(int Emote = 0; Emote < NUM_EMOTES; Emote++)
		{
			float Angle = 2.0f * pi * Emote / (float)NUM_EMOTES;
			if(Angle > pi)
				Angle -= 2.0f * pi;

			const vec2 Nudge = direction(Angle) * s_InnerItemRadius * aAnimationPhase[1];
			const float Phase = ItemAnimationTime == 0.0f ? (Emote == m_SelectedEyeEmote ? 1.0f : 0.0f) : QuadEaseInOut(m_aAnimationTimeEyeEmotes[Emote] / ItemAnimationTime);
			TeeInfo.m_Size = (48.0f + Phase * 18.0f) * WheelScale * aAnimationPhase[1];
			RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, Emote, vec2(-1.0f, 0.0f), ScreenCenter + Nudge, aAnimationPhase[1]);
		}
	}

	// Switch-back-to-emotes button, dead center (mirrors the one CEmoticon draws for switching
	// the other way; drawn last so it sits on top of the eye ring).
	{
		const float ButtonRadius = 26.0f * WheelScale;
		Graphics()->DrawRect(ScreenCenter.x - ButtonRadius, ScreenCenter.y - ButtonRadius, ButtonRadius * 2.0f, ButtonRadius * 2.0f,
			ColorRGBA(0.0f, 0.0f, 0.0f, 0.55f * aAnimationPhase[0]), IGraphics::CORNER_ALL, ButtonRadius);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f * aAnimationPhase[0]);
		CUIRect CenterButtonRect{ScreenCenter.x - ButtonRadius, ScreenCenter.y - 8.0f * WheelScale, ButtonRadius * 2.0f, 16.0f * WheelScale};
		Ui()->DoLabel(&CenterButtonRect, Localize("Emotes"), 11.0f * WheelScale * aAnimationPhase[0], TEXTALIGN_MC);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}

	if(m_vSlots.empty())
	{
		TextRender()->TextColor(0.8f, 0.8f, 0.8f, aAnimationPhase[1]);
		CUIRect EmptyRect{ScreenCenter.x - 130.0f * WheelScale, ScreenCenter.y + s_InnerCircleRadius + 16.0f * WheelScale, 260.0f * WheelScale, 20.0f * WheelScale};
		Ui()->DoLabel(&EmptyRect, Localize("No gifs assigned yet - drag some in from Settings"), 13.0f * WheelScale, TEXTALIGN_MC);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}

	RenderTools()->RenderCursor(GameClient()->m_Emoticon.m_SelectorMouse + ScreenCenter, 24.0f * WheelScale, aAnimationPhase[0]);
}

void CGifWheel::ExecuteSlot(int Index)
{
	if(Index < 0 || Index >= (int)m_vSlots.size() || m_vSlots[Index].m_aUrl[0] == '\0')
		return;
	// Last line of defense: only ever post links that are actually pinned to CherryGifs, in case
	// a slot's url was ever tampered with (e.g. a hand-edited settings_BestClient.cfg).
	if(!IsCherryGifsUrl(m_vSlots[Index].m_aUrl))
		return;
	// Just a normal chat message with the gif's direct link; the server sees plain text.
	// Chat Media (inline preview) and the above-head bubble both key off that link client-side.
	GameClient()->m_Chat.SendChat(0, m_vSlots[Index].m_aUrl);

	// Same as picking an eye emote from the regular emote wheel: a separate /emote chat command,
	// applied on top of (not instead of) the gif. Duration is tied to the gif bubble's own
	// lifetime (not the user's regular cl_eye_duration, which usually stays "forever") so the
	// eyes revert exactly when the bubble disappears, same as a normal temporary eye emote.
	const char *pEyeEmoteCommand = EyeEmoteCommandName(m_vSlots[Index].m_EyeEmote);
	if(pEyeEmoteCommand)
	{
		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "/emote %s %d", pEyeEmoteCommand, std::max(1, g_Config.m_BcGifBubbleDurationMs / 1000));
		GameClient()->m_Chat.SendChat(0, aBuf);
	}
}

void CGifWheel::ExecuteHoveredSlot()
{
	if(m_SelectedSlot >= 0)
		ExecuteSlot(m_SelectedSlot);
}

void CGifWheel::ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	CGifWheel *pThis = (CGifWheel *)pUserData;

	for(CSlot &Slot : pThis->m_vSlots)
	{
		char aBuf[32 * 2 + 256 * 2 + 128 * 2 + 32] = "";
		char *pEnd = aBuf + sizeof(aBuf);
		char *pDst;
		str_append(aBuf, "add_gifwheel \"");
		pDst = aBuf + str_length(aBuf);
		str_escape(&pDst, Slot.m_aGifId, pEnd);
		str_append(aBuf, "\" \"");
		pDst = aBuf + str_length(aBuf);
		str_escape(&pDst, Slot.m_aUrl, pEnd);
		str_append(aBuf, "\" \"");
		pDst = aBuf + str_length(aBuf);
		str_escape(&pDst, Slot.m_aCaption, pEnd);
		str_append(aBuf, "\" ");
		char aEyeEmote[8];
		str_format(aEyeEmote, sizeof(aEyeEmote), "%d", Slot.m_EyeEmote);
		str_append(aBuf, aEyeEmote);
		pConfigManager->WriteLine(aBuf, ConfigDomain::BESTCLIENT);
	}
}
