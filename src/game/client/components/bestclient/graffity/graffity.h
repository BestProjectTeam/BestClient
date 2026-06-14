/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_GRAFFITY_GRAFFITY_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_GRAFFITY_GRAFFITY_H

#include <base/color.h>
#include <base/net.h>

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/sound.h>
#include <engine/shared/json.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class CGraffity : public CComponent
{
public:
	CGraffity();
	~CGraffity() override;

	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnInit() override;
	void OnReset() override;
	void OnRelease() override;
	void OnShutdown() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnUpdate() override;
	void OnRender() override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	bool OnInput(const IInput::CEvent &Event) override;
	void RenderOverlayWorld();
	void RenderOverlayUi();

	bool IsWheelActive() const { return m_WheelActive; }
	bool IsPlacementActive() const { return m_PlacementActive; }

private:
	struct SGraffityDef
	{
		const char *m_pId = "";
		const char *m_pPath = "";
		IGraphics::CTextureHandle m_Texture;
	};

	struct SPlacedGraffity
	{
		std::string m_Id;
		std::string m_GraffityId;
		vec2 m_Pos = vec2(0.0f, 0.0f);
		int m_Size = 1;
		bool m_Air = false;
		bool m_Owned = false;
		int64_t m_AppearTick = 0;
	};

	enum class EPlacementError
	{
		NONE = 0,
		OFFLINE,
		OUT_OF_BOUNDS,
		MIXED_SURFACE,
		OVERLAP,
		NO_SERVER,
	};

	struct SPlacementPreview
	{
		bool m_Valid = false;
		vec2 m_Pos = vec2(0.0f, 0.0f);
		int m_Size = 1;
		bool m_Air = false;
		int m_TileX = 0;
		int m_TileY = 0;
		EPlacementError m_Error = EPlacementError::NONE;
	};

	struct SOwnedPreviewRect
	{
		CUIRect m_Rect;
		std::string m_Id;
		std::string m_GraffityId;
	};

	static void ConGraffity(IConsole::IResult *pResult, void *pUserData);

	void LoadTextures();
	void UnloadTextures();
	void LoadSounds();
	void UnloadSounds();
	void PlaySprayOpenSound();
	void PlaySpraySelectSound();
	void PlaySprayApplySound();
	void OpenWheel();
	void ToggleWheel();
	void CloseWheel();
	void ReleaseWheel();
	int SelectedWheelIndex() const;
	void ClampSelectorMouseToCircle();
	void CancelPlacement();
	void BeginPlacement(int Index);
	void QueueOutbound(std::string Line);
	void DrainInbound();
	void EnsureNetworkConnection();
	void StartNetwork(const char *pGameServerAddress, const char *pOwnerId);
	void StopNetwork();
	void JoinNetworkThreadIfNeeded();
	void NetworkMain(std::string GameServerAddress, std::string OwnerId);
	void HandleIncomingLine(const std::string &Line);
	void ApplySnapshot(const json_value *pJson);
	void SendRemove(const std::string &Id);
	void SendPlace(const SPlacementPreview &Preview);
	SPlacementPreview BuildPlacementPreview() const;
	std::vector<SOwnedPreviewRect> BuildOwnedPreviewRects(const CUIRect &Screen) const;
	const SGraffityDef *FindDefinitionById(const char *pId) const;
	const SGraffityDef *FindDefinitionById(const std::string &Id) const;
	bool GraffityEnabled() const;
	int GraffitySizeStep() const;
	bool IsLocalhostGameServer() const;
	bool CellHasSurface(int TileX, int TileY) const;
	bool IntersectsExisting(const vec2 &Pos, int SizeStep, const char *pIgnoreId = nullptr) const;
	void MapGameScreen() const;
	float GraffityWorldSize(int SizeStep) const;
	float PlacementSelectionZoomScale() const;
	void RenderWorldGraffities(bool AirOnly) const;
	void RenderPlacementPreview(const SPlacementPreview &Preview, bool OverlayPass) const;
	void RenderWheelUi();
	void RenderGraffityQuad(const SGraffityDef &Definition, vec2 Pos, float Size, ColorRGBA Color) const;
	void EchoPlacementError(EPlacementError Error);

	static constexpr int NUM_GRAFFITIES = 3;

	SGraffityDef m_aDefinitions[NUM_GRAFFITIES];
	std::vector<SPlacedGraffity> m_vPlacedGraffities;

	float m_AnimationTime = 0.0f;
	float m_aHoverAnimation[NUM_GRAFFITIES] = {};
	bool m_WheelActive = false;
	bool m_WasWheelActive = false;
	vec2 m_SelectorMouse = vec2(0.0f, 0.0f);
	int m_SelectedIndex = -1;

	bool m_PlacementActive = false;
	int m_PlacementIndex = -1;

	bool m_TexturesLoaded = false;
	bool m_TextureErrorShown = false;
	enum
	{
		SPRAY_SOUND_OPEN = 0,
		SPRAY_SOUND_SELECT,
		SPRAY_SOUND_APPLY,
		NUM_SPRAY_SOUNDS,
	};
	int m_aSpraySoundIds[NUM_SPRAY_SOUNDS] = {-1, -1, -1};
	bool m_SpraySoundErrorShown = false;
	bool m_PendingPlacementSound = false;

	std::atomic<bool> m_StopThread{false};
	std::atomic<bool> m_NetworkRunning{false};
	std::atomic<bool> m_NetworkConnected{false};
	std::thread m_NetworkThread;
	std::mutex m_NetMutex;
	std::condition_variable m_NetCv;
	std::vector<std::string> m_vOutboundLines;
	std::vector<std::string> m_vInboundLines;
	std::string m_ActiveGameServerAddress;
	std::string m_ActiveGraffityServerAddress;
	char m_aLastError[256] = "";
	int64_t m_LastConnectAttempt = 0;
	int64_t m_LastPlacementErrorTick = 0;

	// Protects m_NetworkSocket so StopNetwork() can close it to interrupt
	// a blocking net_tcp_connect() and avoid freezing the main thread.
	NETSOCKET m_NetworkSocket = nullptr;
	std::mutex m_SocketMutex;
};

#endif
