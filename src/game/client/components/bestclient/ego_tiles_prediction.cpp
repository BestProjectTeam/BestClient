/* Copyright © 2026 BestProject Team */
#include "ego_tiles_prediction.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/map/render_map.h>
#include <game/mapitems.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <utility>

namespace
{
enum
{
	EGO_QUADTYPE_NONE = -1,
	EGO_QUADTYPE_FREEZE,
	EGO_QUADTYPE_UNFREEZE,
	EGO_NUM_QUADTYPES
};

constexpr char s_aValidQuadNames[EGO_NUM_QUADTYPES][30] = {
	"QFr",
	"QUnFr"};
}

void CEgoTilesPrediction::CPredQuad::UpdateAabb()
{
	m_AabbMin = m_aPoints[0];
	m_AabbMax = m_aPoints[0];
	for(int i = 1; i < 4; i++)
	{
		m_AabbMin.x = std::min(m_AabbMin.x, m_aPoints[i].x);
		m_AabbMin.y = std::min(m_AabbMin.y, m_aPoints[i].y);
		m_AabbMax.x = std::max(m_AabbMax.x, m_aPoints[i].x);
		m_AabbMax.y = std::max(m_AabbMax.y, m_aPoints[i].y);
	}
}

bool CEgoTilesPrediction::CPredQuad::AabbContains(vec2 Pos) const
{
	return Pos.x >= m_AabbMin.x && Pos.x <= m_AabbMax.x && Pos.y >= m_AabbMin.y && Pos.y <= m_AabbMax.y;
}

int CEgoTilesPrediction::QuadNameToTile(int QuadType)
{
	switch(QuadType)
	{
	case EGO_QUADTYPE_FREEZE: return TILE_FREEZE;
	case EGO_QUADTYPE_UNFREEZE: return TILE_UNFREEZE;
	default: return EGO_QUADTYPE_NONE;
	}
}

void CEgoTilesPrediction::Rotate(vec2 Center, vec2 *pPoint, float Rotation)
{
	const float x = pPoint->x - Center.x;
	const float y = pPoint->y - Center.y;
	pPoint->x = x * std::cos(Rotation) - y * std::sin(Rotation) + Center.x;
	pPoint->y = x * std::sin(Rotation) + y * std::cos(Rotation) + Center.y;
}

bool CEgoTilesPrediction::InsideQuad(vec2 Pos, vec2 T0, vec2 T1, vec2 T2, vec2 T3)
{
	auto IsLeft = [](const vec2 &A, const vec2 &B, const vec2 &P) -> bool {
		return ((B.x - A.x) * (P.y - A.y) - (B.y - A.y) * (P.x - A.x)) >= 0.0f;
	};

	return IsLeft(T0, T1, Pos) &&
		IsLeft(T1, T3, Pos) &&
		IsLeft(T3, T2, Pos) &&
		IsLeft(T2, T0, Pos);
}

void CEgoTilesPrediction::Reset()
{
	m_vQuads.clear();
	m_vAnimatedQuadIndices.clear();
	m_pMap = nullptr;
	m_LastUpdatedTick = -1;
}

void CEgoTilesPrediction::OnStateChange(int NewState, int OldState)
{
	if(NewState != OldState && NewState != IClient::STATE_ONLINE)
		Reset();
}

void CEgoTilesPrediction::OnMapLoad()
{
	LoadQuads();
}

void CEgoTilesPrediction::LoadQuads()
{
	Reset();

	IMap *pMap = GameClient()->Layers()->Map();
	if(!pMap)
		return;
	m_pMap = pMap;

	int GroupsStart, LayersStart, GroupsNum, LayersNum;
	pMap->GetType(MAPITEMTYPE_GROUP, &GroupsStart, &GroupsNum);
	pMap->GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);

	for(int GroupIndex = 0; GroupIndex < GroupsNum; GroupIndex++)
	{
		CMapItemGroup *pGroup = static_cast<CMapItemGroup *>(pMap->GetItem(GroupsStart + GroupIndex));
		for(int LayerIndex = 0; LayerIndex < pGroup->m_NumLayers; LayerIndex++)
		{
			CMapItemLayer *pLayer = static_cast<CMapItemLayer *>(pMap->GetItem(LayersStart + pGroup->m_StartLayer + LayerIndex));
			if(pLayer->m_Type != LAYERTYPE_QUADS)
				continue;

			CMapItemLayerQuads *pQuadsLayer = reinterpret_cast<CMapItemLayerQuads *>(pLayer);
			char aLayerName[30];
			if(!IntsToStr(pQuadsLayer->m_aName, std::size(pQuadsLayer->m_aName), aLayerName, std::size(aLayerName)))
				continue;

			int QuadType = EGO_QUADTYPE_NONE;
			for(size_t NameIndex = 0; NameIndex < std::size(s_aValidQuadNames); NameIndex++)
			{
				if(!str_comp(aLayerName, s_aValidQuadNames[NameIndex]))
				{
					QuadType = (int)NameIndex;
					break;
				}
			}
			if(QuadType == EGO_QUADTYPE_NONE)
				continue;

			const int TileIndex = QuadNameToTile(QuadType);
			CQuad *pQuads = (CQuad *)pMap->GetDataSwapped(pQuadsLayer->m_Data);
			for(int QuadIndex = 0; QuadIndex < pQuadsLayer->m_NumQuads; QuadIndex++)
			{
				CPredQuad QuadData;
				QuadData.m_pQuad = &pQuads[QuadIndex];
				QuadData.m_TileIndex = TileIndex;
				for(int i = 0; i < 5; i++)
					QuadData.m_aLocalPoints[i] = vec2(fx2f(QuadData.m_pQuad->m_aPoints[i].x), fx2f(QuadData.m_pQuad->m_aPoints[i].y));
				std::swap(QuadData.m_aLocalPoints[2], QuadData.m_aLocalPoints[3]);
				for(int i = 0; i < 5; i++)
					QuadData.m_aPoints[i] = QuadData.m_aLocalPoints[i];
				QuadData.m_Animated = QuadData.m_pQuad->m_PosEnv >= 0;
				QuadData.UpdateAabb();

				m_vQuads.push_back(QuadData);
				if(QuadData.m_Animated)
					m_vAnimatedQuadIndices.push_back((int)m_vQuads.size() - 1);
			}
		}
	}
}

