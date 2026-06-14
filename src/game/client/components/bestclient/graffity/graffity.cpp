/* Copyright © 2026 BestProject Team */
#include "graffity.h"

#include <base/color.h>
#include <base/hash_ctxt.h>
#include <base/log.h>
#include <base/math.h>
#include <base/net.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>

#include <game/client/components/sounds.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/layers.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <inttypes.h>
#include <string>
#include <utility>

namespace
{
	constexpr float GRAFFITY_WORLD_SIZE = 32.0f;
	constexpr int MIN_GRAFFITY_SIZE_STEP = 1;
	constexpr int MAX_GRAFFITY_SIZE_STEP = 6;
	constexpr float WHEEL_OPEN_TIME = 0.12f;
	constexpr float ITEM_HOVER_TIME = 0.08f;
	constexpr float WHEEL_INNER_RADIUS = 30.0f;
	constexpr float WHEEL_SELECT_RADIUS = 80.0f;
	constexpr float WHEEL_MAX_CURSOR_RADIUS = 340.0f;
	constexpr float WHEEL_ITEM_RADIUS = 104.0f;
	constexpr float WHEEL_CIRCLE_RADIUS = 138.0f;
	constexpr float GRAFFITY_FADE_IN_TIME = 0.18f;
	constexpr int64_t GRAFFITY_HELLO_RESYNC_INTERVAL = 20;
	constexpr int DEFAULT_GRAFFITY_SERVER_PORT = 8781;
	constexpr int GRAFFITY_AUTH_KEY_SIZE = 32;
	constexpr int GRAFFITY_AUTH_HEX_SIZE = SHA256_DIGEST_LENGTH * 2 + 1;
	const uint8_t s_aGraffityAuthKeyObfuscated[GRAFFITY_AUTH_KEY_SIZE] = {
		0x32, 0x02, 0x15, 0x39, 0x00, 0x2c, 0x37, 0x16,
		0x09, 0x69, 0x3b, 0x3b, 0x12, 0x0a, 0x2a, 0x69,
		0x09, 0x3b, 0x03, 0x1d, 0x31, 0x2b, 0x6d, 0x6e,
		0x33, 0x20, 0x29, 0x35, 0x29, 0x69, 0x0a, 0x0b,
	};
	constexpr uint8_t GRAFFITY_AUTH_XOR_KEY = 0x5A;
	constexpr uint8_t GRAFFITY_SERVER_ADDRESS_XOR_KEY = 0x4B;
	const uint8_t s_aGraffityServerAddressObfuscated[] = {
		0x7a, 0x72, 0x78, 0x65, 0x79, 0x78, 0x65, 0x79, 0x7b, 0x7a,
		0x65, 0x7a, 0x79, 0x7e, 0x71, 0x73, 0x7c, 0x73, 0x7a,
	};

	void DeobfuscateGraffityAuthKey(uint8_t *pOut)
	{
		for(int i = 0; i < GRAFFITY_AUTH_KEY_SIZE; ++i)
			pOut[i] = s_aGraffityAuthKeyObfuscated[i] ^ GRAFFITY_AUTH_XOR_KEY;
	}

	void HmacSha256(const uint8_t *pKey, int KeyLen, const uint8_t *pData, int DataLen, uint8_t *pOut)
	{
		uint8_t aKeyPad[64] = {};
		uint8_t aIpad[64];
		uint8_t aOpad[64];
		memcpy(aKeyPad, pKey, KeyLen);

		for(int i = 0; i < 64; ++i)
		{
			aIpad[i] = aKeyPad[i] ^ 0x36;
			aOpad[i] = aKeyPad[i] ^ 0x5c;
		}

		SHA256_CTX Ctx;
		sha256_init(&Ctx);
		sha256_update(&Ctx, aIpad, sizeof(aIpad));
		sha256_update(&Ctx, pData, DataLen);
		SHA256_DIGEST Inner = sha256_finish(&Ctx);

		sha256_init(&Ctx);
		sha256_update(&Ctx, aOpad, sizeof(aOpad));
		sha256_update(&Ctx, Inner.data, sizeof(Inner.data));
		SHA256_DIGEST Digest = sha256_finish(&Ctx);
		memcpy(pOut, Digest.data, SHA256_DIGEST_LENGTH);
	}

	void LowerHex(const uint8_t *pData, int DataLen, char *pOut, int OutSize)
	{
		static const char s_aHex[] = "0123456789abcdef";
		if(OutSize <= DataLen * 2)
		{
			if(OutSize > 0)
				pOut[0] = '\0';
			return;
		}

		for(int i = 0; i < DataLen; ++i)
		{
			pOut[i * 2] = s_aHex[pData[i] >> 4];
			pOut[i * 2 + 1] = s_aHex[pData[i] & 0x0f];
		}
		pOut[DataLen * 2] = '\0';
	}

	void ComputeGraffityHelloAuth(char *pOut, int OutSize, const char *pServerAddress, const char *pOwnerId, int64_t Timestamp)
	{
		uint8_t aKey[GRAFFITY_AUTH_KEY_SIZE];
		DeobfuscateGraffityAuthKey(aKey);

		const std::string Payload = "hello\n" + std::to_string(Timestamp) + "\n" + pServerAddress + "\n" + pOwnerId;
		uint8_t aDigest[SHA256_DIGEST_LENGTH];
		HmacSha256(aKey, sizeof(aKey), reinterpret_cast<const uint8_t *>(Payload.data()), (int)Payload.size(), aDigest);
		LowerHex(aDigest, sizeof(aDigest), pOut, OutSize);
	}

	std::string BuildGraffityHelloMessage(const std::string &GameServerAddress, const std::string &OwnerId, int64_t Timestamp)
	{
		char aAuth[GRAFFITY_AUTH_HEX_SIZE];
		ComputeGraffityHelloAuth(aAuth, sizeof(aAuth), GameServerAddress.c_str(), OwnerId.c_str(), Timestamp);

		char aHello[768];
		str_format(aHello, sizeof(aHello),
			"{\"type\":\"hello\",\"server_address\":\"%s\",\"owner_id\":\"%s\",\"timestamp\":%" PRId64 ",\"auth\":\"%s\"}",
			GameServerAddress.c_str(),
			OwnerId.c_str(),
			Timestamp,
			aAuth);
		return aHello;
	}

	std::string DefaultGraffityServerAddress()
	{
		char aAddress[sizeof(s_aGraffityServerAddressObfuscated) + 1];
		for(int i = 0; i < (int)sizeof(s_aGraffityServerAddressObfuscated); ++i)
			aAddress[i] = s_aGraffityServerAddressObfuscated[i] ^ GRAFFITY_SERVER_ADDRESS_XOR_KEY;
		aAddress[sizeof(s_aGraffityServerAddressObfuscated)] = '\0';
		return aAddress;
	}

