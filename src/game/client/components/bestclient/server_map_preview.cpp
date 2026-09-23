/* Copyright © 2026 BestProject Team */
#include "server_map_preview.h"

#include <base/color.h>
#include <base/io.h>
#include <base/math.h>
#include <base/secure.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>

#include <game/client/components/envelope_state.h>
#include <game/client/components/mapimages.h>
#include <game/client/gameclient.h>
#include <game/map/map_renderer.h>
#include <game/layers.h>
#include <game/localization.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
constexpr float ZOOM_MIN = 0.5f;
constexpr float ZOOM_MAX = 2.0f;
constexpr float ZOOM_STEP = 1.15f;
constexpr float CORNER_RADIUS = 12.0f;
constexpr float EXPAND_SECONDS = 0.35f;
constexpr float HEIGHT_MIN = 72.0f;
constexpr float HEIGHT_MAX = 420.0f;
constexpr float RESIZE_GRIP = 16.0f;
constexpr int MAX_CACHED_MAPS = 4;
constexpr int64_t MAX_MAP_BYTES = 64ll * 1024 * 1024;
constexpr int64_t MAX_MAPINFO_BYTES = 8ll * 1024 * 1024;
constexpr int MAX_MAPINFO_ENTRIES = 50000;
const char *const MAPINFO_URL = "https://ddnet.org/releases/maps.json";

class CPreviewViewport
{
	IGraphics *m_pGraphics;
	bool m_Pushed = false;

public:
	explicit CPreviewViewport(IGraphics *pGraphics) :
		m_pGraphics(pGraphics) {}

	~CPreviewViewport()
	{
		if(m_Pushed)
			m_pGraphics->PopPreviewViewport();
	}

	void Push(int X, int Y, int W, int H)
	{
		m_pGraphics->PushPreviewViewport(X, Y, W, H);
		m_Pushed = true;
	}
};

void DrawOutsideRoundedCorners(IGraphics *pGraphics, float x, float y, float w, float h, float r, ColorRGBA Color)
{
	if(r <= 0.0f || w < r * 2.0f || h < r * 2.0f)
		return;

	const int NumSegments = 8;
	IGraphics::CFreeformItem aItems[NumSegments * 4];
	int Num = 0;
	const float Step = pi / 2.0f / NumSegments;
	for(int i = 0; i < NumSegments; i++)
	{
		const float A0 = i * Step;
		const float A1 = (i + 1) * Step;
		const float C0 = std::cos(A0);
		const float S0 = std::sin(A0);
		const float C1 = std::cos(A1);
		const float S1 = std::sin(A1);
		const float Ax0 = x + (1.0f - C0) * r;
		const float Ay0 = y + (1.0f - S0) * r;
		const float Ax1 = x + (1.0f - C1) * r;
		const float Ay1 = y + (1.0f - S1) * r;
		aItems[Num++] = IGraphics::CFreeformItem(x, y, Ax0, Ay0, Ax1, Ay1, Ax1, Ay1);
		aItems[Num++] = IGraphics::CFreeformItem(x + w, y, x + w - (Ax0 - x), Ay0, x + w - (Ax1 - x), Ay1, x + w - (Ax1 - x), Ay1);
		aItems[Num++] = IGraphics::CFreeformItem(x, y + h, Ax0, y + h - (Ay0 - y), Ax1, y + h - (Ay1 - y), Ax1, y + h - (Ay1 - y));
		aItems[Num++] = IGraphics::CFreeformItem(x + w, y + h, x + w - (Ax0 - x), y + h - (Ay0 - y), x + w - (Ax1 - x), y + h - (Ay1 - y), x + w - (Ax1 - x), y + h - (Ay1 - y));
	}

	pGraphics->TextureClear();
	pGraphics->QuadsBegin();
	pGraphics->SetColor(Color);
	pGraphics->QuadsDrawFreeform(aItems, Num);
	pGraphics->QuadsEnd();
}

float ZoomForView(float Aspect, float WorldH)
{
	const float Amount = 1150.0f * 1000.0f;
	const float WMax = 1500.0f;
	const float HMax = 1050.0f;
	const float SafeAspect = std::max(Aspect, 0.001f);
	const float Div = std::sqrt(Amount) / std::sqrt(SafeAspect);
	float PreW = Div * SafeAspect;
	float PreH = Div;
	if(PreW > WMax)
	{
		PreW = WMax;
		PreH = PreW / SafeAspect;
	}
	if(PreH > HMax)
	{
		PreH = HMax;
		PreW = PreH * SafeAspect;
	}
	if(PreH <= 0.0f)
		return 1.0f;
	return std::max(WorldH / PreH, 0.05f);
}

void DrawGripStroke(IGraphics::CFreeformItem *pItem, float X0, float Y0, float X1, float Y1, float Thick)
{
	const float Dx = X1 - X0;
	const float Dy = Y1 - Y0;
	const float Len = std::sqrt(Dx * Dx + Dy * Dy);
	const float Nx = -Dy / Len * Thick * 0.5f;
	const float Ny = Dx / Len * Thick * 0.5f;
	*pItem = IGraphics::CFreeformItem(X0 + Nx, Y0 + Ny, X1 + Nx, Y1 + Ny, X0 - Nx, Y0 - Ny, X1 - Nx, Y1 - Ny);
}

void DrawResizeGrip(IGraphics *pGraphics, float Right, float Bottom)
{
	IGraphics::CFreeformItem aItems[3];
	const float Pad = 2.5f;
	const float Step = 3.8f;
	for(int i = 0; i < 3; i++)
	{
		const float Dist = Pad + (i + 1) * Step;
		DrawGripStroke(&aItems[i], Right - Dist, Bottom - Pad, Right - Pad, Bottom - Dist, 1.45f);
	}
	pGraphics->TextureClear();
	pGraphics->QuadsBegin();
	pGraphics->SetColor(0.22f, 0.22f, 0.24f, 0.95f);
	pGraphics->QuadsDrawFreeform(aItems, 3);
	pGraphics->QuadsEnd();
}

