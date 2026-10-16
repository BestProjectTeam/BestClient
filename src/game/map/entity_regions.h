#ifndef GAME_MAP_ENTITY_REGIONS_H
#define GAME_MAP_ENTITY_REGIONS_H

#include <engine/map.h>

#include <game/layers.h>
#include <game/mapitems.h>

#include <algorithm>
#include <vector>

// Shared category/priority map for entity geometry and outlines.
class CEntityRegions
{
public:
	enum
	{
		NONE,
		UNFREEZE,
		FREEZE,
		TELE,
		KILL,
		SOLID,
		SWITCH
	};
	int m_Width = 0, m_Height = 0;
	std::vector<int> m_vTypes;
	std::vector<int> m_vRegionKeys;
	std::vector<unsigned char> m_vTopLayers;
	static bool IsThrough(int Index)
	{
		return Index == TILE_THROUGH || Index == TILE_THROUGH_CUT || Index == TILE_THROUGH_ALL || Index == TILE_THROUGH_DIR;
	}

	static int Category(int Index)
	{
		if(Index == TILE_SOLID || Index == TILE_NOHOOK)
			return SOLID;
		if(Index == TILE_FREEZE || Index == TILE_DFREEZE || Index == TILE_LFREEZE)
			return FREEZE;
		if(Index == TILE_UNFREEZE || Index == TILE_DUNFREEZE || Index == TILE_LUNFREEZE)
			return UNFREEZE;
		return Index == TILE_DEATH ? KILL : NONE;
	}

	static int VisibleType(int GameIndex, int FrontIndex, int TeleType, int SwitchType = 0)
	{
		// Entity layers are rendered game -> front -> tele -> switch. A later
		// tile owns the visible silhouette even when it has no outline of its
		// own; otherwise a covered unfreeze tile leaves corner fragments.
		int Type = Category(GameIndex);
		if(const int Front = Category(FrontIndex); Front != NONE)
			Type = Front;
		if(TeleType != 0)
			Type = TELE;
		if(SwitchType != 0)
			Type = SWITCH;
		return Type;
	}

	static int VisibleRegionKey(int GameIndex, int FrontIndex, int TeleType, int SwitchType = 0)
	{
		// A later entity layer can cover only some cells of an earlier one.
		// Keep that seam out of a single region: an inner junction spanning it
		// otherwise exposes a small piece of the buried asset at its corner.
		int Type = Category(GameIndex);
		int Underlay = NONE;
		int AssetType = 0;
		if(const int Front = Category(FrontIndex); Front != NONE)
		{
			Underlay = Type;
			Type = Front;
		}
		if(TeleType != 0)
		{
			Underlay = Type;
			Type = TELE;
			AssetType = TeleType;
		}
		if(SwitchType != 0)
		{
			Underlay = Type;
			Type = SWITCH;
			AssetType = SwitchType;
		}
		// Adjacent tele/switch types can have different tile artwork. Their
		// shared edge stays straight, but cannot seed an inner arc for either.
		return Type * 2048 + AssetType * 8 + Underlay;
	}

	int Get(int X, int Y) const
	{
		if(m_vTypes.empty())
			return NONE;
		return m_vTypes[(size_t)std::clamp(Y, 0, m_Height - 1) * m_Width + std::clamp(X, 0, m_Width - 1)];
	}

	int RegionKey(int X, int Y) const
	{
		if(m_vRegionKeys.empty())
			return Get(X, Y);
		return m_vRegionKeys[(size_t)std::clamp(Y, 0, m_Height - 1) * m_Width + std::clamp(X, 0, m_Width - 1)];
	}

	int TopLayer(int X, int Y) const
	{
		if(m_vTopLayers.empty())
			return 0;
		return m_vTopLayers[(size_t)std::clamp(Y, 0, m_Height - 1) * m_Width + std::clamp(X, 0, m_Width - 1)];
	}

