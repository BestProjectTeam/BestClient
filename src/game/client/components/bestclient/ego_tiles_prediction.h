/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_EGO_TILES_PREDICTION_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_EGO_TILES_PREDICTION_H

#include <base/vmath.h>

#include <game/client/component.h>

#include <vector>

class CCharacter;
class CQuad;
class IMap;

class CEgoTilesPrediction : public CComponent
{
public:
	struct CPredQuad
	{
		CQuad *m_pQuad = nullptr;
		int m_TileIndex = 0;
		bool m_Animated = false;
		vec2 m_aLocalPoints[5] = {};
		vec2 m_aPoints[5] = {};
		vec2 m_AabbMin = vec2(0, 0);
		vec2 m_AabbMax = vec2(0, 0);

		void UpdateAabb();
		bool AabbContains(vec2 Pos) const;
	};

	int Sizeof() const override { return sizeof(*this); }

	void OnMapLoad() override;
	void OnStateChange(int NewState, int OldState) override;

	void Reset();

	void UpdateForTick(int GameTick, int RoundStartTick, int TickSpeed);
	void HandleCharacter(CCharacter *pCharacter) const;

private:
	std::vector<CPredQuad> m_vQuads;
	std::vector<int> m_vAnimatedQuadIndices;
	IMap *m_pMap = nullptr;
	int m_LastUpdatedTick = -1;

	void LoadQuads();
	void GetAnimationTransform(float GlobalTime, int Env, vec2 &Position, float &Angle) const;
	static void Rotate(vec2 Center, vec2 *pPoint, float Rotation);
	static bool InsideQuad(vec2 Pos, vec2 T0, vec2 T1, vec2 T2, vec2 T3);
	static int QuadNameToTile(int QuadType);
};

#endif