ColorRGBA CategoryColor(const char *pCategory)
{
	if(str_comp_nocase(pCategory, "Novice") == 0)
		return ColorRGBA(0.35f, 0.95f, 0.45f, 1.0f);
	if(str_comp_nocase(pCategory, "Moderate") == 0)
		return ColorRGBA(0.95f, 0.85f, 0.25f, 1.0f);
	if(str_comp_nocase(pCategory, "Brutal") == 0)
		return ColorRGBA(1.0f, 0.55f, 0.15f, 1.0f);
	if(str_comp_nocase(pCategory, "Insane") == 0)
		return ColorRGBA(1.0f, 0.25f, 0.35f, 1.0f);
	if(str_comp_nocase(pCategory, "Dummy") == 0)
		return ColorRGBA(0.35f, 0.75f, 1.0f, 1.0f);
	if(str_comp_nocase(pCategory, "Solo") == 0)
		return ColorRGBA(0.85f, 0.45f, 1.0f, 1.0f);
	if(str_comp_nocase(pCategory, "DDmax") == 0)
		return ColorRGBA(0.25f, 0.9f, 0.85f, 1.0f);
	if(str_comp_nocase(pCategory, "Oldschool") == 0)
		return ColorRGBA(0.75f, 0.75f, 0.75f, 1.0f);
	if(str_comp_nocase(pCategory, "Race") == 0)
		return ColorRGBA(0.45f, 0.9f, 0.55f, 1.0f);
	if(str_comp_nocase(pCategory, "Block") == 0)
		return ColorRGBA(1.0f, 0.4f, 0.4f, 1.0f);
	if(str_comp_nocase(pCategory, "Fun") == 0)
		return ColorRGBA(0.95f, 0.55f, 0.95f, 1.0f);
	return ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
}

float DoZoomHandle(CUi *pUi, const void *pId, const CUIRect &Hit, float Current, CUIRect *pHandle)
{
	Current = std::clamp(Current, 0.0f, 1.0f);
	const float HandleH = std::min(26.0f, std::max(12.0f, Hit.h * 0.28f));
	const float HandleW = std::min(6.0f, Hit.w);
	CUIRect Handle;
	Handle.w = HandleW;
	Handle.h = HandleH;
	Handle.x = Hit.x + (Hit.w - Handle.w) * 0.5f;
	const float Travel = std::max(1.0f, Hit.h - Handle.h);
	Handle.y = Hit.y + Travel * Current;

	static float s_GrabOffset = 0.0f;
	bool Grabbed = false;
	if(pUi->CheckActiveItem(pId))
	{
		if(pUi->MouseButton(0))
			Grabbed = true;
		else
			pUi->SetActiveItem(nullptr);
	}
	else if(pUi->HotItem() == pId)
	{
		if(pUi->MouseHovered(&Handle) && pUi->MouseButton(0))
		{
			pUi->SetActiveItem(pId);
			s_GrabOffset = pUi->MouseY() - Handle.y;
			Grabbed = true;
		}
		else if(pUi->MouseButtonClicked(0))
		{
			pUi->SetActiveItem(pId);
			s_GrabOffset = Handle.h * 0.5f;
			Grabbed = true;
		}
	}
	if(pUi->MouseHovered(&Hit) && !pUi->MouseButton(0))
		pUi->SetHotItem(pId);

	if(Grabbed)
	{
		const float Cur = pUi->MouseY() - s_GrabOffset;
		Current = std::clamp((Cur - Hit.y) / Travel, 0.0f, 1.0f);
		Handle.y = Hit.y + Travel * Current;
	}

	if(pHandle != nullptr)
		*pHandle = Handle;
	return Current;
}
}

struct SPreviewSlot
{
	CMapImages m_Images;
	CMapRenderer m_Renderer;
	std::unique_ptr<IMap> m_pMap;
	std::unique_ptr<CLayers> m_pLayers;
	std::unique_ptr<CEnvelopeState> m_pEnvelope;
	char m_aKey[MAX_MAP_LENGTH + SHA256_MAXSTRSIZE]{};
	char m_aMapName[MAX_MAP_LENGTH]{};
	char m_aMapPath[IO_MAX_PATH_LENGTH]{};
	vec2 m_Camera{};
	float m_Zoom = 2.0f;
	float m_MapWidth = 0.0f;
	float m_MapHeight = 0.0f;

	~SPreviewSlot()
	{
		m_Renderer.Clear();
		m_pEnvelope.reset();
		if(m_pLayers)
			m_pLayers->Unload();
		m_pLayers.reset();
		if(m_pMap)
			m_pMap->Unload();
		m_pMap.reset();
	}
};

struct SPreviewCache
{
	std::vector<std::unique_ptr<SPreviewSlot>> m_vpSlots;
};

struct SMapDifficulty
{
	char m_aType[32]{};
	int m_Stars = 0;
	int m_Points = -1;
};

struct SMapDatabase
{
	std::unordered_map<std::string, SMapDifficulty> m_Entries;
};

CServerMapPreview::CServerMapPreview() = default;

CServerMapPreview::~CServerMapPreview()
{
	if(m_pMapInfoRequest)
		m_pMapInfoRequest->Abort();
	if(GameClient() != nullptr)
	{
		if(m_pSlot)
			m_pSlot->m_Images.Unload();
		if(m_pCache)
		{
			for(auto &pSlot : m_pCache->m_vpSlots)
				pSlot->m_Images.Unload();
		}
	}
	m_pSlot.reset();
	m_pCache.reset();
}

void CServerMapPreview::OnInterfacesInit(CGameClient *pClient)
{
	CComponentInterfaces::OnInterfacesInit(pClient);
	m_pMapDatabase = std::make_unique<SMapDatabase>();
	m_pCache = std::make_unique<SPreviewCache>();
}

void CServerMapPreview::Open()
{
	m_Expanded = true;
}

void CServerMapPreview::Collapse()
{
	m_Expanded = false;
	m_OpenAt = -1.0f;
	m_aPendingAddress[0] = '\0';
}

void CServerMapPreview::ScheduleOpen()
{
	m_OpenAt = Client()->GlobalTime() + CUi::ms_DoubleClickTime;
}

void CServerMapPreview::CancelScheduledOpen()
{
	m_OpenAt = -1.0f;
	m_aPendingAddress[0] = '\0';
}