	std::string ConfiguredGraffityServerAddress()
	{
		std::string Address = g_Config.m_BcGraffityServerAddress;
		const size_t First = Address.find_first_not_of(" \t\r\n");
		if(First == std::string::npos)
			return DefaultGraffityServerAddress();

		const size_t Last = Address.find_last_not_of(" \t\r\n");
		return Address.substr(First, Last - First + 1);
	}

	float EaseInOutQuad(float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		if(t < 0.5f)
			return 2.0f * t * t;
		return 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
	}

	float PositiveMod(float x, float y)
	{
		return std::fmod(x + y, y);
	}

	bool ParseAddress(const char *pAddress, NETADDR &Out)
	{
		if(!pAddress || pAddress[0] == '\0')
			return false;

		Out = NETADDR_ZEROED;
		std::string Address = pAddress;
		if(Address.find(':') == std::string::npos)
			Address += ":" + std::to_string(DEFAULT_GRAFFITY_SERVER_PORT);
		if(net_host_lookup(Address.c_str(), &Out, NETTYPE_ALL) == 0)
			return true;
		return false;
	}

	float GraffitySoundVolume(float BaseVolume)
	{
		const float ConfigVolume = std::clamp(g_Config.m_BcGraffitySoundVolume / 100.0f, 0.0f, 2.0f);
		return std::clamp(BaseVolume * ConfigVolume, 0.0f, 2.0f);
	}

	void QueueInboundLine(std::mutex &Mutex, std::vector<std::string> &vLines, std::string Line)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		vLines.push_back(std::move(Line));
	}

	bool IsIpv6Loopback(const NETADDR &Addr)
	{
		for(int i = 0; i < 15; ++i)
		{
			if(Addr.ip[i] != 0)
				return false;
		}
		return Addr.ip[15] == 1;
	}
}

CGraffity::CGraffity()
{
	m_aDefinitions[0].m_pId = "best_client_graffity";
	m_aDefinitions[0].m_pPath = "BestClient/graffity/best_client_graffity.png";
	m_aDefinitions[1].m_pId = "ego_graffity";
	m_aDefinitions[1].m_pPath = "BestClient/graffity/ego_graffity.png";
	m_aDefinitions[2].m_pId = "cats_with_drool_graffity";
	m_aDefinitions[2].m_pPath = "BestClient/graffity/cats_with_drool_graffity.png";

	OnReset();
}

CGraffity::~CGraffity()
{
	StopNetwork();
}

void CGraffity::ConGraffity(IConsole::IResult *pResult, void *pUserData)
{
	CGraffity *pSelf = static_cast<CGraffity *>(pUserData);
	const bool Pressed = pResult->GetInteger(0) != 0;
	if(!Pressed)
	{
		if(g_Config.m_BcGraffityHoldWheel)
			pSelf->ReleaseWheel();
		return;
	}
	if(pSelf->GameClient()->m_Scoreboard.IsActive())
		return;
	if(!pSelf->GraffityEnabled())
	{
		pSelf->CloseWheel();
		pSelf->CancelPlacement();
		return;
	}
	if(g_Config.m_BcGraffityHoldWheel)
		pSelf->OpenWheel();
	else
		pSelf->ToggleWheel();
}

void CGraffity::OnConsoleInit()
{
	Console()->Register("+graffity", "", CFGFLAG_CLIENT, ConGraffity, this, "Toggle graffity wheel");
}

void CGraffity::OnInit()
{
	LoadTextures();
	LoadSounds();
}

void CGraffity::OnReset()
{
	m_AnimationTime = 0.0f;
	std::fill(std::begin(m_aHoverAnimation), std::end(m_aHoverAnimation), 0.0f);
	m_WheelActive = false;
	m_WasWheelActive = false;
	m_SelectorMouse = vec2(0.0f, 0.0f);
	m_SelectedIndex = -1;
	m_PlacementActive = false;
	m_PlacementIndex = -1;
	m_vPlacedGraffities.clear();
	m_aLastError[0] = '\0';
	m_LastPlacementErrorTick = 0;
	m_PendingPlacementSound = false;
}

void CGraffity::OnRelease()
{
	CloseWheel();
	CancelPlacement();
}

void CGraffity::OnShutdown()
{
	StopNetwork();
	UnloadSounds();
	UnloadTextures();
}

void CGraffity::OnStateChange(int NewState, int OldState)
{
	if(OldState == IClient::STATE_ONLINE && NewState != IClient::STATE_ONLINE)
	{
		StopNetwork();
		m_vPlacedGraffities.clear();
		CloseWheel();
		CancelPlacement();
	}
}

void CGraffity::OnUpdate()
{
	if(!GraffityEnabled())
	{
		CloseWheel();
		CancelPlacement();
		return;
	}

	if(!m_TexturesLoaded)
		LoadTextures();

	JoinNetworkThreadIfNeeded();
	EnsureNetworkConnection();
	DrainInbound();

	if((Client()->State() != IClient::STATE_ONLINE) || GameClient()->m_Snap.m_SpecInfo.m_Active || !GameClient()->m_Snap.m_pLocalCharacter)
	{
		CloseWheel();
		CancelPlacement();
	}
}

bool CGraffity::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_WheelActive)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	m_SelectorMouse += vec2(x, y);
	ClampSelectorMouseToCircle();
	return true;
}

bool CGraffity::OnInput(const IInput::CEvent &Event)
{
	if(!(Event.m_Flags & IInput::FLAG_PRESS))
		return false;

	if(Event.m_Key == KEY_ESCAPE)
	{
		if(m_WheelActive)
		{
			CloseWheel();
			return true;
		}
		if(m_PlacementActive)
		{
			CancelPlacement();
			return true;
		}
		return false;
	}

	if(Event.m_Key == KEY_MOUSE_2)
	{
		if(m_PlacementActive)
		{
			CancelPlacement();
			return true;
		}
		if(m_WheelActive)
		{
			CloseWheel();
			return true;
		}
		return false;
	}

	if(Event.m_Key != KEY_MOUSE_1)
		return false;

	if(m_WheelActive)
	{
		const CUIRect Screen = *Ui()->Screen();
		const vec2 CursorPos = Screen.Center() + m_SelectorMouse;
		for(const SOwnedPreviewRect &OwnedRect : BuildOwnedPreviewRects(Screen))
		{
			if(OwnedRect.m_Rect.Inside(CursorPos))
			{
				SendRemove(OwnedRect.m_Id);
				return true;
			}
		}

		if(m_SelectedIndex >= 0 && m_SelectedIndex < NUM_GRAFFITIES)
		{
			if(!GraffityEnabled())
				return true;
			BeginPlacement(m_SelectedIndex);
			return true;
		}
		return true;
	}

	if(m_PlacementActive)
	{
		if(!GraffityEnabled())
		{
			CancelPlacement();
			return true;
		}
		const SPlacementPreview Preview = BuildPlacementPreview();
		if(!Preview.m_Valid)
		{
			EchoPlacementError(Preview.m_Error);
			return true;
		}
		if(!m_NetworkConnected.load())
		{
			EchoPlacementError(EPlacementError::NO_SERVER);
			return true;
		}

		SendPlace(Preview);
		CancelPlacement();
		return true;
	}

	return false;
}