void CEgoTilesPrediction::GetAnimationTransform(float GlobalTime, int Env, vec2 &Position, float &Angle) const
{
	Position = vec2(0.0f, 0.0f);
	Angle = 0.0f;
	if(!m_pMap || Env < 0)
		return;

	int EnvelopeStart, EnvelopeNum;
	m_pMap->GetType(MAPITEMTYPE_ENVELOPE, &EnvelopeStart, &EnvelopeNum);
	if(Env >= EnvelopeNum)
		return;

	CMapItemEnvelope *pItem = (CMapItemEnvelope *)m_pMap->GetItem(EnvelopeStart + Env);
	if(pItem->m_NumPoints <= 0)
		return;

	CMapBasedEnvelopePointAccess EnvelopePoints(m_pMap);
	EnvelopePoints.SetPointsRange(pItem->m_StartPoint, pItem->m_NumPoints);
	if(EnvelopePoints.NumPoints() <= 0)
		return;

	ColorRGBA Result(0.0f, 0.0f, 0.0f, 0.0f);
	const auto TimeNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(GlobalTime));
	CRenderMap::RenderEvalEnvelope(&EnvelopePoints, TimeNanos, Result, 3);
	Position.x = Result.r;
	Position.y = Result.g;
	Angle = Result.b / 360.0f * pi * 2.0f;
}

void CEgoTilesPrediction::UpdateForTick(int GameTick, int RoundStartTick, int TickSpeed)
{
	if(!g_Config.m_TcEgoTilesAntiLag || m_vQuads.empty() || TickSpeed <= 0)
		return;
	if(GameTick == m_LastUpdatedTick)
		return;
	m_LastUpdatedTick = GameTick;

	if(m_vAnimatedQuadIndices.empty())
		return;

	const double Time = static_cast<double>(GameTick - RoundStartTick + 1) / static_cast<double>(TickSpeed);
	for(int QuadIndex : m_vAnimatedQuadIndices)
	{
		CPredQuad &QuadData = m_vQuads[QuadIndex];
		if(!QuadData.m_pQuad)
			continue;

		vec2 Position = vec2(0.0f, 0.0f);
		float Angle = 0.0f;
		GetAnimationTransform(Time + (QuadData.m_pQuad->m_PosEnvOffset / 1000.0), QuadData.m_pQuad->m_PosEnv, Position, Angle);
		for(int i = 0; i < 5; i++)
			QuadData.m_aPoints[i] = Position + QuadData.m_aLocalPoints[i];

		if(Angle != 0.0f)
		{
			for(int i = 0; i < 4; i++)
				Rotate(QuadData.m_aPoints[4], &QuadData.m_aPoints[i], Angle);
		}
		QuadData.UpdateAabb();
	}
}

void CEgoTilesPrediction::HandleCharacter(CCharacter *pCharacter) const
{
	if(!g_Config.m_TcEgoTilesAntiLag || !pCharacter || m_vQuads.empty())
		return;
	if(!pCharacter->GameWorld()->m_WorldConfig.m_PredictFreeze)
		return;

	bool InQuadFreeze = false;
	const vec2 Pos = pCharacter->m_Pos;

	for(const CPredQuad &QuadData : m_vQuads)
	{
		if(!QuadData.AabbContains(Pos))
			continue;
		if(!InsideQuad(Pos, QuadData.m_aPoints[0], QuadData.m_aPoints[1], QuadData.m_aPoints[3], QuadData.m_aPoints[2]))
			continue;

		if(QuadData.m_TileIndex == TILE_FREEZE)
		{
			pCharacter->Freeze();
			InQuadFreeze = true;
		}
		else if(QuadData.m_TileIndex == TILE_UNFREEZE && !pCharacter->Core()->m_IsInFreeze)
		{
			pCharacter->Unfreeze();
			InQuadFreeze = false;
		}
	}

	if(InQuadFreeze)
		pCharacter->Freeze();
}