void CServerMapPreview::SwitchTo(const char *pAddress)
{
	if(pAddress == nullptr || pAddress[0] == '\0')
		return;

	if(str_comp(m_aShownAddress, pAddress) == 0)
	{
		m_aPendingAddress[0] = '\0';
		if(m_Expanded || m_Expand > 0.001f)
			Open();
		else
			ScheduleOpen();
		return;
	}

	if(m_Expanded || m_Expand > 0.001f || m_aPendingAddress[0] != '\0')
	{
		m_Expanded = false;
		str_copy(m_aPendingAddress, pAddress);
		m_OpenAt = Client()->GlobalTime() + CUi::ms_DoubleClickTime;
		return;
	}

	str_copy(m_aShownAddress, pAddress);
	m_aPendingAddress[0] = '\0';
	ScheduleOpen();
}

bool CServerMapPreview::ShowsAddress(const char *pAddress) const
{
	return pAddress != nullptr && m_aShownAddress[0] != '\0' && str_comp(m_aShownAddress, pAddress) == 0;
}

bool CServerMapPreview::TakeReveal()
{
	const bool Reveal = m_Reveal;
	m_Reveal = false;
	return Reveal;
}

void CServerMapPreview::Tick()
{
	const float Now = Client()->LocalTime();
	float Dt = 0.0f;
	if(m_HasExpandTime)
		Dt = std::clamp(Now - m_LastExpandTime, 0.0f, 0.05f);
	m_HasExpandTime = true;
	m_LastExpandTime = Now;

	const float Target = m_Expanded ? 1.0f : 0.0f;
	if(m_Expand < Target)
		m_Expand = std::min(Target, m_Expand + Dt / EXPAND_SECONDS);
	else if(m_Expand > Target)
		m_Expand = std::max(Target, m_Expand - Dt / EXPAND_SECONDS);

	const bool Closed = !m_Expanded && m_Expand <= 0.0f;
	if(Closed && m_OpenAt >= 0.0f && Client()->GlobalTime() >= m_OpenAt)
	{
		if(m_aPendingAddress[0] != '\0')
		{
			str_copy(m_aShownAddress, m_aPendingAddress);
			m_aPendingAddress[0] = '\0';
			AbortDownload();
			ParkSlot();
			m_State = EState::EMPTY;
			m_aKey[0] = '\0';
		}
		m_OpenAt = -1.0f;
		m_Reveal = true;
		Open();
		m_Expand = std::max(m_Expand, std::min(1.0f, std::max(Dt, 0.001f) / EXPAND_SECONDS));
	}

	const bool Settled = m_Expanded && m_Expand >= 1.0f;
	m_LoadArmed = Settled && m_SettledFrame;
	m_SettledFrame = Settled;

	EnsureMapDatabase();
	PollMapDatabase();
}

float CServerMapPreview::RowHeight() const
{
	const float Smooth = m_Expand * m_Expand * (3.0f - 2.0f * m_Expand);
	return m_PreviewHeight * Smooth;
}

void CServerMapPreview::Clear()
{
	m_OpenAt = -1.0f;
	m_Reveal = false;
	m_aPendingAddress[0] = '\0';
	m_aShownAddress[0] = '\0';
	m_Expanded = false;
	m_Expand = 0.0f;
	m_SettledFrame = false;
	m_LoadArmed = false;
	if(m_State == EState::EMPTY && m_pDownload == nullptr && m_pSlot == nullptr)
		return;

	AbortDownload();
	ParkSlot();
	m_State = EState::EMPTY;
	m_aKey[0] = '\0';
	m_aMapName[0] = '\0';
	m_aMapPath[0] = '\0';
	m_Camera = vec2(0.0f, 0.0f);
	m_Zoom = ZOOM_MAX;
	m_MapWidth = 0.0f;
	m_MapHeight = 0.0f;
}

void CServerMapPreview::UnloadMap()
{
	if(m_pSlot && GameClient() != nullptr)
		m_pSlot->m_Images.Unload();
	m_pSlot.reset();
}

void CServerMapPreview::ParkSlot()
{
	if(m_pSlot == nullptr)
		return;
	if(m_State != EState::READY || m_pSlot->m_aKey[0] == '\0')
	{
		UnloadMap();
		return;
	}

	m_pSlot->m_Camera = m_Camera;
	m_pSlot->m_Zoom = m_Zoom;
	m_pSlot->m_MapWidth = m_MapWidth;
	m_pSlot->m_MapHeight = m_MapHeight;
	str_copy(m_pSlot->m_aMapName, m_aMapName);
	str_copy(m_pSlot->m_aMapPath, m_aMapPath);

	if(m_pCache == nullptr)
		m_pCache = std::make_unique<SPreviewCache>();
	for(auto It = m_pCache->m_vpSlots.begin(); It != m_pCache->m_vpSlots.end(); ++It)
	{
		if(str_comp((*It)->m_aKey, m_pSlot->m_aKey) == 0)
		{
			if(GameClient() != nullptr)
				(*It)->m_Images.Unload();
			m_pCache->m_vpSlots.erase(It);
			break;
		}
	}
	m_pCache->m_vpSlots.push_back(std::move(m_pSlot));
	while((int)m_pCache->m_vpSlots.size() > MAX_CACHED_MAPS)
	{
		if(GameClient() != nullptr)
			m_pCache->m_vpSlots.front()->m_Images.Unload();
		m_pCache->m_vpSlots.erase(m_pCache->m_vpSlots.begin());
	}
}

bool CServerMapPreview::TakeCached(const char *pKey)
{
	if(pKey == nullptr || pKey[0] == '\0' || m_pCache == nullptr)
		return false;

	std::unique_ptr<SPreviewSlot> pTaken;
	for(auto It = m_pCache->m_vpSlots.begin(); It != m_pCache->m_vpSlots.end(); ++It)
	{
		if(str_comp((*It)->m_aKey, pKey) != 0)
			continue;
		pTaken = std::move(*It);
		m_pCache->m_vpSlots.erase(It);
		break;
	}
	if(pTaken == nullptr)
		return false;

	ParkSlot();
	m_pSlot = std::move(pTaken);
	m_State = EState::READY;
	str_copy(m_aKey, m_pSlot->m_aKey);
	str_copy(m_aMapName, m_pSlot->m_aMapName);
	str_copy(m_aMapPath, m_pSlot->m_aMapPath);
	m_Camera = m_pSlot->m_Camera;
	m_Zoom = m_pSlot->m_Zoom;
	m_MapWidth = m_pSlot->m_MapWidth;
	m_MapHeight = m_pSlot->m_MapHeight;
	return true;
}

