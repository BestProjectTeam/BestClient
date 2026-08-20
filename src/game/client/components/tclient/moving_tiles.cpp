#include "moving_tiles.h"

#include <base/color.h>
#include <base/dbg.h>
#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/envelope_state.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/map/render_map.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <iterator>

inline static void RotatePoint(const vec2 &Center, vec2 &Point, float Rotation)
{
	const vec2 RelativePos = Point - Center;
	Point.x = RelativePos.x * std::cos(Rotation) - RelativePos.y * std::sin(Rotation) + Center.x;
	Point.y = RelativePos.x * std::sin(Rotation) + RelativePos.y * std::cos(Rotation) + Center.y;
}

inline static float Cross(const vec2 &A, const vec2 &B, const vec2 &C)
{
	const vec2 AB = B - A;
	const vec2 AC = C - A;
	return AB.x * AC.y - AB.y * AC.x;
}

inline static bool PointInTriangle(const vec2 &Point, const vec2 &A, const vec2 &B, const vec2 &C)
{
	const float CrossAB = Cross(A, B, Point);
	const float CrossBC = Cross(B, C, Point);
	const float CrossCA = Cross(C, A, Point);
	const bool HasNegative = CrossAB < 0.0f || CrossBC < 0.0f || CrossCA < 0.0f;
	const bool HasPositive = CrossAB > 0.0f || CrossBC > 0.0f || CrossCA > 0.0f;
	return !HasNegative || !HasPositive;
}

inline static bool PointOnSegment(const vec2 &Point, const vec2 &A, const vec2 &B)
{
	constexpr float Epsilon = 0.001f;
	return absolute(Cross(A, B, Point)) <= Epsilon &&
		Point.x >= minimum(A.x, B.x) - Epsilon && Point.x <= maximum(A.x, B.x) + Epsilon &&
		Point.y >= minimum(A.y, B.y) - Epsilon && Point.y <= maximum(A.y, B.y) + Epsilon;
}

inline static bool SegmentsIntersect(const vec2 &A, const vec2 &B, const vec2 &C, const vec2 &D)
{
	const float CrossABC = Cross(A, B, C);
	const float CrossABD = Cross(A, B, D);
	const float CrossCDA = Cross(C, D, A);
	const float CrossCDB = Cross(C, D, B);

	if(((CrossABC < 0.0f && CrossABD > 0.0f) || (CrossABC > 0.0f && CrossABD < 0.0f)) &&
		((CrossCDA < 0.0f && CrossCDB > 0.0f) || (CrossCDA > 0.0f && CrossCDB < 0.0f)))
		return true;

	return PointOnSegment(C, A, B) || PointOnSegment(D, A, B) || PointOnSegment(A, C, D) || PointOnSegment(B, C, D);
}

inline static bool SegmentIntersectsTriangle(const vec2 &From, const vec2 &To, const vec2 &A, const vec2 &B, const vec2 &C)
{
	return PointInTriangle(From, A, B, C) || PointInTriangle(To, A, B, C) ||
		SegmentsIntersect(From, To, A, B) || SegmentsIntersect(From, To, B, C) || SegmentsIntersect(From, To, C, A);
}

inline static bool QuadName(const int *pInts, size_t NumInts, char *pStr, size_t StrSize)
{
	dbg_assert(NumInts > 0, "QuadName: NumInts invalid");
	dbg_assert(StrSize >= NumInts * sizeof(int), "QuadName: StrSize invalid");

	// Unpack string without validation
	size_t StrIndex = 0;
	for(size_t IntIndex = 0; IntIndex < NumInts; IntIndex++)
	{
		const int CurrentInt = pInts[IntIndex];
		pStr[StrIndex] = ((CurrentInt >> 24) & 0xff) - 128;
		StrIndex++;
		pStr[StrIndex] = ((CurrentInt >> 16) & 0xff) - 128;
		StrIndex++;
		pStr[StrIndex] = ((CurrentInt >> 8) & 0xff) - 128;
		StrIndex++;
		pStr[StrIndex] = (CurrentInt & 0xff) - 128;
		StrIndex++;
	}
	// Ensure null-termination
	pStr[StrIndex - 1] = '\0';

	// Ensure valid UTF-8
	if(str_utf8_check(pStr))
	{
		return true;
	}
	pStr[0] = '\0';
	return false;
}

void CMovingTiles::Reset()
{
	m_vQuads.clear();
}

void CMovingTiles::OnStateChange(int NewState, int OldState)
{
	if(NewState != OldState && NewState != IClient::STATE_ONLINE)
		Reset();
}