	bool ShouldRoundTile(int X, int Y, int Layer, int Index) const
	{
		if(Index == 0)
			return false;
		const int Type = Get(X, Y);
		// A front-layer through tile is translucent artwork laid over the game
		// tile. Both layers must use the same silhouette, otherwise the square
		// front sprite crosses the curved solid outline (or exposes a wedge).
		if(Layer == 1 && TopLayer(X, Y) == 0 && Type != NONE && IsThrough(Index))
			return true;
		if(Layer != TopLayer(X, Y))
			return false;
		if(Layer == 2)
			return Type == TELE;
		if(Layer == 3)
			return Type == SWITCH;
		return Type != NONE && Category(Index) == Type;
	}

	// A three-against-one material junction has one shared circular boundary:
	// the lone tile loses its convex corner and the other material fills it.
	// Limit this to different visible categories. Two teleporter assets, or a
	// change in buried underlay, must keep their existing straight seam.
	// Low four bits: the lone convex or diagonal concave tile. High four
	// bits: the two side tiles whose straight contour ends at the arc tangent.
	unsigned TransitionCorners(int X, int Y) const
	{
		const int Here = RegionKey(X, Y), Type = Get(X, Y);
		if(Type == NONE)
			return 0;
		unsigned Result = 0;
		constexpr int SX[] = {-1, 1, 1, -1};
		constexpr int SY[] = {-1, -1, 1, 1};
		for(int Corner = 0; Corner < 4; ++Corner)
		{
			const int H = RegionKey(X + SX[Corner], Y);
			const int V = RegionKey(X, Y + SY[Corner]);
			const int D = RegionKey(X + SX[Corner], Y + SY[Corner]);
			const bool Inner = H == Here && V == Here && D != Here && Get(X + SX[Corner], Y + SY[Corner]) != Type;
			const bool Outer = H == V && V == D && H != Here && Get(X + SX[Corner], Y) != NONE && Get(X + SX[Corner], Y) != Type;
			const bool Trim = D == Here && ((H == Here && V != Here && Get(X, Y + SY[Corner]) != Type) ||
							       (V == Here && H != Here && Get(X + SX[Corner], Y) != Type));
			if(Inner || Outer)
				Result |= 1u << Corner;
			if(Trim)
				Result |= 1u << (Corner + 4);
		}
		return Result;
	}

	unsigned ComplementCorners(int X, int Y) const { return TransitionCorners(X, Y) & 15u; }

	unsigned Mask(int X, int Y, int Type) const
	{
		unsigned Result = 0, Bit = 0;
		for(int DY = -1; DY <= 1; ++DY)
			for(int DX = -1; DX <= 1; ++DX)
				if(DX || DY)
				{
					// A different outlined category has its own silhouette. Treating it
					// as filled creates a straight edge on one asset and a rounded edge
					// on the other (and leaves small wedges at their junction).
					if(RegionKey(X + DX, Y + DY) == RegionKey(X, Y))
						Result |= 1u << Bit;
					++Bit;
				}
		return Result;
	}

	unsigned PriorityMask(int X, int Y, int Type) const
	{
		unsigned Result = 0, Bit = 0;
		for(int DY = -1; DY <= 1; ++DY)
			for(int DX = -1; DX <= 1; ++DX)
				if(DX || DY)
				{
					if(Get(X + DX, Y + DY) >= Type)
						Result |= 1u << Bit;
					++Bit;
				}
		return Result;
	}

	unsigned BlockerMask(int X, int Y, int Type) const
	{
		unsigned Result = 0, Bit = 0;
		for(int DY = -1; DY <= 1; ++DY)
			for(int DX = -1; DX <= 1; ++DX)
				if(DX || DY)
				{
					const int Neighbor = Get(X + DX, Y + DY);
					if(Neighbor != NONE && RegionKey(X + DX, Y + DY) != RegionKey(X, Y))
						Result |= 1u << Bit;
					++Bit;
				}
		return Result;
	}