void CGraffity::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	if(m_WheelActive)
	{
		m_AnimationTime = minimum(m_AnimationTime + Client()->RenderFrameTime(), WHEEL_OPEN_TIME);
		m_WasWheelActive = true;
	}
	else if(m_WasWheelActive)
	{
		m_AnimationTime = maximum(m_AnimationTime - Client()->RenderFrameTime() * 3.0f, 0.0f);
		if(m_AnimationTime <= 0.0f)
			m_WasWheelActive = false;
	}

	const float HoverDelta = Client()->RenderFrameTime();
	for(int i = 0; i < NUM_GRAFFITIES; ++i)
	{
		if(i == m_SelectedIndex)
			m_aHoverAnimation[i] = minimum(m_aHoverAnimation[i] + HoverDelta, ITEM_HOVER_TIME);
		else
			m_aHoverAnimation[i] = maximum(m_aHoverAnimation[i] - HoverDelta, 0.0f);
	}

	ClampSelectorMouseToCircle();

	if(m_WheelActive)
		m_SelectedIndex = SelectedWheelIndex();
	else
		m_SelectedIndex = -1;

	float PrevScreenX0, PrevScreenY0, PrevScreenX1, PrevScreenY1;
	Graphics()->GetScreen(&PrevScreenX0, &PrevScreenY0, &PrevScreenX1, &PrevScreenY1);
	MapGameScreen();
	if(GraffityEnabled())
		RenderWorldGraffities(true);
	if(m_PlacementActive && GraffityEnabled())
		RenderPlacementPreview(BuildPlacementPreview(), false);
	Graphics()->MapScreen(PrevScreenX0, PrevScreenY0, PrevScreenX1, PrevScreenY1);
}

void CGraffity::RenderOverlayWorld()
{
	if(!GraffityEnabled())
		return;

	float PrevScreenX0, PrevScreenY0, PrevScreenX1, PrevScreenY1;
	Graphics()->GetScreen(&PrevScreenX0, &PrevScreenY0, &PrevScreenX1, &PrevScreenY1);
	MapGameScreen();
	RenderWorldGraffities(false);
	if(m_PlacementActive)
		RenderPlacementPreview(BuildPlacementPreview(), true);
	Graphics()->MapScreen(PrevScreenX0, PrevScreenY0, PrevScreenX1, PrevScreenY1);
}

void CGraffity::RenderOverlayUi()
{
	if(!(m_WheelActive || m_WasWheelActive))
		return;

	float PrevScreenX0, PrevScreenY0, PrevScreenX1, PrevScreenY1;
	Graphics()->GetScreen(&PrevScreenX0, &PrevScreenY0, &PrevScreenX1, &PrevScreenY1);
	RenderWheelUi();
	Graphics()->MapScreen(PrevScreenX0, PrevScreenY0, PrevScreenX1, PrevScreenY1);
}

void CGraffity::LoadTextures()
{
	if(m_TexturesLoaded)
		return;

	m_TexturesLoaded = true;
	for(SGraffityDef &Definition : m_aDefinitions)
	{
		Definition.m_Texture = Graphics()->LoadTexture(Definition.m_pPath, IStorage::TYPE_ALL);
		if(!Definition.m_Texture.IsValid() && !m_TextureErrorShown)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "[[red]] Graffity texture not found: %s", Definition.m_pPath);
			GameClient()->Echo(aBuf);
			m_TextureErrorShown = true;
		}
	}
}

void CGraffity::UnloadTextures()
{
	if(!m_TexturesLoaded)
		return;

	for(SGraffityDef &Definition : m_aDefinitions)
	{
		if(Definition.m_Texture.IsValid() && !Definition.m_Texture.IsNullTexture())
			Graphics()->UnloadTexture(&Definition.m_Texture);
	}
	m_TexturesLoaded = false;
}

void CGraffity::LoadSounds()
{
	const char *apRelativePaths[NUM_SPRAY_SOUNDS] = {
		"BestClient/graffity/spray-open.wv",
		"BestClient/graffity/spray-select.wv",
		"BestClient/graffity/spray-apply.wv",
	};
	const char *apDataPaths[NUM_SPRAY_SOUNDS] = {
		"data/BestClient/graffity/spray-open.wv",
		"data/BestClient/graffity/spray-select.wv",
		"data/BestClient/graffity/spray-apply.wv",
	};

	for(int i = 0; i < NUM_SPRAY_SOUNDS; ++i)
	{
		if(m_aSpraySoundIds[i] != -1)
			continue;

		auto TryLoad = [this, i](const char *pPath, int StorageType) {
			if(m_aSpraySoundIds[i] != -1)
				return;
			if(!Storage()->FileExists(pPath, StorageType))
				return;
			m_aSpraySoundIds[i] = Sound()->LoadWV(pPath, StorageType);
		};

		char aBinaryDataPath[IO_MAX_PATH_LENGTH];
		char aParentDataPath[IO_MAX_PATH_LENGTH];
		char aParentRelativeDataPath[IO_MAX_PATH_LENGTH];
		str_format(aParentRelativeDataPath, sizeof(aParentRelativeDataPath), "../%s", apDataPaths[i]);
		Storage()->GetBinaryPathAbsolute(apDataPaths[i], aBinaryDataPath, sizeof(aBinaryDataPath));
		Storage()->GetBinaryPathAbsolute(aParentRelativeDataPath, aParentDataPath, sizeof(aParentDataPath));

		TryLoad(apRelativePaths[i], IStorage::TYPE_ALL);
		TryLoad(apDataPaths[i], IStorage::TYPE_ALL);
		TryLoad(aBinaryDataPath, IStorage::TYPE_ABSOLUTE);
		TryLoad(aParentDataPath, IStorage::TYPE_ABSOLUTE);
	}
}

