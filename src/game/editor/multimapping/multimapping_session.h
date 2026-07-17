#ifndef GAME_EDITOR_MULTIMAPPING_SESSION_H
#define GAME_EDITOR_MULTIMAPPING_SESSION_H

#include <game/editor/component.h>
#include <game/editor/multimapping/multimapping_protocol.h>
#include <game/client/ui.h>
#include <game/mapitems.h>
#include <base/net.h>
#include <base/system.h>
#include <map>
#include <set>
#include <utility>
#include <vector>

class CMultiMappingSession : public CEditorComponent
{
public:
	void OnInit(CEditor *pEditor) override;
	void OnReset() override;
	void OnUpdate() override;
	void OnRender(CUIRect View) override;

	void NotifyTileEdit(int GroupIdx, int LayerIdx, int TileX, int TileY, uint8_t Index, uint8_t Flags);
	// Tele layer
	void NotifyTileEditTele(int GroupIdx, int LayerIdx, int TileX, int TileY, uint8_t Index, uint8_t Number, uint8_t Type);
	// Speedup layer
	void NotifyTileEditSpeedup(int GroupIdx, int LayerIdx, int TileX, int TileY, uint8_t Index, uint8_t Force, uint8_t MaxSpeed, int16_t Angle);
	// Switch layer
	void NotifyTileEditSwitch(int GroupIdx, int LayerIdx, int TileX, int TileY, uint8_t Index, uint8_t Flags, uint8_t Number, uint8_t SwitchType, uint8_t SwitchFlags, uint8_t Delay);
	// Tune layer
	void NotifyTileEditTune(int GroupIdx, int LayerIdx, int TileX, int TileY, uint8_t Index, uint8_t Number, uint8_t Type);
	void NotifyStrokeEnd(); // call when mouse button released after drawing
	void NotifyFullSync();  // call after undo/redo вЂ” checks all tile layers
	void NotifyAddGroup(int InsertIdx = -1);
	void NotifyDelGroup(int GroupIdx);
	void NotifyAddLayer(int GroupIdx, int LayerIdx, int LayerType, const char *pName, int SubType = 0);
	void NotifyDelLayer(int GroupIdx, int LayerIdx);
	void SyncLayerContents(int GroupIdx, int LayerIdx); // send image + quads/tiles after layer restore
	void NotifySetImage(int GroupIdx, int LayerIdx, int ImageIdx);
	void NotifyRenameGroup(int GroupIdx, const char *pName);
	void NotifyRenameLayer(int GroupIdx, int LayerIdx, const char *pName);
	void NotifyLayerProp(int GroupIdx, int LayerIdx, int PropId, int Value);
	void NotifyAddImage(const char *pName, bool External, const uint8_t *pData, int DataSize);
	void NotifyDelImage(int ImageIdx);
	void NotifyEmbedImage(int ImageIdx, const uint8_t *pData, int DataSize);
	void NotifyExternImage(int ImageIdx);
	void NotifyAddSound(const char *pName, const uint8_t *pData, int DataSize);
	void NotifyDelSound(int SoundIdx);
	void NotifySetSound(int GroupIdx, int LayerIdx, int SoundIdx);
	void NotifyAddSoundSource(int GroupIdx, int LayerIdx, int SourceIdx);
	void NotifyDelSoundSource(int GroupIdx, int LayerIdx, int SourceIdx);
	void NotifyEditSoundSource(int GroupIdx, int LayerIdx, int SourceIdx, int PropId, int Value);
	void NotifyAddQuad(int GroupIdx, int LayerIdx, int QuadIdx, const CQuad &Quad);
	void NotifyDelQuad(int GroupIdx, int LayerIdx, int QuadIdx);
	void NotifyQuadPoints(int GroupIdx, int LayerIdx, int QuadIdx, const CPoint *pPoints);
	void NotifyQuadColors(int GroupIdx, int LayerIdx, int QuadIdx, const CColor *pColors);
	void NotifyQuadProp(int GroupIdx, int LayerIdx, int QuadIdx, int Prop, int Value);
	void NotifyQuadPointProp(int GroupIdx, int LayerIdx, int QuadIdx, int PointIdx, int Prop, int Value);
	void NotifyLayerFlags(int GroupIdx, int LayerIdx, int Flags);
	void NotifyGroupProp(int GroupIdx, int PropId, int Value);
	void NotifySettingAdd(const char *pCmd);
	void NotifySettingDel(int CmdIdx);
	void NotifySettingEdit(int CmdIdx, const char *pCmd);
	void NotifySettingMove(int CmdIdx, int Direction);
	void NotifyEditorSettings();
	void StartMapTransfer(uint8_t TargetSlot = 0xFF); // called when STATE_LIVE and we are authority; TargetSlot 0xFF = broadcast to all others
	bool IsLive() const { return m_State == STATE_LIVE; }
	bool FindGroupAndLayer(const void *pLayerPtr, int &GroupIdx, int &LayerIdx) const;

