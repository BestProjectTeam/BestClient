/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_HOOKBLACKHOLE_H
#define GAME_CLIENT_COMPONENTS_HOOKBLACKHOLE_H
#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/shared/protocol.h>

#include <game/client/component.h>
#include "fgf_glow.h"
#include "hookgrip.h"

class CHookBlackHole : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnRender() override;

private:
	enum
	{
		DISK_SAMPLES = 48,
		ARC_POINTS = 24,
	};

	struct CPalette
	{
		ColorRGBA m_Hot;
		ColorRGBA m_Deep;
		ColorRGBA m_Core;
	};

	struct CHoleState
	{
		float m_Spin = 1.0f;
		float m_DiskAngle = 0.0f;
		float m_InfallAccumulator = 0.0f;
		float m_PullAccumulator = 0.0f;
	};

	IGraphics::CTextureHandle m_GlowTexture;
	IGraphics::CTextureHandle m_RingTexture;
	IGraphics::CTextureHandle m_DiscTexture;
	IGraphics::CTextureHandle m_BeamTexture;

	CHookGripTracker m_Tracker;
	CHoleState m_aStates[MAX_CLIENTS];
	CGlowParticles m_Matter;
	CGlowParticles m_Light;
	float m_LastTime = 0.0f;

	static CPalette Palette();
	static float HorizonRadius(const CHookGripTracker::CGrip &Grip, float Scale);

	void Form(const CHookGripTracker::CGrip &Grip, const CPalette &Palette, float Scale);
	void Flare(const CHookGripTracker::CGrip &Grip, const CPalette &Palette, float Scale);
	void Evaporate(int Owner, const CHookGripTracker::CGrip &Grip, const CPalette &Palette, float Scale);
	void EmitInfall(int Owner, const CHookGripTracker::CGrip &Grip, const CPalette &Palette, float Passed, float Scale);

	void DrawDisk(const CHookGripTracker::CGrip &Grip, const CHoleState &State, const CPalette &Palette, float Scale, float Time, bool FrontHalf);
	void DrawLensedArc(vec2 Center, float Radius, float StartAngle, float Width, ColorRGBA Color);
	void Draw(const CPalette &Palette, float Scale, float Time);
};
#endif