void CGraffity::UnloadSounds()
{
	for(int &SoundId : m_aSpraySoundIds)
	{
		if(SoundId != -1)
		{
			Sound()->UnloadSample(SoundId);
			SoundId = -1;
		}
	}
}

void CGraffity::PlaySprayOpenSound()
{
	if(GameClient()->m_SuppressEvents || !g_Config.m_SndEnable || !Sound()->IsSoundEnabled())
		return;
	LoadSounds();
	if(m_aSpraySoundIds[SPRAY_SOUND_OPEN] == -1)
	{
		if(!m_SpraySoundErrorShown)
		{
			m_SpraySoundErrorShown = true;
			GameClient()->Echo("[[red]] Graffity spray sounds not found. Put files in data/BestClient/graffity/");
		}
		return;
	}
	const float Volume = GraffitySoundVolume(0.95f);
	if(Volume <= 0.0f)
		return;
	Sound()->Play(CSounds::CHN_GUI, m_aSpraySoundIds[SPRAY_SOUND_OPEN], 0, Volume);
}

void CGraffity::PlaySpraySelectSound()
{
	if(GameClient()->m_SuppressEvents || !g_Config.m_SndEnable || !Sound()->IsSoundEnabled())
		return;
	LoadSounds();
	if(m_aSpraySoundIds[SPRAY_SOUND_SELECT] == -1)
		return;
	const float Volume = GraffitySoundVolume(0.9f);
	if(Volume <= 0.0f)
		return;
	Sound()->Play(CSounds::CHN_GUI, m_aSpraySoundIds[SPRAY_SOUND_SELECT], 0, Volume);
}

void CGraffity::PlaySprayApplySound()
{
	if(GameClient()->m_SuppressEvents || !g_Config.m_SndEnable || !Sound()->IsSoundEnabled())
		return;
	LoadSounds();
	if(m_aSpraySoundIds[SPRAY_SOUND_APPLY] == -1)
		return;
	const float Volume = GraffitySoundVolume(0.95f);
	if(Volume <= 0.0f)
		return;
	Sound()->Play(CSounds::CHN_GUI, m_aSpraySoundIds[SPRAY_SOUND_APPLY], 0, Volume);
}

void CGraffity::OpenWheel()
{
	if(!GraffityEnabled())
		return;
	if(Client()->State() != IClient::STATE_ONLINE || GameClient()->m_Snap.m_SpecInfo.m_Active || !GameClient()->m_Snap.m_pLocalCharacter)
		return;
	if(IsLocalhostGameServer())
	{
		GameClient()->Echo("Graffiti: disabled on localhost servers");
		return;
	}
	if(m_PlacementActive)
		CancelPlacement();
	if(m_WheelActive)
		return;

	GameClient()->m_Emoticon.OnRelease();
	GameClient()->m_BindWheel.OnRelease();
	m_WheelActive = true;
	m_SelectorMouse = vec2(0.0f, 0.0f);
	PlaySprayOpenSound();
}

void CGraffity::ToggleWheel()
{
	if(!GraffityEnabled())
	{
		CloseWheel();
		CancelPlacement();
		return;
	}
	if(m_PlacementActive)
	{
		CancelPlacement();
		return;
	}

	if(m_WheelActive)
	{
		CloseWheel();
		return;
	}

	OpenWheel();
}

void CGraffity::CloseWheel()
{
	m_WheelActive = false;
	m_SelectedIndex = -1;
}

void CGraffity::ReleaseWheel()
{
	if(!m_WheelActive)
		return;

	const int SelectedIndex = SelectedWheelIndex();
	if(SelectedIndex >= 0 && SelectedIndex < NUM_GRAFFITIES && GraffityEnabled())
	{
		BeginPlacement(SelectedIndex);
		return;
	}

	CloseWheel();
}

int CGraffity::SelectedWheelIndex() const
{
	const CUIRect Screen = *Ui()->Screen();
	const vec2 CursorPos = Screen.Center() + m_SelectorMouse;
	for(const SOwnedPreviewRect &OwnedRect : BuildOwnedPreviewRects(Screen))
	{
		if(OwnedRect.m_Rect.Inside(CursorPos))
			return -1;
	}

	if(length(m_SelectorMouse) <= WHEEL_SELECT_RADIUS)
		return -1;

	const float SelectorAngle = angle(m_SelectorMouse);
	return (int)PositiveMod(std::round(SelectorAngle / (2.0f * pi) * (float)NUM_GRAFFITIES), (float)NUM_GRAFFITIES);
}

void CGraffity::ClampSelectorMouseToCircle()
{
	const float Distance = length(m_SelectorMouse);
	if(Distance > WHEEL_MAX_CURSOR_RADIUS && Distance > 0.0f)
		m_SelectorMouse *= WHEEL_MAX_CURSOR_RADIUS / Distance;
}

void CGraffity::CancelPlacement()
{
	m_PlacementActive = false;
	m_PlacementIndex = -1;
}

void CGraffity::BeginPlacement(int Index)
{
	CloseWheel();
	m_PlacementActive = true;
	m_PlacementIndex = Index;
	PlaySpraySelectSound();
}

void CGraffity::QueueOutbound(std::string Line)
{
	std::lock_guard<std::mutex> Lock(m_NetMutex);
	if(m_vOutboundLines.size() >= 256)
		return;
	m_vOutboundLines.push_back(std::move(Line));
	m_NetCv.notify_all();
}

void CGraffity::DrainInbound()
{
	std::vector<std::string> vLines;
	{
		std::lock_guard<std::mutex> Lock(m_NetMutex);
		vLines.swap(m_vInboundLines);
	}

	for(const std::string &Line : vLines)
		HandleIncomingLine(Line);
}

void CGraffity::EnsureNetworkConnection()
{
	if(!GraffityEnabled())
	{
		StopNetwork();
		return;
	}
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		StopNetwork();
		return;
	}
	if(GameClient()->m_Snap.m_LocalClientId < 0)
		return;
	if(IsLocalhostGameServer())
	{
		StopNetwork();
		m_vPlacedGraffities.clear();
		str_copy(m_aLastError, "Graffity disabled on localhost servers", sizeof(m_aLastError));
		return;
	}

	char aServerAddr[NETADDR_MAXSTRSIZE];
	net_addr_str(&Client()->ServerAddress(), aServerAddr, sizeof(aServerAddr), true);

	if(m_ActiveGameServerAddress != aServerAddr)
	{
		StopNetwork();
		m_ActiveGameServerAddress = aServerAddr;
	}
	if(m_ActiveGraffityServerAddress != ConfiguredGraffityServerAddress())
	{
		StopNetwork();
		m_ActiveGraffityServerAddress = ConfiguredGraffityServerAddress();
	}

	const int64_t ReconnectDelayTicks = time_freq() * 2;
	if(m_ActiveGameServerAddress.empty() || m_NetworkRunning.load() || time_get() - m_LastConnectAttempt < ReconnectDelayTicks)
		return;

	char aOwnerId[32];
	str_format(aOwnerId, sizeof(aOwnerId), "%d", GameClient()->m_Snap.m_LocalClientId);
	StartNetwork(m_ActiveGameServerAddress.c_str(), aOwnerId);
}