	static CUi::EPopupMenuFunctionResult PopupMultiMapping(void *pContext, CUIRect View, bool Active);
	static CUi::EPopupMenuFunctionResult PopupMultiMappingMain(void *pContext, CUIRect View, bool Active);
	static CUi::EPopupMenuFunctionResult PopupMultiMappingCreate(void *pContext, CUIRect View, bool Active);
	static CUi::EPopupMenuFunctionResult PopupMultiMappingJoin(void *pContext, CUIRect View, bool Active);

	enum EState
	{
		STATE_IDLE = 0,
		STATE_CONNECTING,
		STATE_WAITING,
		STATE_LIVE,
		STATE_ERROR,
	};

	EState m_State = STATE_IDLE;
	NETSOCKET m_Socket = nullptr;
	NETADDR m_ServerAddr = {};
	int64_t m_LastHeartbeatTime = 0;
	int64_t m_LastServerPacketTime = 0;
	int64_t m_LastCursorSendTime = 0;
	int64_t m_ConnectStartTime = 0;
	char m_aRoomCode[MultiMappingProtocol::ROOM_CODE_LEN + 1] = {};
	bool m_IsCreator = false;

	struct SPeer
	{
		bool m_Connected = false;
		bool m_IsCreator = false;
		char m_aNickname[MultiMappingProtocol::MAX_NICKNAME_LEN + 1] = {};
		float m_CursorX = 0.0f;
		float m_CursorY = 0.0f;
		bool m_HasCursor = false;
		MultiMappingProtocol::EActivity m_Activity = MultiMappingProtocol::ACTIVITY_MAPPING;
	};
	SPeer m_aPeers[MultiMappingProtocol::MAX_MULTIMAPPING_PLAYERS];
	uint8_t m_LocalSlot = 0;
	char m_aLocalNickname[MultiMappingProtocol::MAX_NICKNAME_LEN + 1] = {};

	// late-join map push queue (authority only): slots that appeared in the
	// roster as newly connected and still need the initial full map transfer
	std::vector<uint8_t> m_vPendingLateJoinTargets;
	uint8_t m_PendingMapTransferTarget = 0xFF; // 0xFF = broadcast to all others

	MultiMappingProtocol::EActivity m_LastLocalActivity = MultiMappingProtocol::ACTIVITY_MAPPING;

	int m_ParticipantCount = 0;
	char m_aJoinCodeInput[MultiMappingProtocol::ROOM_CODE_LEN + 1] = {};
	int m_JoinCodeLen = 0;
	char m_aErrorMsg[128] = {};

	// TCP recv buffer for stream reassembly
	std::vector<uint8_t> m_vRecvBuf;
	int m_RecvBufLen = 0;

	// TCP outbound queue вЂ” SendFrame enqueues, DrainSendBuf drains non-blocking
	// Priority queue for ping/pong/cursor вЂ” always drained first
	std::vector<uint8_t> m_vSendBufPrio;
	int m_SendBufPrioOffset = 0;
	// Normal queue for tile edits, sync data, etc.
	std::vector<uint8_t> m_vSendBuf;
	int m_SendBufOffset = 0;

