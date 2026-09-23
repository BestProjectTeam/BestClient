/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_SERVER_MAP_PREVIEW_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_SERVER_MAP_PREVIEW_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/map.h>
#include <engine/serverbrowser.h>

#include <game/client/component.h>
#include <game/client/ui.h>

#include <memory>

class CEnvelopeState;
class CLayers;
class IHttpRequest;
struct SMapDatabase;
struct SPreviewCache;
struct SPreviewSlot;

class CServerMapPreview : public CComponentInterfaces
{
public:
	static constexpr float HEIGHT = 100.0f * 4.0f / 3.0f;

	CServerMapPreview();
	~CServerMapPreview() override;

	void OnInterfacesInit(CGameClient *pClient) override;
	void Clear();
	void Tick();
	float RowHeight() const;
	void Collapse();
	void CancelScheduledOpen();
	void SwitchTo(const char *pAddress);
	bool ShowsAddress(const char *pAddress) const;
	bool OpenPending() const { return m_OpenAt >= 0.0f || m_aPendingAddress[0] != '\0'; }
	bool TakeReveal();
	bool Expanded() const { return m_Expanded; }
	bool ReadyToLoad() const { return m_LoadArmed; }
	bool HasMap(const CServerInfo *pInfo) const;
	void MarkUnavailable(const CServerInfo *pInfo);
	void SetServer(const CServerInfo *pInfo);
	void Render(CUIRect Rect, ColorRGBA FrameColor);

private:
	enum class EState
	{
		EMPTY,
		DOWNLOADING,
		READY,
		FAILED,
	};

	std::unique_ptr<SPreviewSlot> m_pSlot;
	std::unique_ptr<SPreviewCache> m_pCache;
	std::shared_ptr<IHttpRequest> m_pDownload;
	std::shared_ptr<IHttpRequest> m_pMapInfoRequest;
	std::unique_ptr<SMapDatabase> m_pMapDatabase;

	EState m_State = EState::EMPTY;
	bool m_Expanded = false;
	float m_OpenAt = -1.0f;
	bool m_Reveal = false;
	float m_PreviewHeight = HEIGHT;
	char m_aShownAddress[MAX_SERVER_ADDRESSES * NETADDR_MAXSTRSIZE]{};
	char m_aPendingAddress[MAX_SERVER_ADDRESSES * NETADDR_MAXSTRSIZE]{};
	float m_Expand = 0.0f;
	float m_LastExpandTime = 0.0f;
	bool m_HasExpandTime = false;
	bool m_SettledFrame = false;
	bool m_LoadArmed = false;
	bool m_MapInfoStarted = false;
	char m_aKey[MAX_MAP_LENGTH + SHA256_MAXSTRSIZE]{};
	char m_aMapName[MAX_MAP_LENGTH]{};
	char m_aMapPath[IO_MAX_PATH_LENGTH]{};
	vec2 m_Camera = vec2(0.0f, 0.0f);
	float m_Zoom = 2.0f;
	float m_MapWidth = 0.0f;
	float m_MapHeight = 0.0f;

	void Open();
	void ScheduleOpen();
	void UnloadMap();
	void ParkSlot();
	bool TakeCached(const char *pKey);
	void AbortDownload();
	void PollDownload();
	void ResetCamera();
	void ClampCamera(float Aspect);
	void HandleInput(const CUIRect &MapRect, const CUIRect &WheelRect, const CUIRect &Ignore);
	void RenderMap(const CUIRect &Rect);
	void RenderDifficulty(const CUIRect &Rect);
	void EnsureMapDatabase();
	void PollMapDatabase();
	void ParseMapDatabase(const char *pJson, unsigned Length);
	bool DifficultyText(char *pText, int TextSize, ColorRGBA *pColor) const;
	bool FindLocalMap(const CServerInfo *pInfo);
	bool StartDownload(const CServerInfo *pInfo);
	bool LoadMap();
	bool BuildUrl(char *pUrl, int UrlSize, const CServerInfo *pInfo) const;
	const char *StatusText() const;
	static void FormatKey(char *pKey, int KeySize, const CServerInfo *pInfo);
	bool VisibleSlice(const CUIRect &Rect, CUIRect *pVisible) const;
};

#endif