void CGraffity::StartNetwork(const char *pGameServerAddress, const char *pOwnerId)
{
	if(m_NetworkThread.joinable())
		m_NetworkThread.join();

	m_StopThread = false;
	m_NetworkRunning = true;
	m_NetworkConnected = false;
	m_LastConnectAttempt = time_get();
	m_NetworkThread = std::thread(&CGraffity::NetworkMain, this, std::string(pGameServerAddress), std::string(pOwnerId));
}

void CGraffity::StopNetwork()
{
	m_StopThread = true;
	m_NetCv.notify_all();
	// Close the socket under lock so the network thread's blocking
	// net_tcp_connect() is interrupted immediately, preventing the main
	// thread from freezing during map changes.
	{
		std::lock_guard<std::mutex> Lock(m_SocketMutex);
		if(m_NetworkSocket != nullptr)
		{
			net_tcp_close(m_NetworkSocket);
			m_NetworkSocket = nullptr;
		}
	}
	if(m_NetworkThread.joinable())
		m_NetworkThread.join();
	m_NetworkRunning = false;
	m_NetworkConnected = false;
	{
		std::lock_guard<std::mutex> Lock(m_NetMutex);
		m_vOutboundLines.clear();
		m_vInboundLines.clear();
	}
}

void CGraffity::JoinNetworkThreadIfNeeded()
{
	if(!m_NetworkRunning.load() && m_NetworkThread.joinable())
		m_NetworkThread.join();
}

void CGraffity::NetworkMain(std::string GameServerAddress, std::string OwnerId)
{
	// Early-out before starting a potentially slow DNS lookup.
	if(m_StopThread.load())
	{
		m_NetworkRunning = false;
		return;
	}

	NETADDR ServerAddr;
	if(!ParseAddress(ConfiguredGraffityServerAddress().c_str(), ServerAddr))
	{
		str_copy(m_aLastError, "Graffity server address is invalid", sizeof(m_aLastError));
		m_NetworkRunning = false;
		return;
	}

	NETADDR BindAddr = NETADDR_ZEROED;
	BindAddr.type = ServerAddr.type;
	NETSOCKET Socket = net_tcp_create(BindAddr);
	if(!Socket)
	{
		str_copy(m_aLastError, "Could not create graffity socket", sizeof(m_aLastError));
		m_NetworkRunning = false;
		return;
	}

	// Register the socket so StopNetwork() can close it to interrupt
	// the blocking net_tcp_connect() below without joining.
	{
		std::lock_guard<std::mutex> Lock(m_SocketMutex);
		if(m_StopThread.load())
		{
			// StopNetwork was called between DNS and socket registration.
			net_tcp_close(Socket);
			m_NetworkRunning = false;
			return;
		}
		m_NetworkSocket = Socket;
	}

	if(net_tcp_connect(Socket, &ServerAddr) != 0)
	{
		// Connection failed — may be because StopNetwork closed our socket.
		{
			std::lock_guard<std::mutex> Lock(m_SocketMutex);
			if(m_NetworkSocket == Socket)
			{
				m_NetworkSocket = nullptr;
				net_tcp_close(Socket);
			}
			// Otherwise StopNetwork already closed it; don't double-close.
		}
		if(!m_StopThread.load())
			str_copy(m_aLastError, "Could not connect to graffity server", sizeof(m_aLastError));
		m_NetworkRunning = false;
		return;
	}

	net_set_non_blocking(Socket);
	m_NetworkConnected = true;
	m_aLastError[0] = '\0';

	std::string PendingRecv;
	PendingRecv.reserve(4096);
	std::array<char, 2048> aBuffer{};
	bool HelloSent = false;
	int64_t NextHelloTick = 0;

	while(!m_StopThread.load())
	{
		std::vector<std::string> vOutbound;
		{
			std::unique_lock<std::mutex> Lock(m_NetMutex);
			if(m_vOutboundLines.empty())
				m_NetCv.wait_for(Lock, std::chrono::milliseconds(25));
			vOutbound.swap(m_vOutboundLines);
		}

		const int64_t Now = time_get();
		if(!HelloSent || Now >= NextHelloTick)
		{
			vOutbound.insert(vOutbound.begin(), BuildGraffityHelloMessage(GameServerAddress, OwnerId, ::time(nullptr)));
			HelloSent = true;
			NextHelloTick = Now + time_freq() * GRAFFITY_HELLO_RESYNC_INTERVAL;
		}

		for(const std::string &Line : vOutbound)
		{
			std::string Packet = Line;
			Packet.push_back('\n');
			int Sent = 0;
			while(Sent < (int)Packet.size())
			{
				const int Result = net_tcp_send(Socket, Packet.data() + Sent, (int)Packet.size() - Sent);
				if(Result <= 0)
				{
					str_copy(m_aLastError, "Graffity server send failed", sizeof(m_aLastError));
					m_StopThread = true;
					break;
				}
				Sent += Result;
			}
			if(m_StopThread.load())
				break;
		}

		int Received = net_tcp_recv(Socket, aBuffer.data(), (int)aBuffer.size());
		if(Received < 0)
		{
			if(net_would_block())
				continue;

			str_copy(m_aLastError, "Graffity server disconnected", sizeof(m_aLastError));
			break;
		}
		if(Received == 0)
		{
			str_copy(m_aLastError, "Graffity server disconnected", sizeof(m_aLastError));
			break;
		}
		if(Received > 0)
		{
			PendingRecv.append(aBuffer.data(), Received);
			size_t NewlinePos = PendingRecv.find('\n');
			while(NewlinePos != std::string::npos)
			{
				std::string Line = PendingRecv.substr(0, NewlinePos);
				if(!Line.empty())
					QueueInboundLine(m_NetMutex, m_vInboundLines, std::move(Line));
				PendingRecv.erase(0, NewlinePos + 1);
				NewlinePos = PendingRecv.find('\n');
			}
		}
	}

	{
		std::lock_guard<std::mutex> Lock(m_SocketMutex);
		if(m_NetworkSocket == Socket)
		{
			m_NetworkSocket = nullptr;
			net_tcp_close(Socket);
		}
		// If StopNetwork already closed it, skip to avoid double-close.
	}
	m_NetworkConnected = false;
	m_NetworkRunning = false;
}