void CMovingTiles::OnMapLoad()
{
	Reset();

	int GroupsStart, LayersStart, GroupsNum, LayersNum;

	GameClient()->Map()->GetType(MAPITEMTYPE_GROUP, &GroupsStart, &GroupsNum);
	GameClient()->Map()->GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);

	// size_t NumQuadLayers = 0;
	for(int GroupIndex = 0; GroupIndex < GroupsNum; GroupIndex++)
	{
		CMapItemGroup *pGroup = static_cast<CMapItemGroup *>(GameClient()->Map()->GetItem(GroupsStart + GroupIndex));
		for(int LayerIndex = 0; LayerIndex < pGroup->m_NumLayers; LayerIndex++)
		{
			CMapItemLayer *pLayer = static_cast<CMapItemLayer *>(GameClient()->Map()->GetItem(LayersStart + pGroup->m_StartLayer + LayerIndex));

			if(pLayer->m_Type != LAYERTYPE_QUADS)
				continue;

			char aLayerName[30];
			CMapItemLayerQuads *pTilemap = reinterpret_cast<CMapItemLayerQuads *>(pLayer);
			QuadName(pTilemap->m_aName, std::size(pTilemap->m_aName), aLayerName, std::size(aLayerName));

			for(size_t Names = 0; Names < std::size(ValidQuadNames); Names++)
			{
				if(!str_comp(ValidQuadNames[Names], aLayerName))
				{
					EQType Type = static_cast<EQType>(Names);
					if(!m_RenderAbove && (Type == EQType::HOOKABLE || Type == EQType::UNHOOKABLE))
						continue;
					else if(m_RenderAbove && Type != EQType::HOOKABLE && Type != EQType::UNHOOKABLE)
						continue;

					// NumQuadLayers++;
					CQuad *pQuads = (CQuad *)GameClient()->Map()->GetDataSwapped(pTilemap->m_Data);
					for(int NumQuads = 0; NumQuads < pTilemap->m_NumQuads; NumQuads++)
					{
						CQuadData QuadData;
						QuadData.m_pQuad = &pQuads[NumQuads];
						QuadData.m_pGroup = pGroup;
						QuadData.m_pLayer = pTilemap;
						QuadData.m_Type = Type;
						for(int j = 0; j < 5; j++)
							QuadData.m_Pos[j] = vec2(fx2f(QuadData.m_pQuad->m_aPoints[j].x), fx2f(QuadData.m_pQuad->m_aPoints[j].y));
						m_vQuads.push_back(QuadData);
					}
					break;
				}
			}
		}
	}
	// log_info("moving-tiles", "%" PRIzu " valid quadlayer%s with %d quads", NumQuadLayers, NumQuadLayers == 1 ? "" : "s", (int)m_vQuads.size());
	m_EnvEvaluator = CEnvelopeState(GameClient()->Map(), true);
	m_EnvEvaluator.OnInterfacesInit(GameClient());
}

void CMovingTiles::ApplyEgoTilesAntiLag(CCharacter *pCharacter) const
{
	if(!g_Config.m_TcEgoTilesAntiLag || !pCharacter || m_RenderAbove || m_vQuads.empty())
		return;
	if(pCharacter->m_FreezeTime > 0 || pCharacter->Core()->m_Super || pCharacter->Core()->m_Invincible || pCharacter->Core()->m_DeepFrozen)
		return;

	const vec2 From = pCharacter->Core()->m_Pos;
	const vec2 To = From + pCharacter->Core()->m_Vel;
	for(const CQuadData &QuadData : m_vQuads)
	{
		if(QuadData.m_Type != EQType::FREEZE || !QuadData.m_pQuad)
			continue;

		ColorRGBA Position(0.0f, 0.0f, 0.0f, 0.0f);
		m_EnvEvaluator.EnvelopeEval(QuadData.m_pQuad->m_PosEnvOffset, QuadData.m_pQuad->m_PosEnv, Position, 3);
		const vec2 Offset(Position.r, Position.g);
		const float Rotation = Position.b / 180.0f * pi + QuadData.m_Angle;

		vec2 aPoints[4] = {
			QuadData.m_Pos[0],
			QuadData.m_Pos[1],
			QuadData.m_Pos[2],
			QuadData.m_Pos[3],
		};
		if(Rotation != 0.0f)
		{
			for(vec2 &Point : aPoints)
				RotatePoint(QuadData.m_Pos[4], Point, Rotation);
		}
		for(vec2 &Point : aPoints)
			Point += Offset;

		float MinX = aPoints[0].x;
		float MaxX = aPoints[0].x;
		float MinY = aPoints[0].y;
		float MaxY = aPoints[0].y;
		for(size_t i = 1; i < std::size(aPoints); i++)
		{
			MinX = minimum(MinX, aPoints[i].x);
			MaxX = maximum(MaxX, aPoints[i].x);
			MinY = minimum(MinY, aPoints[i].y);
			MaxY = maximum(MaxY, aPoints[i].y);
		}
		if(maximum(From.x, To.x) < MinX || minimum(From.x, To.x) > MaxX ||
			maximum(From.y, To.y) < MinY || minimum(From.y, To.y) > MaxY)
			continue;

		if(SegmentIntersectsTriangle(From, To, aPoints[0], aPoints[1], aPoints[2]) ||
			SegmentIntersectsTriangle(From, To, aPoints[1], aPoints[3], aPoints[2]))
		{
			pCharacter->Freeze();
			return;
		}
	}
}