bool CServerMapPreview::HasMap(const CServerInfo *pInfo) const
{
	if(pInfo == nullptr)
		return false;
	char aKey[sizeof(m_aKey)];
	FormatKey(aKey, sizeof(aKey), pInfo);
	if(m_State == EState::READY && str_comp(aKey, m_aKey) == 0)
		return true;
	if(m_pCache == nullptr)
		return false;
	for(const auto &pSlot : m_pCache->m_vpSlots)
	{
		if(str_comp(pSlot->m_aKey, aKey) == 0)
			return true;
	}
	return false;
}

void CServerMapPreview::AbortDownload()
{
	if(m_pDownload)
	{
		m_pDownload->Abort();
		m_pDownload.reset();
	}
}

void CServerMapPreview::FormatKey(char *pKey, int KeySize, const CServerInfo *pInfo)
{
	if(pInfo->m_HasMapSha256)
	{
		char aSha256[SHA256_MAXSTRSIZE];
		sha256_str(pInfo->m_MapSha256, aSha256, sizeof(aSha256));
		str_format(pKey, KeySize, "%s\n%s", pInfo->m_aMap, aSha256);
	}
	else
	{
		str_format(pKey, KeySize, "%s\n%08x", pInfo->m_aMap, (unsigned)pInfo->m_MapCrc);
	}
}

bool CServerMapPreview::FindLocalMap(const CServerInfo *pInfo)
{
	if(pInfo->m_HasMapSha256)
	{
		char aSha256[SHA256_MAXSTRSIZE];
		sha256_str(pInfo->m_MapSha256, aSha256, sizeof(aSha256));
		str_format(m_aMapPath, sizeof(m_aMapPath), "downloadedmaps/%s_%s.map", pInfo->m_aMap, aSha256);
		if(Storage()->FileExists(m_aMapPath, IStorage::TYPE_SAVE))
			return true;
	}

	str_format(m_aMapPath, sizeof(m_aMapPath), "maps/%s.map", pInfo->m_aMap);
	return Storage()->FileExists(m_aMapPath, IStorage::TYPE_ALL);
}

bool CServerMapPreview::BuildUrl(char *pUrl, int UrlSize, const CServerInfo *pInfo) const
{
	if(pInfo->m_aMapUrl[0] != '\0')
	{
		if(str_length(pInfo->m_aMapUrl) >= UrlSize || str_has_cc(pInfo->m_aMapUrl) || !str_startswith(pInfo->m_aMapUrl, "https://"))
			return false;
		str_copy(pUrl, pInfo->m_aMapUrl, UrlSize);
		return true;
	}

	if(!pInfo->m_HasMapSha256 || str_has_cc(g_Config.m_ClMapDownloadUrl) || !str_startswith(g_Config.m_ClMapDownloadUrl, "https://"))
		return false;

	char aSha256[SHA256_MAXSTRSIZE];
	sha256_str(pInfo->m_MapSha256, aSha256, sizeof(aSha256));
	char aFilename[IO_MAX_PATH_LENGTH];
	str_format(aFilename, sizeof(aFilename), "%s_%s.map", pInfo->m_aMap, aSha256);
	char aEscaped[256];
	EscapeUrl(aEscaped, aFilename);

	char aBase[128];
	str_copy(aBase, g_Config.m_ClMapDownloadUrl);
	int Length = str_length(aBase);
	while(Length > 0 && aBase[Length - 1] == '/')
	{
		aBase[Length - 1] = '\0';
		--Length;
	}
	if(Length + 1 + str_length(aEscaped) >= UrlSize)
		return false;
	str_format(pUrl, UrlSize, "%s/%s", aBase, aEscaped);
	return str_startswith(pUrl, "https://") != nullptr;
}

bool CServerMapPreview::StartDownload(const CServerInfo *pInfo)
{
	char aUrl[256];
	if(!BuildUrl(aUrl, sizeof(aUrl), pInfo))
		return false;

	char aSha256[SHA256_MAXSTRSIZE];
	sha256_str(pInfo->m_MapSha256, aSha256, sizeof(aSha256));
	str_format(m_aMapPath, sizeof(m_aMapPath), "downloadedmaps/%s_%s.map", pInfo->m_aMap, aSha256);

	const int64_t MaxSize = pInfo->m_MapSize > 0 ? pInfo->m_MapSize : MAX_MAP_BYTES;
	m_pDownload = HttpGetFile(aUrl, Storage(), m_aMapPath, IStorage::TYPE_SAVE);
	m_pDownload->Timeout(CTimeout{g_Config.m_ClMapDownloadConnectTimeoutMs, 0, g_Config.m_ClMapDownloadLowSpeedLimit, g_Config.m_ClMapDownloadLowSpeedTime});
	m_pDownload->MaxResponseSize(MaxSize);
	m_pDownload->ExpectSha256(pInfo->m_MapSha256);
	m_pDownload->SkipByFileTime(false);
	m_pDownload->LogProgress(HTTPLOG::NONE);
	Http()->Run(m_pDownload);
	return true;
}

void CServerMapPreview::ResetCamera()
{
	m_Zoom = ZOOM_MAX;
	m_Camera = vec2(m_MapWidth * 0.5f, m_MapHeight * 0.5f);
	CMapItemLayerTilemap *pGame = m_pSlot && m_pSlot->m_pLayers ? m_pSlot->m_pLayers->GameLayer() : nullptr;
	if(pGame == nullptr || pGame->m_Width <= 0 || pGame->m_Height <= 0 || pGame->m_Data < 0)
		return;

	const CTile *pTiles = static_cast<const CTile *>(m_pSlot->m_pMap->GetData(pGame->m_Data));
	const int DataSize = m_pSlot->m_pMap->GetDataSize(pGame->m_Data);
	const size_t Need = (size_t)pGame->m_Width * pGame->m_Height * sizeof(CTile);
	if(pTiles == nullptr || DataSize < 0 || (size_t)DataSize < Need)
	{
		m_pSlot->m_pMap->UnloadData(pGame->m_Data);
		return;
	}

	std::vector<std::pair<int, int>> Spawns;
	std::vector<std::pair<int, int>> TeamSpawns;
	int MinX = pGame->m_Width;
	int MinY = pGame->m_Height;
	int MaxX = -1;
	int MaxY = -1;
	for(int y = 0; y < pGame->m_Height; ++y)
	{
		for(int x = 0; x < pGame->m_Width; ++x)
		{
			const CTile &Tile = pTiles[(size_t)y * pGame->m_Width + x];
			if(Tile.m_Index >= ENTITY_OFFSET)
			{
				const int Entity = Tile.m_Index - ENTITY_OFFSET;
				if(Entity == ENTITY_SPAWN)
					Spawns.emplace_back(x, y);
				else if(Entity == ENTITY_SPAWN_RED || Entity == ENTITY_SPAWN_BLUE)
					TeamSpawns.emplace_back(x, y);
			}
			if(Tile.m_Index != 0)
			{
				MinX = std::min(MinX, x);
				MinY = std::min(MinY, y);
				MaxX = std::max(MaxX, x);
				MaxY = std::max(MaxY, y);
			}
		}
	}
	m_pSlot->m_pMap->UnloadData(pGame->m_Data);

	const std::vector<std::pair<int, int>> &PickFrom = !Spawns.empty() ? Spawns : TeamSpawns;
	if(!PickFrom.empty())
	{
		const std::pair<int, int> Spawn = PickFrom[secure_rand_below((int)PickFrom.size())];
		m_Camera = vec2(Spawn.first * 32.0f + 16.0f, Spawn.second * 32.0f + 16.0f);
	}
	else if(MaxX >= MinX && MaxY >= MinY)
		m_Camera = vec2((MinX + MaxX + 1) * 16.0f, (MinY + MaxY + 1) * 16.0f);
}