void CGraffity::HandleIncomingLine(const std::string &Line)
{
	json_settings Settings = {};
	char aError[256];
	json_value *pJson = json_parse_ex(&Settings, (json_char *)Line.data(), Line.size(), aError);
	if(!pJson || pJson->type != json_object)
	{
		if(pJson)
			json_value_free(pJson);
		return;
	}

	const char *pType = json_string_get(json_object_get(pJson, "type"));
	if(pType && str_comp(pType, "snapshot") == 0)
	{
		ApplySnapshot(pJson);
	}
	else if(pType && str_comp(pType, "error") == 0)
	{
		m_PendingPlacementSound = false;
		if(const char *pMessage = json_string_get(json_object_get(pJson, "message")); pMessage && pMessage[0] != '\0')
			GameClient()->Echo(pMessage);
	}

	json_value_free(pJson);
}

void CGraffity::ApplySnapshot(const json_value *pJson)
{
	const json_value *pGraffities = json_object_get(pJson, "graffities");
	if(!pGraffities || pGraffities->type != json_array)
	{
		m_vPlacedGraffities.clear();
		return;
	}

	std::vector<SPlacedGraffity> vNewGraffities;
	vNewGraffities.reserve(json_array_length(pGraffities));
	const int64_t Now = time_get();
	bool PlacementConfirmed = false;

	for(int i = 0; i < json_array_length(pGraffities); ++i)
	{
		const json_value *pEntry = json_array_get(pGraffities, i);
		if(!pEntry || pEntry->type != json_object)
			continue;

		const char *pId = json_string_get(json_object_get(pEntry, "id"));
		const char *pGraffityId = json_string_get(json_object_get(pEntry, "graffity_id"));
		const json_value *pX = json_object_get(pEntry, "x");
		const json_value *pY = json_object_get(pEntry, "y");
		const json_value *pSize = json_object_get(pEntry, "size");
		const json_value *pAir = json_object_get(pEntry, "air");
		const json_value *pOwned = json_object_get(pEntry, "owned");
		if(!pId || !pGraffityId || !pX || !pY || !pAir || !pOwned)
			continue;

		SPlacedGraffity Graffity;
		Graffity.m_Id = pId;
		Graffity.m_GraffityId = pGraffityId;
		Graffity.m_Pos = vec2((float)json_int_get(pX), (float)json_int_get(pY));
		Graffity.m_Size = pSize ? std::clamp((int)json_int_get(pSize), MIN_GRAFFITY_SIZE_STEP, MAX_GRAFFITY_SIZE_STEP) : 1;
		Graffity.m_Air = json_boolean_get(pAir) != 0;
		Graffity.m_Owned = json_boolean_get(pOwned) != 0;
		Graffity.m_AppearTick = Now;
		if(auto It = std::find_if(m_vPlacedGraffities.begin(), m_vPlacedGraffities.end(), [&](const SPlacedGraffity &Existing) { return Existing.m_Id == Graffity.m_Id; }); It != m_vPlacedGraffities.end())
		{
			Graffity.m_AppearTick = It->m_AppearTick;
		}
		else if(Graffity.m_Owned)
		{
			PlacementConfirmed = true;
		}
		vNewGraffities.push_back(std::move(Graffity));
	}

	m_vPlacedGraffities = std::move(vNewGraffities);
	if(PlacementConfirmed && m_PendingPlacementSound)
	{
		PlaySprayApplySound();
		m_PendingPlacementSound = false;
	}
}

void CGraffity::SendRemove(const std::string &Id)
{
	QueueOutbound("{\"type\":\"remove\",\"id\":\"" + Id + "\"}");
}

void CGraffity::SendPlace(const SPlacementPreview &Preview)
{
	if(m_PlacementIndex < 0 || m_PlacementIndex >= NUM_GRAFFITIES)
		return;

	char aLine[512];
	str_format(aLine, sizeof(aLine),
		"{\"type\":\"place\",\"graffity_id\":\"%s\",\"x\":%d,\"y\":%d,\"air\":%s,\"size\":%d}",
		m_aDefinitions[m_PlacementIndex].m_pId,
		round_to_int(Preview.m_Pos.x),
		round_to_int(Preview.m_Pos.y),
		JsonBool(Preview.m_Air),
		Preview.m_Size);
	m_PendingPlacementSound = true;
	QueueOutbound(aLine);
}

CGraffity::SPlacementPreview CGraffity::BuildPlacementPreview() const
{
	SPlacementPreview Preview;
	if(!m_PlacementActive || m_PlacementIndex < 0 || m_PlacementIndex >= NUM_GRAFFITIES || Client()->State() != IClient::STATE_ONLINE || !GameClient()->m_Snap.m_pLocalCharacter)
	{
		Preview.m_Error = EPlacementError::OFFLINE;
		return Preview;
	}

	const vec2 Target = GameClient()->m_Controls.m_aTargetPos[g_Config.m_ClDummy];
	Preview.m_Size = GraffitySizeStep();
	const int FootprintTiles = Preview.m_Size;
	const int BaseTileX = (int)std::floor(Target.x / 32.0f);
	const int BaseTileY = (int)std::floor(Target.y / 32.0f);
	Preview.m_TileX = BaseTileX;
	Preview.m_TileY = BaseTileY;
	Preview.m_Pos = vec2(BaseTileX * 32.0f + Preview.m_Size * 16.0f, BaseTileY * 32.0f + Preview.m_Size * 16.0f);
	bool SawInBounds = false;
	bool SawMixedSurface = false;
	bool SawOverlap = false;
	float BestDistSq = 0.0f;
	float FallbackDistSq = 0.0f;

	for(int OffsetY = -(FootprintTiles - 1); OffsetY <= 0; ++OffsetY)
	{
		for(int OffsetX = -(FootprintTiles - 1); OffsetX <= 0; ++OffsetX)
		{
			const int TileX = BaseTileX + OffsetX;
			const int TileY = BaseTileY + OffsetY;
			if(TileX < 0 || TileY < 0 || TileX + FootprintTiles - 1 >= Collision()->GetWidth() || TileY + FootprintTiles - 1 >= Collision()->GetHeight())
				continue;

			SawInBounds = true;
			const vec2 Pos(TileX * 32.0f + Preview.m_Size * 16.0f, TileY * 32.0f + Preview.m_Size * 16.0f);
			const vec2 Delta = Pos - Target;
			const float DistSq = Delta.x * Delta.x + Delta.y * Delta.y;
			if(!Preview.m_Valid && ((!SawMixedSurface && !SawOverlap) || DistSq < FallbackDistSq))
			{
				FallbackDistSq = DistSq;
				Preview.m_TileX = TileX;
				Preview.m_TileY = TileY;
				Preview.m_Pos = Pos;
			}
			int SurfaceCells = 0;
			for(int y = 0; y < FootprintTiles; ++y)
			{
				for(int x = 0; x < FootprintTiles; ++x)
				{
					if(CellHasSurface(TileX + x, TileY + y))
						SurfaceCells++;
				}
			}

			const int TotalCells = FootprintTiles * FootprintTiles;
			if(SurfaceCells != 0 && SurfaceCells != TotalCells)
			{
				SawMixedSurface = true;
				continue;
			}

			if(IntersectsExisting(Pos, Preview.m_Size))
			{
				SawOverlap = true;
				continue;
			}

			if(!Preview.m_Valid || DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Preview.m_Valid = true;
				Preview.m_TileX = TileX;
				Preview.m_TileY = TileY;
				Preview.m_Pos = Pos;
				Preview.m_Air = SurfaceCells == 0;
				Preview.m_Error = EPlacementError::NONE;
			}
		}
	}

	if(Preview.m_Valid)
		return Preview;

	if(SawOverlap)
		Preview.m_Error = EPlacementError::OVERLAP;
	else if(SawMixedSurface)
		Preview.m_Error = EPlacementError::MIXED_SURFACE;
	else if(!SawInBounds)
		Preview.m_Error = EPlacementError::OUT_OF_BOUNDS;
	return Preview;
}

