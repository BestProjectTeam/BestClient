#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_STATUSBAR_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_STATUSBAR_H

#include <game/client/component.h>

enum
{
	STATUSBAR_MAX_SIZE = 128,
	STATUSBAR_TYPE_LETTERS = 4
};

class CStatusItem
{
public:
	std::function<void()> m_RenderItem;
	std::function<float()> m_GetWidth;
	char m_aName[32];
	char m_aDisplayName[32];
	char m_aDesc[128];
	char m_aLetters[STATUSBAR_TYPE_LETTERS] = {};
	bool m_ShowLabel = true;
	CStatusItem(std::function<void()> Render, std::function<float()> Width, const char *pLetters, const char *pName, const char *pDisplayName, const char *pDesc, bool ShowLabel = true);
};

class CStatusBar : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	void OnInit() override;

	CStatusItem m_Angle = CStatusItem([this] { AngleRender(); }, [this] { return AngleWidth(); },
		"a", "Angle", "", "Displays your current angle in degrees");
	CStatusItem m_Ping = CStatusItem([this] { PingRender(); }, [this] { return PingWidth(); },
		"p", "Ping", "", "Displays your ping to the current server");
	CStatusItem m_Prediction = CStatusItem([this] { PredictionRender(); }, [this] { return PredictionWidth(); },
		"d", "Prediction", "Pred", "Displays your current prediction amount");
	CStatusItem m_Position = CStatusItem([this] { PositionRender(); }, [this] { return PositionWidth(); },
		"c", "Position", "Pos", "Displays position");
	CStatusItem m_LocalTime = CStatusItem([this] { LocalTimeRender(); }, [this] { return LocalTimeWidth(); },
		"l", "Local Time", "", "Displays your local time", false);
	CStatusItem m_RaceTime = CStatusItem([this] { RaceTimeRender(); }, [this] { return RaceTimeWidth(); },
		"r", "Race Time", "", "Display your race time", false);
	CStatusItem m_FPS = CStatusItem([this] { FPSRender(); }, [this] { return FPSWidth(); },
		"f", "FPS", "", "Displays your frames per second");
	CStatusItem m_Velocity = CStatusItem([this] { VelocityRender(); }, [this] { return VelocityWidth(); },
		"v", "Velocity", "", "Displays X and Y velocity");
	CStatusItem m_Zoom = CStatusItem([this] { ZoomRender(); }, [this] { return ZoomWidth(); },
		"z", "Zoom", "", "Displays current zoom value");
	CStatusItem m_Space = CStatusItem([this] { SpaceRender(); }, [this] { return SpaceWidth(); },
		" _", "Space", " ", "Gap between statusbar items", false);

	std::vector<CStatusItem> m_StatusItemTypes = {m_Angle, m_Ping, m_Prediction, m_Position, m_LocalTime, m_RaceTime, m_FPS, m_Velocity, m_Zoom, m_Space};
	std::vector<CStatusItem *> m_StatusBarItems = {&m_LocalTime, &m_FPS, &m_Space, &m_Angle, &m_Space, &m_Ping};

	void UpdateStatusBarSize();
	void ApplyStatusBarScheme(const char *pScheme);
	void UpdateStatusBarScheme(char *pScheme);

	bool m_PingActive = false;

private:
	float m_FrameTimeAverage = 0.0f;
	int m_PlayerId = 0;
	float m_FontSize = 12.0f;
	float m_CursorX, m_CursorY, m_BarX = 0.0f, m_BarY;
	float m_Width, m_Height;
	float m_BarHeight, m_Margin;

	int m_CurrentRaceTime = 0;
	float GetDurationWidth(int Duration);
	int GetDigitsIndex(int Value, int Max);
	float AngleWidth();
	void AngleRender();

	float PingWidth();
	void PingRender();

	float PredictionWidth();
	void PredictionRender();

	float PositionWidth();
	void PositionRender();

	float LocalTimeWidth();
	void LocalTimeRender();

	float RaceTimeWidth();
	void RaceTimeRender();

	float FPSWidth();
	void FPSRender();

	float VelocityWidth();
	void VelocityRender();

	float ZoomWidth();
	void ZoomRender();

	float SpaceWidth();
	void SpaceRender();

	void LabelRender(const char *pLabel);
	float LabelWidth(const char *pLabel);
};

#endif