void CServerMapPreview::ClampCamera(float Aspect)
{
	if(m_MapWidth <= 0.0f || m_MapHeight <= 0.0f)
		return;

	float ViewW = 0.0f;
	float ViewH = 0.0f;
	Graphics()->CalcScreenParams(Aspect, m_Zoom, &ViewW, &ViewH);
	if(m_MapWidth <= ViewW)
		m_Camera.x = m_MapWidth * 0.5f;
	else
		m_Camera.x = std::clamp(m_Camera.x, 0.0f, m_MapWidth);
	if(m_MapHeight <= ViewH)
		m_Camera.y = m_MapHeight * 0.5f;
	else
		m_Camera.y = std::clamp(m_Camera.y, 0.0f, m_MapHeight);
}

bool CServerMapPreview::LoadMap()
{
	ParkSlot();
	m_pSlot = std::make_unique<SPreviewSlot>();
	m_pSlot->m_Images.OnInterfacesInit(GameClient());
	m_pSlot->m_Images.SetTextureScale(g_Config.m_ClTextEntitiesSize);
	str_copy(m_pSlot->m_aKey, m_aKey);
	str_copy(m_pSlot->m_aMapName, m_aMapName);
	str_copy(m_pSlot->m_aMapPath, m_aMapPath);

	m_pSlot->m_pMap = CreateMap();
	if(!m_pSlot->m_pMap->Load(m_aMapName, Storage(), m_aMapPath, IStorage::TYPE_ALL))
	{
		UnloadMap();
		return false;
	}

	m_pSlot->m_pLayers = std::make_unique<CLayers>();
	m_pSlot->m_pLayers->Init(m_pSlot->m_pMap.get(), false, true);
	m_pSlot->m_Images.LoadBackground(m_pSlot->m_pLayers.get(), m_pSlot->m_pMap.get());
	m_pSlot->m_pEnvelope = std::make_unique<CEnvelopeState>(m_pSlot->m_pMap.get(), false);
	m_pSlot->m_pEnvelope->OnInterfacesInit(GameClient());
	m_pSlot->m_Renderer.OnInit(Graphics(), TextRender(), GameClient()->RenderMap());
	m_pSlot->m_Renderer.Load(ERenderType::RENDERTYPE_FULL_DESIGN, m_pSlot->m_pLayers.get(), &m_pSlot->m_Images, m_pSlot->m_pEnvelope.get(), std::nullopt);

	m_MapWidth = 0.0f;
	m_MapHeight = 0.0f;
	if(m_pSlot->m_pLayers->GameLayer() != nullptr)
	{
		m_MapWidth = m_pSlot->m_pLayers->GameLayer()->m_Width * 32.0f;
		m_MapHeight = m_pSlot->m_pLayers->GameLayer()->m_Height * 32.0f;
	}
	ResetCamera();
	m_pSlot->m_Camera = m_Camera;
	m_pSlot->m_Zoom = m_Zoom;
	m_pSlot->m_MapWidth = m_MapWidth;
	m_pSlot->m_MapHeight = m_MapHeight;
	return true;
}

void CServerMapPreview::PollDownload()
{
	if(m_State != EState::DOWNLOADING || m_pDownload == nullptr || !m_pDownload->Done())
		return;

	const bool Ok = m_pDownload->State() == EHttpState::DONE && Storage()->FileExists(m_aMapPath, IStorage::TYPE_SAVE);
	m_pDownload.reset();
	if(Ok && LoadMap())
		m_State = EState::READY;
	else
		m_State = EState::FAILED;
}

void CServerMapPreview::MarkUnavailable(const CServerInfo *pInfo)
{
	if(pInfo == nullptr)
		return;

	char aKey[sizeof(m_aKey)];
	FormatKey(aKey, sizeof(aKey), pInfo);
	if(str_comp(aKey, m_aKey) == 0 && m_State != EState::EMPTY)
		return;
	if(pInfo->m_aMap[0] != '\0' && (pInfo->m_HasMapSha256 || FindLocalMap(pInfo)))
		return;

	AbortDownload();
	ParkSlot();
	str_copy(m_aKey, aKey);
	str_copy(m_aMapName, pInfo->m_aMap);
	m_aMapPath[0] = '\0';
	m_State = EState::FAILED;
}

void CServerMapPreview::SetServer(const CServerInfo *pInfo)
{
	if(pInfo == nullptr || pInfo->m_aMap[0] == '\0')
	{
		MarkUnavailable(pInfo);
		return;
	}

	char aKey[sizeof(m_aKey)];
	FormatKey(aKey, sizeof(aKey), pInfo);
	if(str_comp(aKey, m_aKey) == 0 && m_State == EState::DOWNLOADING)
	{
		PollDownload();
		return;
	}
	if(str_comp(aKey, m_aKey) == 0 && (m_State == EState::READY || m_State == EState::FAILED))
		return;
	if(TakeCached(aKey))
		return;

	AbortDownload();
	ParkSlot();
	str_copy(m_aKey, aKey);
	str_copy(m_aMapName, pInfo->m_aMap);
	m_Zoom = ZOOM_MAX;

	if(FindLocalMap(pInfo))
	{
		m_State = LoadMap() ? EState::READY : EState::FAILED;
		return;
	}
	if(pInfo->m_HasMapSha256 && StartDownload(pInfo))
	{
		m_State = EState::DOWNLOADING;
		return;
	}
	m_aMapPath[0] = '\0';
	m_State = EState::FAILED;
}