void CMovingTiles::OnRender()
{
	if(g_Config.m_ClOverlayEntities != 100)
		return;
	if(!g_Config.m_TcMovingTilesEntities)
		return;
	if(m_vQuads.empty())
		return;

	const vec2 Center = GameClient()->m_Camera.m_Center;
	const float Zoom = GameClient()->m_Camera.m_Zoom;

	auto ApplyGroupState = [&](const CMapItemGroup *pGroup) -> bool {
		Graphics()->ClipDisable();

		if(!pGroup)
			return false;

		if(!g_Config.m_GfxNoclip && pGroup->m_Version >= 2 && pGroup->m_UseClipping)
		{
			Graphics()->MapScreenToInterface(Center.x, Center.y, Zoom);

			float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
			const CScreenRect ScreenRect = Graphics()->GetScreen();
			ScreenX0 = ScreenRect.m_TopLeft.x;
			ScreenY0 = ScreenRect.m_TopLeft.y;
			ScreenX1 = ScreenRect.m_BottomRight.x;
			ScreenY1 = ScreenRect.m_BottomRight.y;

			const float ScreenWidth = ScreenX1 - ScreenX0;
			const float ScreenHeight = ScreenY1 - ScreenY0;
			const float Left = pGroup->m_ClipX - ScreenX0;
			const float Top = pGroup->m_ClipY - ScreenY0;
			const float Right = pGroup->m_ClipX + pGroup->m_ClipW - ScreenX0;
			const float Bottom = pGroup->m_ClipY + pGroup->m_ClipH - ScreenY0;

			if(Right < 0.0f || Left > ScreenWidth || Bottom < 0.0f || Top > ScreenHeight)
				return false;

			const int ClipX = (int)std::round(Left * Graphics()->ScreenWidth() / ScreenWidth);
			const int ClipY = (int)std::round(Top * Graphics()->ScreenHeight() / ScreenHeight);

			Graphics()->ClipEnable(
				ClipX,
				ClipY,
				(int)std::round(Right * Graphics()->ScreenWidth() / ScreenWidth) - ClipX,
				(int)std::round(Bottom * Graphics()->ScreenHeight() / ScreenHeight) - ClipY);
		}

		const int ParallaxZoom = std::clamp(maximum(pGroup->m_ParallaxX, pGroup->m_ParallaxY), 0, 100);
		CScreenRect ScreenRect = Graphics()->MapScreenToWorld(
			Center.x, Center.y,
			pGroup->m_ParallaxX, pGroup->m_ParallaxY, (float)ParallaxZoom,
			pGroup->m_OffsetX, pGroup->m_OffsetY,
			Graphics()->ScreenAspect(), Zoom);
		Graphics()->MapScreen(ScreenRect);

		return true;
	};

	auto RenderPass = [&](int RenderFlags) {
		constexpr float ColorConv = 1.0f / 255.0f;

		size_t QuadStart = 0;
		while(QuadStart < m_vQuads.size())
		{
			const CMapItemGroup *pGroup = m_vQuads[QuadStart].m_pGroup;
			const CMapItemLayerQuads *pLayer = m_vQuads[QuadStart].m_pLayer;
			if(!pLayer)
			{
				QuadStart++;
				continue;
			}

			size_t QuadEnd = QuadStart + 1;
			while(QuadEnd < m_vQuads.size() &&
				m_vQuads[QuadEnd].m_pGroup == pGroup &&
				m_vQuads[QuadEnd].m_pLayer == pLayer)
			{
				QuadEnd++;
			}

			if(!ApplyGroupState(pGroup))
			{
				QuadStart = QuadEnd;
				continue;
			}

			if(pLayer->m_Image >= 0 && pLayer->m_Image < GameClient()->m_MapImages.Num())
				Graphics()->TextureSet(GameClient()->m_MapImages.Get(pLayer->m_Image));
			else
				Graphics()->TextureClear();

			Graphics()->TrianglesBegin();

			for(size_t QuadIndex = QuadStart; QuadIndex < QuadEnd; QuadIndex++)
			{
				const CQuadData &QuadData = m_vQuads[QuadIndex];
				const CQuad *pQuad = QuadData.m_pQuad;
				if(!pQuad)
					continue;

				ColorRGBA Color(1.0f, 1.0f, 1.0f, 1.0f);
				m_EnvEvaluator.EnvelopeEval(pQuad->m_ColorEnvOffset, pQuad->m_ColorEnv, Color, 4);
				if(Color.a <= 0.0f)
					continue;

				bool Opaque = false;
				if(Opaque && !(RenderFlags & LAYERRENDERFLAG_OPAQUE))
					continue;
				if(!Opaque && !(RenderFlags & LAYERRENDERFLAG_TRANSPARENT))
					continue;

				Graphics()->QuadsSetSubsetFree(
					fx2f(pQuad->m_aTexcoords[0].x), fx2f(pQuad->m_aTexcoords[0].y),
					fx2f(pQuad->m_aTexcoords[1].x), fx2f(pQuad->m_aTexcoords[1].y),
					fx2f(pQuad->m_aTexcoords[2].x), fx2f(pQuad->m_aTexcoords[2].y),
					fx2f(pQuad->m_aTexcoords[3].x), fx2f(pQuad->m_aTexcoords[3].y));

				ColorRGBA Position(0.0f, 0.0f, 0.0f, 0.0f);
				m_EnvEvaluator.EnvelopeEval(pQuad->m_PosEnvOffset, pQuad->m_PosEnv, Position, 3);

				const vec2 Offset(Position.r, Position.g);
				const float Rotation = Position.b / 180.0f * pi + QuadData.m_Angle;

				IGraphics::CColorVertex aColors[4] = {
					IGraphics::CColorVertex(0, pQuad->m_aColors[0].r * ColorConv * Color.r, pQuad->m_aColors[0].g * ColorConv * Color.g, pQuad->m_aColors[0].b * ColorConv * Color.b, pQuad->m_aColors[0].a * ColorConv * Color.a),
					IGraphics::CColorVertex(1, pQuad->m_aColors[1].r * ColorConv * Color.r, pQuad->m_aColors[1].g * ColorConv * Color.g, pQuad->m_aColors[1].b * ColorConv * Color.b, pQuad->m_aColors[1].a * ColorConv * Color.a),
					IGraphics::CColorVertex(2, pQuad->m_aColors[2].r * ColorConv * Color.r, pQuad->m_aColors[2].g * ColorConv * Color.g, pQuad->m_aColors[2].b * ColorConv * Color.b, pQuad->m_aColors[2].a * ColorConv * Color.a),
					IGraphics::CColorVertex(3, pQuad->m_aColors[3].r * ColorConv * Color.r, pQuad->m_aColors[3].g * ColorConv * Color.g, pQuad->m_aColors[3].b * ColorConv * Color.b, pQuad->m_aColors[3].a * ColorConv * Color.a)};
				Graphics()->SetColorVertex(aColors, std::size(aColors));

				vec2 aPoints[4] = {
					QuadData.m_Pos[0],
					QuadData.m_Pos[1],
					QuadData.m_Pos[2],
					QuadData.m_Pos[3],
				};

				if(Rotation != 0.0f)
				{
					for(vec2 &Point : aPoints)
						RotatePoint(QuadData.m_Pos[4], Point, Rotation);
				}

				const IGraphics::CFreeformItem Freeform(
					aPoints[0] + Offset,
					aPoints[1] + Offset,
					aPoints[2] + Offset,
					aPoints[3] + Offset);
				Graphics()->QuadsDrawFreeform(&Freeform, 1);
			}

			Graphics()->TrianglesEnd();
			QuadStart = QuadEnd;
		}
	};

	Graphics()->BlendNone();
	RenderPass(LAYERRENDERFLAG_OPAQUE);

	Graphics()->BlendNormal();
	RenderPass(LAYERRENDERFLAG_TRANSPARENT);

	Graphics()->ClipDisable();
}
