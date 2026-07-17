#include "background.h"

#include <base/system.h>

#include <engine/map.h>
#include <engine/shared/config.h>

#include <game/client/components/mapimages.h>
#include <game/client/components/maplayers.h>
#include <game/client/gameclient.h>
#include <game/layers.h>
#include <game/localization.h>

CBackground::CBackground(ERenderType MapType, bool OnlineOnly) :
	CMapLayers(MapType, OnlineOnly)
{
	m_pLayers = new CLayers;
	m_pBackgroundLayers = m_pLayers;
	m_pImages = new CMapImages;
	m_pBackgroundImages = m_pImages;
	m_Loaded = false;
	m_aMapName[0] = '\0';
}

CBackground::~CBackground()
{
	delete m_pBackgroundLayers;
	delete m_pBackgroundImages;
}

void CBackground::OnInit()
{
	m_pBackgroundMap = CreateMap();
	m_pMap = m_pBackgroundMap.get();
	m_MediaBackground.Init(Graphics(), Storage());

	m_pImages->OnInterfacesInit(GameClient());
	if(g_Config.m_ClBackgroundEntities[0] != '\0' && str_comp(g_Config.m_ClBackgroundEntities, CURRENT_MAP))
		LoadBackground();
}

void CBackground::OnShutdown()
{
	m_MediaBackground.Shutdown();
}

void CBackground::LoadBackground()
{
	if(m_Loaded && m_pMap == m_pBackgroundMap.get())
		m_pMap->Unload();

	m_Loaded = false;
	m_pMap = m_pBackgroundMap.get();
	m_pLayers = m_pBackgroundLayers;
	m_pImages = m_pBackgroundImages;

	str_copy(m_aMapName, g_Config.m_ClBackgroundEntities);
	if(g_Config.m_ClBackgroundEntities[0] != '\0')
	{
		bool NeedImageLoading = false;

		char aBuf[IO_MAX_PATH_LENGTH];
		str_format(aBuf, sizeof(aBuf), "maps/%s%s", g_Config.m_ClBackgroundEntities, str_endswith(g_Config.m_ClBackgroundEntities, ".map") ? "" : ".map");
		if(str_comp(g_Config.m_ClBackgroundEntities, CURRENT_MAP) == 0)
		{
			m_pMap = GameClient()->Map();
			if(m_pMap->IsLoaded())
			{
				m_pLayers = GameClient()->Layers();
				m_pImages = &GameClient()->m_MapImages;
				m_Loaded = true;
			}
		}
		else if(m_pMap->Load(g_Config.m_ClBackgroundEntities, Storage(), aBuf, IStorage::TYPE_ALL))
		{
			m_pLayers->Init(m_pMap, true);
			NeedImageLoading = true;
			m_Loaded = true;
		}

		if(m_Loaded)
		{
			if(NeedImageLoading)
			{
				m_pImages->LoadBackground(m_pLayers, m_pMap);
			}
			CMapLayers::OnMapLoad();
		}
	}
}

void CBackground::OnMapLoad()
{
	if(str_comp(g_Config.m_ClBackgroundEntities, CURRENT_MAP) == 0 || str_comp(g_Config.m_ClBackgroundEntities, m_aMapName))
	{
		LoadBackground();
	}
}

void CBackground::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		m_MediaBackground.Unload();
		return;
	}

	m_MediaBackground.SyncFromConfig(g_Config.m_BcGameMediaBackground, g_Config.m_BcMenuMediaBackgroundPath);
	m_MediaBackground.Update();
	// Only render with entities enabled: the map design background stays untouched,
	// while in entities view the media replaces the plain background color.
	if(g_Config.m_BcGameMediaBackground && g_Config.m_ClOverlayEntities > 0)
	{
		float ViewWidth = 0.0f;
		float ViewHeight = 0.0f;
		Graphics()->CalcScreenParams(Graphics()->ScreenAspect(), GetCurCamera()->m_Zoom, &ViewWidth, &ViewHeight);

		CMenuMediaBackground::SRenderContext RenderContext;
		RenderContext.m_CameraCenterX = GetCurCamera()->m_Center.x;
		RenderContext.m_CameraCenterY = GetCurCamera()->m_Center.y;
		RenderContext.m_ViewWidth = ViewWidth;
		RenderContext.m_ViewHeight = ViewHeight;
		RenderContext.m_WorldOffset = (float)g_Config.m_BcGameMediaBackgroundOffset / 100.0f;
		if(GameClient()->Layers() != nullptr && GameClient()->Layers()->GameLayer() != nullptr)
		{
			RenderContext.m_MapWidth = GameClient()->Layers()->GameLayer()->m_Width * 32.0f;
			RenderContext.m_MapHeight = GameClient()->Layers()->GameLayer()->m_Height * 32.0f;
		}

		const bool Rendered = m_MediaBackground.Render(ViewWidth, ViewHeight, &RenderContext);

		if(g_Config.m_Debug)
		{
			static int64_t s_LastLogTick = 0;
			const int64_t Now = time_get();
			if(s_LastLogTick == 0 || Now - s_LastLogTick >= time_freq())
			{
				dbg_msg("mediabg", "rendered=%d loaded=%d overlay=%d view=%.1fx%.1f zoom=%.2f offset=%d",
					Rendered ? 1 : 0,
					m_MediaBackground.IsLoaded() ? 1 : 0,
					g_Config.m_ClOverlayEntities,
					ViewWidth, ViewHeight,
					GetCurCamera()->m_Zoom,
					g_Config.m_BcGameMediaBackgroundOffset);
				s_LastLogTick = Now;
			}
		}
	}

	if(g_Config.m_ClOverlayEntities != 100)
		return;

	if(!m_Loaded)
		return;

	CMapLayers::OnRender();
}
