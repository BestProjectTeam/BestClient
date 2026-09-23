/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_OPTIMIZER_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_OPTIMIZER_H

#include <base/math.h>
#include <base/vmath.h>

#include <engine/console.h>

#include <game/client/component.h>

class COptimizer : public CComponent
{
	int Sizeof() const override { return sizeof(*this); }

	bool m_Enabled = false;
	bool m_DisableParticles = false;
	bool m_DisableQuads = false;
	bool m_FpsFog = false;
	bool m_CullMapTiles = false;
	bool m_WasDisableParticles = false;
	bool m_RestoreHighDetailOnRelease = false;
	vec2 m_Center = vec2(0.0f, 0.0f);
	float m_HalfW = 0.0f;
	float m_HalfH = 0.0f;

	void ComputeHalfExtents(float &HalfW, float &HalfH) const;
	void SyncHighDetailFromOptimizer();

	static void ConchainOptimizer(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainDisableHighDetail(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainGfxHighDetail(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	void OnConsoleInit() override;

public:
	void RefreshFrame();
	void RenderFpsFogRect();

	void OnOptimizerDisableHighDetailToggled();
	void OnGfxHighDetailToggled();

	bool DisableParticles() const { return m_DisableParticles; }
	bool DisableQuads() const { return m_DisableQuads; }
	bool FpsFogEnabled() const { return m_FpsFog; }
	bool CullMapTiles() const { return m_CullMapTiles; }
	float FogHalfW() const { return m_HalfW; }
	float FogHalfH() const { return m_HalfH; }

	void FpsFogHalfExtents(float &HalfW, float &HalfH) const
	{
		HalfW = m_HalfW;
		HalfH = m_HalfH;
	}

	bool AllowRenderPos(vec2 WorldPos) const
	{
		if(!m_FpsFog)
			return true;
		return absolute(WorldPos.x - m_Center.x) <= m_HalfW && absolute(WorldPos.y - m_Center.y) <= m_HalfH;
	}
};

#endif