	void Load(CLayers *pLayers)
	{
		m_Width = m_Height = 0;
		m_vTypes.clear();
		m_vRegionKeys.clear();
		m_vTopLayers.clear();
		struct CLayer
		{
			CMapItemLayerTilemap *m_pLayer;
			int m_Data;
			bool m_Tele;
			bool m_Switch;
		};
		const auto *pTele = pLayers->TeleLayer();
		const auto *pGame = pLayers->GameLayer();
		const auto *pFront = pLayers->FrontLayer();
		const auto *pSwitch = pLayers->SwitchLayer();
		// Follow the visible entity rendering order. Later layers cover earlier
		// ones, so their category must own the rounded shape and outline.
		CLayer Layers[] = {{pLayers->GameLayer(), pGame ? pGame->m_Data : -1, false, false},
			{pLayers->FrontLayer(), pFront ? pFront->m_Front : -1, false, false},
			{pLayers->TeleLayer(), pTele ? pTele->m_Tele : -1, true, false},
			{pLayers->SwitchLayer(), pSwitch ? pSwitch->m_Switch : -1, false, true}};
		for(auto &L : Layers)
		{
			if(!L.m_pLayer)
				continue;
			const int W = L.m_pLayer->m_Width, H = L.m_pLayer->m_Height;
			const size_t TileSize = L.m_Tele ? sizeof(CTeleTile) : (L.m_Switch ? sizeof(CSwitchTile) : sizeof(CTile));
			const int Bytes = pLayers->Map()->GetDataSize(L.m_Data);
			if(W <= 0 || H <= 0 || Bytes <= 0 || (uint64_t)W * H * TileSize > (uint64_t)Bytes)
			{
				L.m_pLayer = nullptr;
				continue;
			}
			m_Width = std::max(m_Width, W);
			m_Height = std::max(m_Height, H);
		}
		m_vTypes.resize((size_t)m_Width * m_Height, NONE);
		m_vRegionKeys.resize((size_t)m_Width * m_Height, NONE);
		m_vTopLayers.resize((size_t)m_Width * m_Height, 0);
		const void *pData[] = {
			Layers[0].m_pLayer ? pLayers->Map()->GetData(Layers[0].m_Data) : nullptr,
			Layers[1].m_pLayer ? pLayers->Map()->GetData(Layers[1].m_Data) : nullptr,
			Layers[2].m_pLayer ? pLayers->Map()->GetData(Layers[2].m_Data) : nullptr,
			Layers[3].m_pLayer ? pLayers->Map()->GetData(Layers[3].m_Data) : nullptr};
		auto TileIndex = [&](int Layer, int X, int Y) {
			const auto &L = Layers[Layer];
			if(!pData[Layer] || !L.m_pLayer || X >= L.m_pLayer->m_Width || Y >= L.m_pLayer->m_Height)
				return 0;
			const size_t I = (size_t)Y * L.m_pLayer->m_Width + X;
			return L.m_Tele ? int(static_cast<const CTeleTile *>(pData[Layer])[I].m_Type) : (L.m_Switch ? int(static_cast<const CSwitchTile *>(pData[Layer])[I].m_Type) : int(static_cast<const CTile *>(pData[Layer])[I].m_Index));
		};
		for(int Y = 0; Y < m_Height; ++Y)
			for(int X = 0; X < m_Width; ++X)
			{
				const int Game = TileIndex(0, X, Y), Front = TileIndex(1, X, Y), Tele = TileIndex(2, X, Y), Switch = TileIndex(3, X, Y);
				const size_t I = (size_t)Y * m_Width + X;
				m_vTypes[I] = VisibleType(Game, Front, Tele, Switch);
				m_vRegionKeys[I] = VisibleRegionKey(Game, Front, Tele, Switch);
				m_vTopLayers[I] = Switch ? 3 : Tele            ? 2 :
						       Category(Front) != NONE ? 1 :
										 0;
			}
	}
};

#endif