const char *CServerMapPreview::StatusText() const
{
	switch(m_State)
	{
	case EState::DOWNLOADING:
	case EState::EMPTY:
		return Localize("Loading map");
	case EState::FAILED:
		return Localize("Preview unavailable");
	default:
		return "";
	}
}

bool CServerMapPreview::VisibleSlice(const CUIRect &Rect, CUIRect *pVisible) const
{
	*pVisible = Rect;
	if(!Ui()->IsClipped())
		return Rect.w > 0.5f && Rect.h > 0.5f;
	const CUIRect *pClip = Ui()->ClipArea();
	const float X0 = std::max(Rect.x, pClip->x);
	const float Y0 = std::max(Rect.y, pClip->y);
	const float X1 = std::min(Rect.x + Rect.w, pClip->x + pClip->w);
	const float Y1 = std::min(Rect.y + Rect.h, pClip->y + pClip->h);
	pVisible->x = X0;
	pVisible->y = Y0;
	pVisible->w = X1 - X0;
	pVisible->h = Y1 - Y0;
	return pVisible->w > 0.5f && pVisible->h > 0.5f;
}

void CServerMapPreview::HandleInput(const CUIRect &MapRect, const CUIRect &WheelRect, const CUIRect &Ignore)
{
	if(MapRect.w <= 0.0f || MapRect.h <= 0.0f)
		return;

	const float Aspect = MapRect.w / MapRect.h;
	static CButtonContainer s_Drag;
	if(!Ui()->MouseHovered(&Ignore) || Ui()->CheckActiveItem(&s_Drag))
		Ui()->DoButtonLogic(&s_Drag, 0, &MapRect, BUTTONFLAG_LEFT);
	if(m_State == EState::READY && Ui()->CheckActiveItem(&s_Drag) && Ui()->MouseButton(0))
	{
		const CUIRect *pScreen = Ui()->Screen();
		const float WindowW = std::max(1, Graphics()->WindowWidth());
		const float WindowH = std::max(1, Graphics()->WindowHeight());
		const float Dx = Ui()->MouseDeltaX() * pScreen->w / WindowW;
		const float Dy = Ui()->MouseDeltaY() * pScreen->h / WindowH;
		float ViewW = 0.0f;
		float ViewH = 0.0f;
		Graphics()->CalcScreenParams(Aspect, m_Zoom, &ViewW, &ViewH);
		m_Camera.x -= Dx * ViewW / MapRect.w;
		m_Camera.y -= Dy * ViewH / MapRect.h;
	}
	if(Ui()->MouseHovered(&WheelRect))
	{
		if(Ui()->ConsumeHotkey(CUi::HOTKEY_SCROLL_UP))
			m_Zoom /= ZOOM_STEP;
		else if(Ui()->ConsumeHotkey(CUi::HOTKEY_SCROLL_DOWN))
			m_Zoom *= ZOOM_STEP;
	}
	m_Zoom = std::clamp(m_Zoom, ZOOM_MIN, ZOOM_MAX);
	if(m_State == EState::READY)
		ClampCamera(Aspect);
}

void CServerMapPreview::RenderMap(const CUIRect &Rect)
{
	CUIRect Visible;
	if(m_State != EState::READY || m_pSlot == nullptr || m_pSlot->m_pLayers == nullptr || !VisibleSlice(Rect, &Visible))
		return;

	const CUIRect *pUiScreen = Ui()->Screen();
	if(pUiScreen->w <= 0.0f || pUiScreen->h <= 0.0f || Graphics()->ScreenWidth() <= 0 || Graphics()->ScreenHeight() <= 0)
		return;

	const float XScale = Graphics()->ScreenWidth() / pUiScreen->w;
	const float YScale = Graphics()->ScreenHeight() / pUiScreen->h;
	const int X = (int)std::lround(Visible.x * XScale) + Graphics()->ViewportX();
	const int Y = (int)std::lround(Visible.y * YScale);
	const int W = std::max(1, (int)std::lround(Visible.w * XScale));
	const int H = std::max(1, (int)std::lround(Visible.h * YScale));
	const float FullAspect = std::max(Rect.w / std::max(Rect.h, 0.001f), 0.001f);
	float FullW = 0.0f;
	float FullH = 0.0f;
	Graphics()->CalcScreenParams(FullAspect, m_Zoom, &FullW, &FullH);
	const float U = (Visible.x + Visible.w * 0.5f - Rect.x) / Rect.w;
	const float V = (Visible.y + Visible.h * 0.5f - Rect.y) / Rect.h;
	const vec2 SliceCenter(m_Camera.x + (U - 0.5f) * FullW, m_Camera.y + (V - 0.5f) * FullH);
	const float SliceZoom = ZoomForView(Visible.w / std::max(Visible.h, 0.001f), FullH * Visible.h / Rect.h);

	const bool WasClipped = Ui()->IsClipped();
	CUIRect SavedClip;
	if(WasClipped)
	{
		SavedClip = *Ui()->ClipArea();
		Ui()->ClipDisable();
	}
	{
		CPreviewViewport Viewport(Graphics());
		Viewport.Push(X, Y, W, H);

		CRenderLayerParams Params;
		Params.m_RenderType = ERenderType::RENDERTYPE_FULL_DESIGN;
		Params.m_EntityOverlayVal = 0;
		Params.m_Center = SliceCenter;
		Params.m_Zoom = SliceZoom;
		Params.m_RenderText = false;
		Params.m_RenderInvalidTiles = false;
		Params.m_TileAndQuadBuffering = true;
		Params.m_RenderTileBorder = false;
		Params.m_DebugRenderGroupClips = false;
		Params.m_DebugRenderQuadClips = false;
		Params.m_DebugRenderClusterClips = false;
		Params.m_DebugRenderTileClips = false;
		Params.m_FpsFogEnabled = false;
		Params.m_FpsFogCullMapTiles = false;
		Params.m_FpsFogHalfW = 0.0f;
		Params.m_FpsFogHalfH = 0.0f;
		Params.m_DisableMapQuads = false;
		Params.m_MapPreview = true;
		m_pSlot->m_Renderer.Render(Params);
	}

	Graphics()->BlendNormal();
	Graphics()->WrapNormal();
	if(WasClipped)
		Ui()->ClipEnable(&SavedClip);
}