	// set while applying a remote packet вЂ” prevents re-broadcasting back
	bool m_ApplyingRemote = false;
	// set while owner is loading a new map to transfer вЂ” prevents OnReset from disconnecting
	bool m_OwnerLoadingMap = false;
	// set when MAP_NEW received вЂ” processed next frame
	bool m_PendingMapNew = false;
	// set when map transfer should happen after save completes
	bool m_PendingMapTransfer = false;
	// envelope sync: track undo stack size to detect changes
	int m_LastEnvUndoSize = 0;
	bool m_EnvDirty = false;
	// set when no OTHER peer is currently connected while session is LIVE
	// (i.e. we're alone in the room) вЂ” not triggered by any single peer among
	// several leaving, since the room keeps functioning for the rest
	bool m_RemoteDisconnected = false;
	// set by TestMapLocally to distinguish testing from just closing editor
	bool m_LocalTestingActive = false;

	// status log вЂ” up to 4 recent entries shown in menubar
	struct SLogEntry { char m_aText[128]; };
	static const int LOG_CAPACITY = 4;
	SLogEntry m_aLog[LOG_CAPACITY] = {};
	int m_LogCount = 0;
	void PushLog(const char *pText);
	static void SanitizeMapName(const char *pIn, char *pOut, int OutSize);

	// debug counters
	int m_DbgQuadSent = 0;
	int m_DbgQuadRecv = 0;

	// e2e tile latency: time from FlushTileEdits to receiving TILE_RELAY, in ms
	int64_t m_LastTileEditSentTime = 0;   // time_get() when last batch was flushed
	int m_TileRelayLatencyMs = -1;        // -1 = no measurement yet

	// ping/pong RTT measurement вЂ” sent every ~2s while STATE_LIVE
	int64_t m_LastPingSentTime = 0;
	int64_t m_LastPingTime = 0;
	int m_PingMs = -1;                    // -1 = no measurement yet

	// deferred SYNC_REQUEST: when we receive SYNC_CHECK with a mismatched CRC,
	// wait 500ms before sending SYNC_REQUEST вЂ” gives TILE_RELAY packets time to arrive
	struct SPendingSyncRequest
	{
		int m_GroupIdx = -1;
		int m_LayerIdx = -1;
		int64_t m_RequestAfter = 0; // time_get() value after which to send
	};
	std::vector<SPendingSyncRequest> m_vPendingSyncRequests;

	// deferred SYNC_DATA responses: queued in HandleMessage, sent in OnBackgroundUpdate
	struct SPendingSyncResponse
	{
		int m_GroupIdx = -1;
		int m_LayerIdx = -1;
	};
	std::vector<SPendingSyncResponse> m_vPendingSyncResponses;

	int64_t m_LastFullSyncTime = 0;

	// layer index cache вЂ” maps CLayer pointer to (GroupIdx, LayerIdx)
	// invalidated by NotifyAddLayer/NotifyDelLayer/NotifyAddGroup/NotifyDelGroup
	mutable std::map<const void *, std::pair<int, int>> m_LayerIndexCache;
	mutable bool m_LayerCacheValid = false;
	void InvalidateLayerCache() { m_LayerCacheValid = false; m_LayerIndexCache.clear(); }

	// map transfer вЂ” receiver side
	bool m_MapTransferActive = false;
	int m_MapTransferTotal = 0;
	int m_MapTransferReceived = 0;
	char m_aMapTransferName[256] = {};
	std::vector<uint8_t> m_vMapTransferBuf;

	struct STileEditEntry
	{
		int m_GroupIdx;
		int m_LayerIdx;
		int m_TileX;
		int m_TileY;
		uint8_t m_Index;
		uint8_t m_Flags;
		// ExtraType: 0=none, 1=tele, 2=speedup, 3=switch, 4=tune
		uint8_t m_ExtraType = 0;
		uint8_t m_ExNumber = 0;
		uint8_t m_ExType = 0;
		uint8_t m_ExForce = 0;
		uint8_t m_ExMaxSpeed = 0;
		int16_t m_ExAngle = 0;
		uint8_t m_ExSwitchFlags = 0;
		uint8_t m_ExDelay = 0;
	};
	std::vector<STileEditEntry> m_vPendingTileEdits;

	// layers touched during current mouse stroke вЂ” flushed on NotifyStrokeEnd
	std::set<std::pair<int, int>> m_DirtyLayers;