std::vector<CGraffity::SOwnedPreviewRect> CGraffity::BuildOwnedPreviewRects(const CUIRect &Screen) const
{
	std::vector<SOwnedPreviewRect> vRects;
	for(const SPlacedGraffity &Graffity : m_vPlacedGraffities)
	{
		if(!Graffity.m_Owned)
			continue;
		vRects.push_back({{}, Graffity.m_Id, Graffity.m_GraffityId});
	}

	if(vRects.empty())
		return vRects;

	const float Size = 48.0f;
	const float Spacing = 12.0f;
	const float TotalWidth = vRects.size() * Size + (vRects.size() - 1) * Spacing;
	float X = Screen.Center().x - TotalWidth / 2.0f;
	const float Y = Screen.Center().y - WHEEL_CIRCLE_RADIUS - 58.0f;

	for(SOwnedPreviewRect &Rect : vRects)
	{
		Rect.m_Rect.x = X;
		Rect.m_Rect.y = Y;
		Rect.m_Rect.w = Size;
		Rect.m_Rect.h = Size;
		X += Size + Spacing;
	}

	return vRects;
}

const CGraffity::SGraffityDef *CGraffity::FindDefinitionById(const char *pId) const
{
	if(!pId)
		return nullptr;
	for(const SGraffityDef &Definition : m_aDefinitions)
	{
		if(str_comp(Definition.m_pId, pId) == 0)
			return &Definition;
	}
	return nullptr;
}

const CGraffity::SGraffityDef *CGraffity::FindDefinitionById(const std::string &Id) const
{
	return FindDefinitionById(Id.c_str());
}

bool CGraffity::GraffityEnabled() const
{
	return g_Config.m_BcGraffityEnabled != 0;
}

int CGraffity::GraffitySizeStep() const
{
	return std::clamp(g_Config.m_BcGraffitySize, MIN_GRAFFITY_SIZE_STEP, MAX_GRAFFITY_SIZE_STEP);
}

bool CGraffity::IsLocalhostGameServer() const
{
	const NETADDR &Addr = Client()->ServerAddress();
	if(Addr.type & NETTYPE_IPV4)
		return Addr.ip[0] == 127;
	if(Addr.type & NETTYPE_IPV6)
		return IsIpv6Loopback(Addr);
	return false;
}

bool CGraffity::CellHasSurface(int TileX, int TileY) const
{
	if(TileX < 0 || TileY < 0 || TileX >= Collision()->GetWidth() || TileY >= Collision()->GetHeight())
		return false;
	const int WorldX = TileX * 32 + 16;
	const int WorldY = TileY * 32 + 16;
	return Collision()->GetTile(WorldX, WorldY) != 0 || Collision()->GetFrontTile(WorldX, WorldY) != 0 || Collision()->IsSolid(WorldX, WorldY);
}

bool CGraffity::IntersectsExisting(const vec2 &Pos, int SizeStep, const char *pIgnoreId) const
{
	const float HalfSize = GraffityWorldSize(SizeStep) * 0.5f;
	for(const SPlacedGraffity &Graffity : m_vPlacedGraffities)
	{
		if(pIgnoreId && str_comp(Graffity.m_Id.c_str(), pIgnoreId) == 0)
			continue;
		const float OtherHalfSize = GraffityWorldSize(Graffity.m_Size) * 0.5f;
		if(absolute(Graffity.m_Pos.x - Pos.x) <= HalfSize + OtherHalfSize &&
			absolute(Graffity.m_Pos.y - Pos.y) <= HalfSize + OtherHalfSize)
			return true;
	}
	return false;
}

void CGraffity::MapGameScreen() const
{
	const vec2 Center = GameClient()->m_Camera.m_Center;
	if(const CMapItemGroup *pGameGroup = Layers()->GameGroup())
	{
		const int ParallaxZoom = std::clamp(maximum(pGameGroup->m_ParallaxX, pGameGroup->m_ParallaxY), 0, 100);
		float aPoints[4];
		Graphics()->MapScreenToWorld(
			Center.x, Center.y,
			pGameGroup->m_ParallaxX, pGameGroup->m_ParallaxY, (float)ParallaxZoom,
			pGameGroup->m_OffsetX, pGameGroup->m_OffsetY,
			Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, aPoints);
		Graphics()->MapScreen(aPoints[0], aPoints[1], aPoints[2], aPoints[3]);
	}
	else
	{
		float Width = 0.0f;
		float Height = 0.0f;
		Graphics()->CalcScreenParams(Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom, &Width, &Height);
		Graphics()->MapScreen(Center.x - Width * 0.5f, Center.y - Height * 0.5f, Center.x + Width * 0.5f, Center.y + Height * 0.5f);
	}
}

