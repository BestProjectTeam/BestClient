/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_HOOKGRIP_H
#define GAME_CLIENT_COMPONENTS_HOOKGRIP_H
#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <functional>

#include <base/color.h>

class CGameClient;

ColorRGBA FgfThemeColor();

enum
{
	FGF_HOOK_STYLE_OFF = 0,
	FGF_HOOK_STYLE_WHIRLWIND,
	FGF_HOOK_STYLE_BLACK_HOLE,
	FGF_HOOK_STYLE_MAGIC,
	FGF_HOOK_STYLE_FIRE,
	FGF_HOOK_STYLE_TROLL,
	FGF_HOOK_STYLE_RAINBOW,
	FGF_HOOK_STYLE_LIGHTNING,
	FGF_HOOK_STYLE_TENTACLE,
};

ColorRGBA FgfRainbow(float Hue, float Lightness = 0.62f);

class CHookGripTracker
{
public:
	struct CGrip
	{
		bool m_Holding = false;
		bool m_Released = true;
		bool m_FullyCharged = false;
		vec2 m_Pos = vec2(0.0f, 0.0f);
		vec2 m_Dir = vec2(0.0f, 1.0f);
		vec2 m_TeePos = vec2(0.0f, 0.0f);
		float m_TeeDistance = 1000.0f;
		float m_Charge = 0.0f;
		float m_HoldTime = 0.0f;
		float m_Fade = 0.0f;
		float m_ReleaseTime = 0.0f;
		float m_Alpha = 1.0f;
	};

	enum class EEvent
	{
		GRAB,
		CHARGED,
		RELEASE,
	};

	using FEvent = std::function<void(int ClientId, EEvent Event, const CGrip &Grip)>;

	void Reset();
	void Update(CGameClient *pGameClient, float Passed, float ChargeTime, float FadeTime, const FEvent &OnEvent);

	const CGrip &Grip(int ClientId) const { return m_aGrips[ClientId]; }

private:
	CGrip m_aGrips[MAX_CLIENTS];
};
#endif