	void Connect(const char *pRoomCode, bool Create);
	void Disconnect();
	void OpenSocket();
	void CloseSocket();
	void DrainSendBuf();
	void SendFrame(const std::vector<uint8_t> &vPayload);
	void SendFramePrio(const std::vector<uint8_t> &vPayload); // ping/pong/cursor вЂ” bypasses normal queue
	void SendHello();
	void SendHeartbeat();
	void SendCursor(float WorldX, float WorldY);
	void SendTileEdit(int GroupIdx, int LayerIdx, int TileX, int TileY, uint8_t Index, uint8_t Flags);
	void SendTileEditExtra(const STileEditEntry &Entry);
	void FlushTileEdits();
	void SendSyncCheck(int GroupIdx, int LayerIdx);
	void SendSyncRequest(int GroupIdx, int LayerIdx);
	void SendSyncData(int GroupIdx, int LayerIdx);
	void SendStructAddGroup(int InsertIdx);
	void SendStructDelGroup(int GroupIdx);
	void SendStructAddLayer(int GroupIdx, int LayerIdx, int LayerType, const char *pName, int SubType = 0);
	void SendStructDelLayer(int GroupIdx, int LayerIdx);
	void SendStructSetImage(int GroupIdx, int LayerIdx, int ImageIdx);
	void SendStructRenameGroup(int GroupIdx, const char *pName);
	void SendStructRenameLayer(int GroupIdx, int LayerIdx, const char *pName);
	void SendStructLayerProp(int GroupIdx, int LayerIdx, int PropId, int Value);
	void SendStructAddImage(const char *pName, bool External, const uint8_t *pData, int DataSize);
	void SendStructDelImage(int ImageIdx);
	void SendStructEmbedImage(int ImageIdx, const uint8_t *pData, int DataSize);
	void SendStructExternImage(int ImageIdx);
	void SendStructAddSound(const char *pName, const uint8_t *pData, int DataSize);
	void SendStructDelSound(int SoundIdx);
	void SendStructSetSound(int GroupIdx, int LayerIdx, int SoundIdx);
	void SendStructAddSoundSource(int GroupIdx, int LayerIdx, int SourceIdx);
	void SendStructDelSoundSource(int GroupIdx, int LayerIdx, int SourceIdx);
	void SendStructEditSoundSource(int GroupIdx, int LayerIdx, int SourceIdx, int PropId, int Value);
	void SendQuadAdd(int GroupIdx, int LayerIdx, int QuadIdx, const CQuad &Quad);
	void SendQuadDel(int GroupIdx, int LayerIdx, int QuadIdx);
	void SendQuadPoints(int GroupIdx, int LayerIdx, int QuadIdx, const CPoint *pPoints);
	void SendQuadColors(int GroupIdx, int LayerIdx, int QuadIdx, const CColor *pColors);
	void SendQuadProp(int GroupIdx, int LayerIdx, int QuadIdx, int Prop, int Value);
	void SendQuadPointProp(int GroupIdx, int LayerIdx, int QuadIdx, int PointIdx, int Prop, int Value);
	void SendLayerFlags(int GroupIdx, int LayerIdx, int Flags);
	void SendGroupProp(int GroupIdx, int PropId, int Value);
	void SendSettingAdd(const char *pCmd);
	void SendSettingDel(int CmdIdx);
	void SendSettingEdit(int CmdIdx, const char *pCmd);
	void SendSettingMove(int CmdIdx, int Direction);
	void SendGoodbye();
	void SendPing();
	void SendMapStart(const char *pName, int TotalSize, uint8_t TargetSlot = 0xFF);
	void SendMapChunk(int Offset, const uint8_t *pData, int DataLen);
	void SendMapEnd();
	void SendMapNew();
	void SendEditorSettings();
	void SendActivity(MultiMappingProtocol::EActivity Activity);
	void OnBackgroundUpdate();
	void ProcessNetwork();
	void HandleMessage(const uint8_t *pData, int Size);
	void AppendAuth(std::vector<uint8_t> &vPacket) const;

private:
	static uint32_t CalcLayerCRC(const uint8_t *pTiles, int Count, uint32_t InitCrc = 0xFFFFFFFFu);
	// returns the slot of the current map authority (self if we're the creator), or 0xFF if unknown
	uint8_t FindAuthoritySlot() const;
	// push a local envelope edit: authority broadcasts to everyone, non-authority pushes to authority only
	void TriggerEnvelopeResync();
};

#endif // GAME_EDITOR_MULTIMAPPING_SESSION_H