void CServerMapPreview::Render(CUIRect Rect, ColorRGBA FrameColor)
{
	if(Rect.h < 4.0f || Rect.w < 16.0f)
		return;

	CUIRect Map = Rect;
	const float Pad = std::min(4.0f, Rect.h * 0.08f);
	Map.Margin(Pad, &Map);
	if(Map.h < 12.0f || Map.w < 28.0f)
		return;

	CUIRect PanRect = Map;
	CUIRect ZoomHit;
	PanRect.VSplitLeft(16.0f, &ZoomHit, &PanRect);
	const float SliderGap = std::min(16.0f, ZoomHit.h * 0.12f);
	if(ZoomHit.h > SliderGap * 2.0f + 24.0f)
		ZoomHit.HMargin(SliderGap, &ZoomHit);

	static CButtonContainer s_Zoom;
	const float ZoomSpan = ZOOM_MAX - ZOOM_MIN;
	float ZoomT = ZoomSpan > 0.0f ? (m_Zoom - ZOOM_MIN) / ZoomSpan : 0.0f;
	CUIRect Handle;
	ZoomT = DoZoomHandle(Ui(), &s_Zoom, ZoomHit, ZoomT, &Handle);
	m_Zoom = std::clamp(ZOOM_MIN + ZoomT * ZoomSpan, ZOOM_MIN, ZOOM_MAX);

	CUIRect Grip;
	Grip.w = RESIZE_GRIP;
	Grip.h = RESIZE_GRIP;
	Grip.x = Map.x + Map.w - Grip.w;
	Grip.y = Map.y + Map.h - Grip.h;
	static CButtonContainer s_Resize;
	Ui()->DoDraggableButtonLogic(&s_Resize, 0, &Grip, nullptr, nullptr);
	const bool Resizing = Ui()->CheckActiveItem(&s_Resize);
	if(Resizing && Ui()->MouseButton(0))
	{
		const CUIRect *pScreen = Ui()->Screen();
		const float WindowH = std::max(1, Graphics()->WindowHeight());
		m_PreviewHeight = std::clamp(m_PreviewHeight + Ui()->MouseDeltaY() * pScreen->h / WindowH, HEIGHT_MIN, HEIGHT_MAX);
	}

	if(!Resizing)
		HandleInput(PanRect, Map, Grip);
	if(m_State == EState::READY)
	{
		RenderMap(Map);
		const float Radius = std::min(CORNER_RADIUS, Map.h * 0.5f);
		const ColorRGBA Backdrop(0.10f, 0.10f, 0.11f, 1.0f);
		DrawOutsideRoundedCorners(Graphics(), Map.x, Map.y, Map.w, Map.h, Radius, Backdrop);
		DrawOutsideRoundedCorners(Graphics(), Map.x, Map.y, Map.w, Map.h, Radius, FrameColor);
	}
	CUIRect Rail;
	ZoomHit.VMargin(std::max(0.0f, (ZoomHit.w - 6.0f) * 0.5f), &Rail);
	Rail.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_ALL, Rail.w * 0.5f);
	Handle.Draw(CUi::ms_ScrollBarColorFunction.GetColor(Ui()->CheckActiveItem(&s_Zoom), Ui()->HotItem() == &s_Zoom), IGraphics::CORNER_ALL, Handle.w * 0.5f);
	RenderDifficulty(Map);
	if(Map.h >= RESIZE_GRIP && Map.w >= RESIZE_GRIP * 2.0f)
		DrawResizeGrip(Graphics(), Map.x + Map.w, Map.y + Map.h);
	if(m_State != EState::READY)
	{
		const char *pStatus = StatusText();
		const float FontSize = 14.0f;
		const float TextW = TextRender()->TextWidth(FontSize, pStatus, -1);
		CUIRect Chip;
		Chip.w = std::min(std::max(8.0f, PanRect.w - 8.0f), TextW + 16.0f);
		Chip.h = std::min(std::max(8.0f, Map.h - 8.0f), FontSize + 8.0f);
		Chip.x = PanRect.x + (PanRect.w - Chip.w) * 0.5f;
		Chip.y = Map.y + (Map.h - Chip.h) * 0.5f;
		Chip.Draw(ColorRGBA(0.93f, 0.93f, 0.94f, 1.0f), IGraphics::CORNER_ALL, Chip.h * 0.5f);
		TextRender()->TextColor(ColorRGBA(0.08f, 0.08f, 0.09f, 1.0f));
		TextRender()->TextOutlineColor(ColorRGBA(0.93f, 0.93f, 0.94f, 1.0f));
		Ui()->DoLabel(&Chip, pStatus, FontSize, TEXTALIGN_MC);
		TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
}

void CServerMapPreview::EnsureMapDatabase()
{
	if(m_MapInfoStarted || m_pMapDatabase == nullptr)
		return;
	m_MapInfoStarted = true;

	void *pBuf = nullptr;
	unsigned Length = 0;
	if(Storage()->ReadFile("ddnet-maps.json", IStorage::TYPE_SAVE, &pBuf, &Length))
	{
		ParseMapDatabase(static_cast<const char *>(pBuf), Length);
		free(pBuf);
	}

	if(str_startswith(MAPINFO_URL, "https://") == nullptr)
		return;

	m_pMapInfoRequest = HttpGet(MAPINFO_URL);
	m_pMapInfoRequest->Timeout(CTimeout{5000, 0, 500, 5});
	m_pMapInfoRequest->MaxResponseSize(MAX_MAPINFO_BYTES);
	m_pMapInfoRequest->LogProgress(HTTPLOG::NONE);
	Http()->Run(m_pMapInfoRequest);
}