float CGraffity::GraffityWorldSize(int SizeStep) const
{
	return GRAFFITY_WORLD_SIZE * std::clamp(SizeStep, MIN_GRAFFITY_SIZE_STEP, MAX_GRAFFITY_SIZE_STEP);
}

float CGraffity::PlacementSelectionZoomScale() const
{
	return std::clamp(1.0f / maximum(GameClient()->m_Camera.m_Zoom, 0.01f), 0.85f, 1.55f);
}

void CGraffity::RenderWorldGraffities(bool AirOnly) const
{
	const int64_t Now = time_get();
	const float FadeDuration = GRAFFITY_FADE_IN_TIME * (float)time_freq();

	for(const SPlacedGraffity &Graffity : m_vPlacedGraffities)
	{
		if(Graffity.m_Air != AirOnly)
			continue;
		const SGraffityDef *pDefinition = FindDefinitionById(Graffity.m_GraffityId);
		if(!pDefinition)
			continue;
		const float FadeIn = FadeDuration <= 0.0f ? 1.0f : std::clamp((Now - Graffity.m_AppearTick) / FadeDuration, 0.0f, 1.0f);
		RenderGraffityQuad(*pDefinition, Graffity.m_Pos, GraffityWorldSize(Graffity.m_Size), ColorRGBA(1.0f, 1.0f, 1.0f, (Graffity.m_Air ? 0.85f : 1.0f) * FadeIn));
	}
}

void CGraffity::RenderPlacementPreview(const SPlacementPreview &Preview, bool OverlayPass) const
{
	if(m_PlacementIndex < 0 || m_PlacementIndex >= NUM_GRAFFITIES)
		return;
	const bool ShouldRenderInOverlay = !Preview.m_Valid || !Preview.m_Air;
	if(ShouldRenderInOverlay != OverlayPass)
		return;
	const SGraffityDef &Definition = m_aDefinitions[m_PlacementIndex];
	const float WorldSize = GraffityWorldSize(Preview.m_Size);
	const ColorRGBA Tint = Preview.m_Valid ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.55f) : ColorRGBA(1.0f, 0.35f, 0.35f, 0.4f);
	if(!Preview.m_Valid)
	{
		const float SelectionSize = WorldSize * PlacementSelectionZoomScale();
		const float SelectionRound = 6.0f * PlacementSelectionZoomScale();
		Graphics()->TextureClear();
		Graphics()->DrawRect(Preview.m_Pos.x - SelectionSize * 0.5f, Preview.m_Pos.y - SelectionSize * 0.5f, SelectionSize, SelectionSize, ColorRGBA(1.0f, 0.25f, 0.25f, 0.16f), IGraphics::CORNER_ALL, SelectionRound);
	}
	RenderGraffityQuad(Definition, Preview.m_Pos, WorldSize, Tint);
}

void CGraffity::RenderWheelUi()
{
	const CUIRect Screen = *Ui()->Screen();
	const vec2 ScreenCenter = Screen.Center();
	const float OpenPhase = WHEEL_OPEN_TIME <= 0.0f ? 1.0f : EaseInOutQuad(m_AnimationTime / WHEEL_OPEN_TIME);

	Ui()->MapScreen();
	Graphics()->BlendNormal();

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.34f * OpenPhase);
	Graphics()->DrawCircle(ScreenCenter.x, ScreenCenter.y, WHEEL_CIRCLE_RADIUS * OpenPhase, 64);
	Graphics()->QuadsEnd();

	for(int i = 0; i < NUM_GRAFFITIES; ++i)
	{
		const float Angle = 2.0f * pi * i / (float)NUM_GRAFFITIES;
		const vec2 Nudge = direction(Angle) * WHEEL_ITEM_RADIUS * OpenPhase;
		const float HoverPhase = ITEM_HOVER_TIME <= 0.0f ? 0.0f : EaseInOutQuad(m_aHoverAnimation[i] / ITEM_HOVER_TIME);
		const float Size = (54.0f + HoverPhase * 24.0f) * OpenPhase;
		RenderGraffityQuad(m_aDefinitions[i], ScreenCenter + Nudge, Size, ColorRGBA(1.0f, 1.0f, 1.0f, OpenPhase));
	}

	for(const SOwnedPreviewRect &OwnedRect : BuildOwnedPreviewRects(Screen))
	{
		const SGraffityDef *pDefinition = FindDefinitionById(OwnedRect.m_GraffityId);
		if(!pDefinition)
			continue;

		CUIRect BgRect = OwnedRect.m_Rect;
		BgRect.Margin(-5.0f, &BgRect);
		BgRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.22f * OpenPhase), IGraphics::CORNER_ALL, 6.0f);

		RenderGraffityQuad(*pDefinition, OwnedRect.m_Rect.Center(), OwnedRect.m_Rect.w, ColorRGBA(1.0f, 1.0f, 1.0f, OpenPhase));
	}

	RenderTools()->RenderCursor(ScreenCenter + m_SelectorMouse, 24.0f, OpenPhase);
}

void CGraffity::RenderGraffityQuad(const SGraffityDef &Definition, vec2 Pos, float Size, ColorRGBA Color) const
{
	if(!Definition.m_Texture.IsValid() || Definition.m_Texture.IsNullTexture())
		return;

	Graphics()->TextureSet(Definition.m_Texture);
	Graphics()->QuadsSetSubset(0, 0, 1, 1);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(Color);
	IGraphics::CQuadItem Quad(Pos.x, Pos.y, Size, Size);
	Graphics()->QuadsDraw(&Quad, 1);
	Graphics()->QuadsEnd();
}

void CGraffity::EchoPlacementError(EPlacementError Error)
{
	const int64_t Now = time_get();
	if(Now - m_LastPlacementErrorTick < time_freq() / 3)
		return;
	m_LastPlacementErrorTick = Now;

	switch(Error)
	{
	case EPlacementError::OUT_OF_BOUNDS:
		GameClient()->Echo("Graffiti: out of map bounds");
		break;
	case EPlacementError::MIXED_SURFACE:
		GameClient()->Echo("Graffiti: place only on full 2x2 air or full 2x2 wall");
		break;
	case EPlacementError::OVERLAP:
		GameClient()->Echo("Graffiti: too close to another graffiti");
		break;
	case EPlacementError::NO_SERVER:
		if(m_aLastError[0] != '\0')
		{
			char aBuf[320];
			str_format(aBuf, sizeof(aBuf), "Graffiti: server unavailable (%s)", m_aLastError);
			GameClient()->Echo(aBuf);
		}
		else
		{
			GameClient()->Echo("Graffiti: server unavailable");
		}
		break;
	default:
		break;
	}
}
