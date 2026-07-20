/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_GRADIENT_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_GRADIENT_H

#include <game/client/component.h>

#include <engine/shared/protocol.h>
#include <engine/textrender.h>

#include <vector>

class CGameClient;

enum
{
	BC_GRADIENT_MODE_SKIN = 0,
	BC_GRADIENT_MODE_CUSTOM = 1,
	BC_GRADIENT_MODE_RAINBOW = 2,
};

class CBcGradient : public CComponent
{
	ColorRGBA m_aOriginalBody[MAX_CLIENTS];
	ColorRGBA m_aOriginalFeet[MAX_CLIENTS];
	bool m_aHasOriginal[MAX_CLIENTS] = {};

	int m_LastEverything = -1;
	int m_LastNick = -1;
	int m_LastClan = -1;
	int m_LastMode = -1;
	int m_LastColorCount = -1;
	int m_LastAnimateSpeed = -1;
	unsigned m_LastColor1 = 0;
	unsigned m_LastColor2 = 0;
	unsigned m_LastColor3 = 0;
	unsigned m_LastColor4 = 0;

	void RefreshCachedText();

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnRender() override;

	static float AnimatePhase(double GlobalTime);
	static ColorRGBA SampleRainbow(float Hue);
	static ColorRGBA SampleCustomGradient(float Position);
	static void GetSkinToneColors(int ClientId, const CGameClient *pGameClient, ColorRGBA &Body, ColorRGBA &Feet);
	static void GetAnimatedEndpointColors(int ClientId, const CGameClient *pGameClient, float Phase, ColorRGBA &Color1, ColorRGBA &Color2);
	static std::vector<STextColorSplit> BuildStaticColorSplits(const char *pText, const ColorRGBA &Color1, const ColorRGBA &Color2);
	static std::vector<STextColorSplit> BuildAnimatedColorSplits(const char *pText, const ColorRGBA &Color1, const ColorRGBA &Color2, float Phase);
	static std::vector<STextColorSplit> BuildAnimatedTextSplits(const char *pText, int ClientId, CGameClient *pGameClient, float Phase);
	static void ApplyEverythingGradient(CTextCursor *pCursor, const char *pText, int Length, CGameClient *pGameClient);
};

#endif