void CServerMapPreview::PollMapDatabase()
{
	if(m_pMapInfoRequest == nullptr || !m_pMapInfoRequest->Done())
		return;

	if(m_pMapInfoRequest->State() == EHttpState::DONE)
	{
		unsigned char *pData = nullptr;
		size_t Length = 0;
		m_pMapInfoRequest->Result(&pData, &Length);
		if(pData != nullptr && Length > 0 && Length <= (size_t)MAX_MAPINFO_BYTES)
		{
			ParseMapDatabase(reinterpret_cast<const char *>(pData), (unsigned)Length);
			IOHANDLE File = Storage()->OpenFile("ddnet-maps.json", IOFLAG_WRITE, IStorage::TYPE_SAVE);
			if(File)
			{
				io_write(File, pData, (unsigned)Length);
				io_close(File);
			}
		}
	}
	m_pMapInfoRequest.reset();
}

void CServerMapPreview::ParseMapDatabase(const char *pJsonData, unsigned Length)
{
	if(m_pMapDatabase == nullptr || pJsonData == nullptr || Length == 0)
		return;

	json_settings JsonSettings{};
	char aError[256];
	json_value *pJson = JsonParseEx(&JsonSettings, (const json_char *)pJsonData, Length, aError);
	if(pJson == nullptr)
		return;

	int Added = 0;
	const auto ParseItem = [&](const json_value &Item) {
		if(Added >= MAX_MAPINFO_ENTRIES || Item.type != json_object)
			return;
		const json_value &Name = Item["name"];
		if(Name.type != json_string || Name.u.string.ptr == nullptr || Name.u.string.ptr[0] == '\0')
			return;
		if(str_has_cc(Name.u.string.ptr) || str_length(Name.u.string.ptr) >= MAX_MAP_LENGTH)
			return;

		SMapDifficulty Entry;
		const json_value &Type = Item["type"];
		const json_value &Server = Item["server"];
		const json_value *pType = Type.type == json_string ? &Type : (Server.type == json_string ? &Server : nullptr);
		if(pType != nullptr && pType->u.string.ptr != nullptr && !str_has_cc(pType->u.string.ptr))
			str_copy(Entry.m_aType, pType->u.string.ptr);

		const json_value &Stars = Item["stars"];
		const json_value &Difficulty = Item["difficulty"];
		const json_value *pStars = nullptr;
		if(Stars.type == json_integer || Stars.type == json_double || Stars.type == json_string)
			pStars = &Stars;
		else if(Difficulty.type == json_integer || Difficulty.type == json_double || Difficulty.type == json_string)
			pStars = &Difficulty;
		if(pStars != nullptr)
		{
			int StarsValue = 0;
			if(pStars->type == json_integer)
				StarsValue = (int)pStars->u.integer;
			else if(pStars->type == json_double)
				StarsValue = (int)std::lround(pStars->u.dbl);
			else if(pStars->u.string.ptr != nullptr)
				StarsValue = str_toint(pStars->u.string.ptr);
			Entry.m_Stars = std::clamp(StarsValue, 0, 5);
		}

		const json_value &Points = Item["points"];
		if(Points.type == json_integer)
			Entry.m_Points = std::max(0, (int)Points.u.integer);
		else if(Points.type == json_double)
			Entry.m_Points = std::max(0, (int)std::lround(Points.u.dbl));
		else if(Points.type == json_string && Points.u.string.ptr != nullptr)
			Entry.m_Points = std::max(0, str_toint(Points.u.string.ptr));

		char aKey[MAX_MAP_LENGTH];
		str_utf8_tolower(Name.u.string.ptr, aKey, sizeof(aKey));
		m_pMapDatabase->m_Entries[aKey] = Entry;
		++Added;
	};

	if(pJson->type == json_array)
	{
		for(unsigned i = 0; i < pJson->u.array.length && Added < MAX_MAPINFO_ENTRIES; ++i)
			ParseItem((*pJson)[i]);
	}
	else if(pJson->type == json_object)
	{
		const json_value &Maps = (*pJson)["maps"];
		const json_value &Releases = (*pJson)["releases"];
		const json_value *pArr = Maps.type == json_array ? &Maps : (Releases.type == json_array ? &Releases : nullptr);
		if(pArr != nullptr)
		{
			for(unsigned i = 0; i < pArr->u.array.length && Added < MAX_MAPINFO_ENTRIES; ++i)
				ParseItem((*pArr)[i]);
		}
		else
		{
			for(unsigned i = 0; i < pJson->u.object.length && Added < MAX_MAPINFO_ENTRIES; ++i)
			{
				const json_value *pValue = pJson->u.object.values[i].value;
				if(pValue != nullptr)
					ParseItem(*pValue);
			}
		}
	}

	json_value_free(pJson);
}

bool CServerMapPreview::DifficultyText(char *pText, int TextSize, ColorRGBA *pColor) const
{
	pText[0] = '\0';
	if(m_pMapDatabase == nullptr || m_aMapName[0] == '\0')
		return false;

	char aKey[MAX_MAP_LENGTH];
	str_utf8_tolower(m_aMapName, aKey, sizeof(aKey));
	const auto It = m_pMapDatabase->m_Entries.find(aKey);
	if(It == m_pMapDatabase->m_Entries.end())
		return false;

	const char *pCategory = It->second.m_aType;
	const int Stars = std::clamp(It->second.m_Stars, 0, 5);
	const int Points = It->second.m_Points;

	char aStars[32];
	aStars[0] = '\0';
	for(int i = 0; i < 5; ++i)
		str_append(aStars, i < Stars ? "★" : "☆", sizeof(aStars));

	if(pCategory[0] != '\0' && Points >= 0)
		str_format(pText, TextSize, "%s %s  %d pts", pCategory, aStars, Points);
	else if(pCategory[0] != '\0')
		str_format(pText, TextSize, "%s %s", pCategory, aStars);
	else if(Points >= 0)
		str_format(pText, TextSize, "%s  %d pts", aStars, Points);
	else
		str_copy(pText, aStars, TextSize);

	*pColor = CategoryColor(pCategory);
	return pText[0] != '\0';
}

void CServerMapPreview::RenderDifficulty(const CUIRect &Rect)
{
	char aText[128];
	ColorRGBA Color;
	if(Rect.h < 24.0f || !DifficultyText(aText, sizeof(aText), &Color))
		return;

	CUIRect Label;
	Rect.HSplitBottom(18.0f, nullptr, &Label);
	if(Label.h <= 0.0f || Label.w <= 0.0f)
		return;
	TextRender()->TextColor(Color);
	TextRender()->TextOutlineColor(ColorRGBA(0.0f, 0.0f, 0.0f, 0.85f));
	Ui()->DoLabel(&Label, aText, 11.0f, TEXTALIGN_MC);
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}
