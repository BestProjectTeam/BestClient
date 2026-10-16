#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_OUTLINES_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_OUTLINES_H

#include <game/client/component.h>
#include <game/map/entity_outline.h>
#include <game/map/entity_regions.h>
#include <game/map/rounded_tiles.h>

#include <cstdint>
#include <unordered_map>

class CTile;
class CTeleTile;

class COutlines : public CComponent
{
private:
	ivec2 m_MapDataSize;
	int *m_pMapData = nullptr;
	CEntityRegions m_Regions;
	CEntityOutlineIndex m_OutlineIndex;
	void RenderRoundedOutlines(int StartX, int StartY, int EndX, int EndY);
	std::unordered_map<uint64_t, std::pair<int, int>> m_RoundedContainers;
	int m_RoundingPercent = -1, m_RoundingMode = -1, m_RoundingSteps = 2;
	void ClearRoundedCache();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnMapLoad() override;
	void OnRender() override;
	void OnShutdown() override;
};

#endif
