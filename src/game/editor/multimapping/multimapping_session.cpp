#include "multimapping_session.h"

#include <game/editor/editor.h>
#include <game/editor/editor_actions.h>
#include <game/editor/mapitems.h>
#include <game/editor/mapitems/layer_tiles.h>
#include <game/editor/mapitems/layer_tele.h>
#include <game/editor/mapitems/layer_speedup.h>
#include <game/editor/mapitems/layer_switch.h>
#include <game/editor/mapitems/layer_tune.h>
#include <game/editor/mapitems/layer_quads.h>
#include <game/editor/mapitems/layer_sounds.h>
#include <game/editor/mapitems/image.h>
#include <game/editor/mapitems/sound.h>
#include <engine/gfx/image_loader.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/sound.h>
#include <engine/shared/config.h>
#include <engine/keys.h>
#include <base/hash_ctxt.h>
#include <base/math.h>
#include <game/client/lineinput.h>
#include <cstdio>
#include <cstdlib>

using namespace MultiMappingProtocol;

static const uint8_t s_aAuthKeyObfuscated[32] = {
	0x1e, 0x2f, 0x3b, 0x37, 0x3b, 0x2b, 0x3e, 0x2b,
	0x6b, 0x78, 0x7e, 0x7a, 0x6e, 0x6b, 0x7b, 0x6b,
	0x29, 0x3f, 0x2b, 0x3d, 0x3f, 0x2b, 0x4b, 0x7b,
	0x7b, 0x3c, 0x3d, 0x3e, 0x3f, 0x60, 0x62, 0x63,
};
static constexpr uint8_t AUTH_XOR_KEY = 0x5A;

static void DeobfuscateKey(uint8_t *pOut)
{
	for(int i = 0; i < 32; i++)
		pOut[i] = s_aAuthKeyObfuscated[i] ^ AUTH_XOR_KEY;
}

void CMultiMappingSession::OnInit(CEditor *pEditor)
{
	CEditorComponent::OnInit(pEditor);
}

void CMultiMappingSession::PushLog(const char *pText)
{
	// shift entries down, newest at index 0
	for(int i = LOG_CAPACITY - 1; i > 0; i--)
		m_aLog[i] = m_aLog[i - 1];
	str_copy(m_aLog[0].m_aText, pText);
	if(m_LogCount < LOG_CAPACITY)
		m_LogCount++;
}

void CMultiMappingSession::SanitizeMapName(const char *pIn, char *pOut, int OutSize)
{
	// Defense against path traversal: a malicious peer fully controls this
	// string. Strip any directory component and keep only a safe basename,
	// then force a .map extension so the result can only ever be a map file
	// inside maps/. Never trust the wire value for filesystem paths.
	const char *pBase = pIn;
	for(const char *p = pIn; *p; p++)
	{
		if(*p == '/' || *p == '\\')
			pBase = p + 1;
	}

	int Len = 0;
	char aClean[128];
	for(const char *p = pBase; *p && Len < (int)sizeof(aClean) - 1; p++)
	{
		char c = *p;
		bool Ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			  (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
		// reject "." sequences that could form ".." traversal
		if(c == '.' && (p == pBase || p[1] == '.' || p[1] == '\0'))
			Ok = false;
		if(Ok)
			aClean[Len++] = c;
	}
	aClean[Len] = '\0';

	if(Len == 0)
		str_copy(aClean, "unnamed");

	// strip existing extension and force .map
	char aNoExt[128];
	fs_split_file_extension(aClean, aNoExt, sizeof(aNoExt));
	if(aNoExt[0] == '\0')
		str_copy(aNoExt, "unnamed");
	str_format(pOut, OutSize, "%s.map", aNoExt);
}

void CMultiMappingSession::OnReset()
{
	if(!m_ApplyingRemote && !m_OwnerLoadingMap)
	{
		if(m_State == STATE_LIVE && m_IsCreator)
			return;
		Disconnect();
	}
}

void CMultiMappingSession::OnUpdate()
{
	if(m_State == STATE_IDLE || m_State == STATE_ERROR)
		return;

	if(m_Socket == nullptr)
		return;

	if(m_State == STATE_CONNECTING && m_ConnectStartTime != 0)
	{
		const int ConnectResult = net_tcp_connect_poll(m_Socket);
		if(ConnectResult > 0)
		{
			m_ConnectStartTime = 0;
			SendHello();
		}
		else if(ConnectResult < 0 || time_get() - m_ConnectStartTime >= 5 * time_freq())
		{
			CloseSocket();
			str_copy(m_aErrorMsg, "Failed to connect");
			m_State = STATE_ERROR;
		}
		return;
	}

	// Apply pending MAP_NEW (deferred to avoid calling Reset mid-network-processing)
	if(m_PendingMapNew)
	{
		m_PendingMapNew = false;
		m_ApplyingRemote = true;
		Editor()->Reset();
		m_ApplyingRemote = false;
	}

	// Deferred map transfer: wait for save job to finish, then read and send
	if(m_PendingMapTransfer && Editor()->m_WriterFinishJobs.empty())
	{
		m_PendingMapTransfer = false;
		static const char *s_pTmpPath = "multimapping_transfer_tmp.map";
		IOHANDLE File = Editor()->Storage()->OpenFile(s_pTmpPath, IOFLAG_READ, IStorage::TYPE_SAVE);
		if(File)
		{
			std::vector<uint8_t> vData;
			uint8_t aBuf[32768];
			int Read;
			while((Read = io_read(File, aBuf, sizeof(aBuf))) > 0)
				vData.insert(vData.end(), aBuf, aBuf + Read);
			io_close(File);
			Editor()->Storage()->RemoveFile(s_pTmpPath, IStorage::TYPE_SAVE);

			if(!vData.empty())
			{
				const char *pFilename = Editor()->Map()->m_aFilename;
				const char *pBaseName = pFilename[0] ? pFilename : "untitled.map";
				for(const char *p = pBaseName; *p; p++)
					if(*p == '/' || *p == '\\')
						pBaseName = p + 1;
				dbg_msg("multimapping", "MapTransfer: sending '%s' (%d bytes) target=%d", pBaseName, (int)vData.size(), m_PendingMapTransferTarget);
				SendMapStart(pBaseName, (int)vData.size(), m_PendingMapTransferTarget);
				const int ChunkSize = 32768;
				for(int Offset = 0; Offset < (int)vData.size(); Offset += ChunkSize)
				{
					int Len = minimum(ChunkSize, (int)vData.size() - Offset);
					SendMapChunk(Offset, vData.data() + Offset, Len);
				}
				SendMapEnd();
			}
		}
	}

	// Envelope sync: detect changes and send map transfer when mouse released
	if(m_State == STATE_LIVE && !m_ApplyingRemote)
	{
		int CurEnvUndoSize = (int)Editor()->Map()->m_EnvelopeEditorHistory.m_vpUndoActions.size();
		if(CurEnvUndoSize != m_LastEnvUndoSize)
		{
			m_LastEnvUndoSize = CurEnvUndoSize;
			m_EnvDirty = true;
		}
		if(m_EnvDirty && !Editor()->Input()->KeyIsPressed(KEY_MOUSE_1))
		{
			m_EnvDirty = false;
			TriggerEnvelopeResync();
		}
	}

	// Late-join map push (authority only): send queued new joiners their
	// initial full map, one at a time, once any in-flight save/transfer is done.
	if(m_State == STATE_LIVE && !m_ApplyingRemote && m_IsCreator && !m_PendingMapTransfer &&
		!m_vPendingLateJoinTargets.empty() && Editor()->m_WriterFinishJobs.empty())
	{
		uint8_t TargetSlot = m_vPendingLateJoinTargets.front();
		m_vPendingLateJoinTargets.erase(m_vPendingLateJoinTargets.begin());
		StartMapTransfer(TargetSlot);
	}

	int64_t Now = time_get();
	int64_t Freq = time_freq();

	// Cursor sync at ~30 Hz РІР‚вЂќ only while actively mapping, so the partner's
	// cursor disappears when we open a dialog / picker / extra editor.
	if(m_State == STATE_LIVE && m_LastLocalActivity == ACTIVITY_MAPPING && Now - m_LastCursorSendTime > Freq / 30)
	{
		SendCursor(Editor()->m_MouseWorldNoParaPos.x, Editor()->m_MouseWorldNoParaPos.y);
		m_LastCursorSendTime = Now;
	}

	// Flush batched tile edits
	if(m_State == STATE_LIVE)
		FlushTileEdits();

	// Drain outbound TCP buffer non-blocking
	DrainSendBuf();
}

// fixed per-slot palette РІР‚вЂќ shared knowledge baked into every client build, so
// only the 1-byte slot id needs to travel over the wire, never RGB.
static const ColorRGBA s_aMultiMappingSlotColors[MultiMappingProtocol::MAX_MULTIMAPPING_PLAYERS] = {
	ColorRGBA(0.20f, 0.80f, 1.00f, 0.85f), // 0: cyan
	ColorRGBA(1.00f, 0.55f, 0.10f, 0.85f), // 1: orange
	ColorRGBA(1.00f, 0.25f, 0.75f, 0.85f), // 2: pink
	ColorRGBA(0.35f, 0.90f, 0.25f, 0.85f), // 3: green
	ColorRGBA(1.00f, 0.90f, 0.15f, 0.85f), // 4: yellow
	ColorRGBA(0.65f, 0.35f, 1.00f, 0.85f), // 5: purple
	ColorRGBA(1.00f, 0.25f, 0.25f, 0.85f), // 6: red
	ColorRGBA(0.90f, 0.90f, 0.90f, 0.85f), // 7: white
};

void CMultiMappingSession::OnRender(CUIRect View)
{
	if(m_State != STATE_LIVE)
		return;

	std::shared_ptr<CLayerGroup> pGameGroup;
	for(const auto &pGroup : Editor()->Map()->m_vpGroups)
	{
		if(pGroup->m_GameGroup)
		{
			pGameGroup = pGroup;
			break;
		}
	}
	if(!pGameGroup)
		return;

	float aPoints[4];
	pGameGroup->Mapping(aPoints);
	float WorldWidth = aPoints[2] - aPoints[0];
	float WorldHeight = aPoints[3] - aPoints[1];
	if(WorldWidth <= 0.0f || WorldHeight <= 0.0f)
		return;

	const CUIRect *pScreen = Ui()->Screen();

	for(int Slot = 0; Slot < MultiMappingProtocol::MAX_MULTIMAPPING_PLAYERS; Slot++)
	{
		if(Slot == m_LocalSlot)
			continue;
		const SPeer &Peer = m_aPeers[Slot];
		if(!Peer.m_Connected || !Peer.m_HasCursor)
			continue;

		float ScreenX = (Peer.m_CursorX - aPoints[0]) / WorldWidth * pScreen->w;
		float ScreenY = (Peer.m_CursorY - aPoints[1]) / WorldHeight * pScreen->h;

		if(ScreenX < View.x || ScreenX > View.x + View.w || ScreenY < View.y || ScreenY > View.y + View.h)
			continue;

		const ColorRGBA &Color = s_aMultiMappingSlotColors[Slot];

		Graphics()->WrapClamp();
		Graphics()->TextureSet(Editor()->m_aCursorTextures[CEditor::CURSOR_NORMAL]);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(Color.r, Color.g, Color.b, Color.a);
		IGraphics::CQuadItem QuadItem(ScreenX, ScreenY, 16.0f, 16.0f);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
		Graphics()->QuadsEnd();
		Graphics()->WrapNormal();

		if(Peer.m_aNickname[0])
		{
			CUIRect LabelRect;
			LabelRect.x = ScreenX + 12.0f;
			LabelRect.y = ScreenY - 2.0f;
			LabelRect.w = 120.0f;
			LabelRect.h = 12.0f;
			TextRender()->TextColor(Color.r, Color.g, Color.b, 1.0f);
			Ui()->DoLabel(&LabelRect, Peer.m_aNickname, 8.0f, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
	}
}

void CMultiMappingSession::Connect(const char *pRoomCode, bool Create)
{
	Disconnect();
	m_IsCreator = Create;
	m_ParticipantCount = 0;
	m_aErrorMsg[0] = '\0';
	m_vRecvBuf.clear();
	m_RecvBufLen = 0;
	m_LastHeartbeatTime = time_get();
	m_LastCursorSendTime = time_get();
	m_LocalSlot = 0;
	for(auto &Peer : m_aPeers)
		Peer = SPeer();
	m_vPendingLateJoinTargets.clear();
	m_PendingMapTransferTarget = 0xFF;
	str_copy(m_aLocalNickname, g_Config.m_PlayerName, sizeof(m_aLocalNickname));
	str_utf8_fix_truncation(m_aLocalNickname); // str_copy truncates by raw byte count, which can cut a multi-byte UTF-8 character in half

	if(pRoomCode)
		str_copy(m_aRoomCode, pRoomCode, sizeof(m_aRoomCode));
	else
		m_aRoomCode[0] = '\0';

	NETADDR Addr;
	mem_zero(&Addr, sizeof(Addr));
	{
		static const uint8_t s_aEnc[] = {0x7a, 0x72, 0x78, 0x65, 0x79, 0x78, 0x65, 0x79, 0x7b, 0x7a, 0x65, 0x7a, 0x79, 0x7e, 0x71, 0x7e, 0x7e, 0x7e, 0x7e};
		char aAddr[32];
		for(int i = 0; i < (int)sizeof(s_aEnc); i++)
			aAddr[i] = s_aEnc[i] ^ 0x4B;
		aAddr[sizeof(s_aEnc)] = '\0';
		if(net_addr_from_str(&Addr, aAddr) != 0)
		{
			str_copy(m_aErrorMsg, "Invalid server address");
			m_State = STATE_ERROR;
			return;
		}
	}
	m_ServerAddr = Addr;

	OpenSocket();
	if(m_Socket == nullptr)
	{
		str_copy(m_aErrorMsg, "Failed to connect");
		m_State = STATE_ERROR;
		return;
	}
	m_State = STATE_CONNECTING;
	m_ConnectStartTime = time_get();
	m_LastServerPacketTime = time_get();
}

void CMultiMappingSession::Disconnect()
{
	if(m_State >= STATE_CONNECTING)
		SendGoodbye();
	CloseSocket();
	m_State = STATE_IDLE;
	for(auto &Peer : m_aPeers)
		Peer = SPeer();
	m_LastLocalActivity = ACTIVITY_MAPPING;
	m_RemoteDisconnected = false;
	m_LogCount = 0;
	m_ParticipantCount = 0;
	m_RecvBufLen = 0;
	m_vRecvBuf.clear();
	m_vPendingTileEdits.clear();
	m_DirtyLayers.clear();
	m_vPendingLateJoinTargets.clear();
	m_PendingMapTransferTarget = 0xFF;
}

void CMultiMappingSession::OpenSocket()
{
	NETADDR Bind;
	mem_zero(&Bind, sizeof(Bind));
	Bind.type = NETTYPE_IPV4;
	Bind.port = 0;
	m_Socket = net_tcp_create(Bind);
	if(m_Socket == nullptr)
		return;
	net_tcp_connect_non_blocking(m_Socket, m_ServerAddr);
}

void CMultiMappingSession::CloseSocket()
{
	if(m_Socket != nullptr)
	{
		net_tcp_close(m_Socket);
		m_Socket = nullptr;
	}
	m_vSendBufPrio.clear();
	m_SendBufPrioOffset = 0;
	m_vSendBuf.clear();
	m_SendBufOffset = 0;
	m_vPendingSyncRequests.clear();
	m_vPendingSyncResponses.clear();
}

void CMultiMappingSession::SendFrame(const std::vector<uint8_t> &vPayload)
{
	if(m_Socket == nullptr)
		return;
	if((int)(m_vSendBuf.size() - m_SendBufOffset) + (int)(m_vSendBufPrio.size() - m_SendBufPrioOffset) > 4 * 1024 * 1024)
	{
		str_copy(m_aErrorMsg, "Send buffer overflow");
		m_State = STATE_ERROR;
		CloseSocket();
		return;
	}
	uint32_t Size = (uint32_t)vPayload.size();
	m_vSendBuf.push_back((Size >> 24) & 0xFF);
	m_vSendBuf.push_back((Size >> 16) & 0xFF);
	m_vSendBuf.push_back((Size >> 8) & 0xFF);
	m_vSendBuf.push_back(Size & 0xFF);
	m_vSendBuf.insert(m_vSendBuf.end(), vPayload.begin(), vPayload.end());
}

void CMultiMappingSession::SendFramePrio(const std::vector<uint8_t> &vPayload)
{
	if(m_Socket == nullptr)
		return;
	uint32_t Size = (uint32_t)vPayload.size();
	m_vSendBufPrio.push_back((Size >> 24) & 0xFF);
	m_vSendBufPrio.push_back((Size >> 16) & 0xFF);
	m_vSendBufPrio.push_back((Size >> 8) & 0xFF);
	m_vSendBufPrio.push_back(Size & 0xFF);
	m_vSendBufPrio.insert(m_vSendBufPrio.end(), vPayload.begin(), vPayload.end());
}

void CMultiMappingSession::DrainSendBuf()
{
	if(m_Socket == nullptr)
		return;

	// drain priority queue first (ping/pong/cursor)
	if(!m_vSendBufPrio.empty())
	{
		const uint8_t *pData = m_vSendBufPrio.data() + m_SendBufPrioOffset;
		int Remaining = (int)m_vSendBufPrio.size() - m_SendBufPrioOffset;
		while(Remaining > 0)
		{
			int Sent = net_tcp_send(m_Socket, pData, Remaining);
			if(Sent > 0) { pData += Sent; m_SendBufPrioOffset += Sent; Remaining -= Sent; }
			else if(Sent == 0) { str_copy(m_aErrorMsg, "Connection lost"); m_State = STATE_ERROR; CloseSocket(); return; }
			else if(net_would_block()) break;
			else { str_copy(m_aErrorMsg, "Connection lost"); m_State = STATE_ERROR; CloseSocket(); return; }
		}
		if(m_SendBufPrioOffset >= (int)m_vSendBufPrio.size()) { m_vSendBufPrio.clear(); m_SendBufPrioOffset = 0; }
	}

	// drain normal queue
	if(m_vSendBuf.empty())
		return;
	const uint8_t *pData = m_vSendBuf.data() + m_SendBufOffset;
	int Remaining = (int)m_vSendBuf.size() - m_SendBufOffset;
	while(Remaining > 0)
	{
		int Sent = net_tcp_send(m_Socket, pData, Remaining);
		if(Sent > 0) { pData += Sent; m_SendBufOffset += Sent; Remaining -= Sent; }
		else if(Sent == 0) { str_copy(m_aErrorMsg, "Connection lost"); m_State = STATE_ERROR; CloseSocket(); return; }
		else if(net_would_block()) break;
		else { str_copy(m_aErrorMsg, "Connection lost"); m_State = STATE_ERROR; CloseSocket(); return; }
	}
	if(m_SendBufOffset >= (int)m_vSendBuf.size())
	{
		m_vSendBuf.clear();
		m_SendBufOffset = 0;
	}
}

void CMultiMappingSession::SendHello()
{
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_HELLO);
	WriteU16(vPacket, 100); // client build
	WriteU8(vPacket, m_IsCreator ? 1 : 0);
	int CodeLen = str_length(m_aRoomCode);
	WriteString(vPacket, m_aRoomCode, CodeLen);
	int NickLen = minimum(str_length(m_aLocalNickname), MAX_NICKNAME_LEN);
	WriteU8(vPacket, static_cast<uint8_t>(NickLen));
	WriteBytes(vPacket, reinterpret_cast<const uint8_t *>(m_aLocalNickname), NickLen);
	WriteU64(vPacket, static_cast<uint64_t>(time(nullptr)));
	AppendAuth(vPacket);
	SendFrame(vPacket);
}

void CMultiMappingSession::SendHeartbeat()
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_HEARTBEAT);
	SendFrame(vPacket);
}

void CMultiMappingSession::SendCursor(float WorldX, float WorldY)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_CURSOR);
	WriteS32(vPacket, static_cast<int32_t>(WorldX * 1000.0f));
	WriteS32(vPacket, static_cast<int32_t>(WorldY * 1000.0f));
	SendFramePrio(vPacket);
}

void CMultiMappingSession::SendTileEdit(int GroupIdx, int LayerIdx, int TileX, int TileY, uint8_t Index, uint8_t Flags)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_TILE_EDIT);
	WriteS32(vPacket, GroupIdx);
	WriteS32(vPacket, LayerIdx);
	WriteS32(vPacket, TileX);
	WriteS32(vPacket, TileY);
	WriteU8(vPacket, Index);
	WriteU8(vPacket, Flags);
	// ExtraType=0: plain tile, no extra bytes
	WriteU8(vPacket, 0);
	SendFrame(vPacket);
}

void CMultiMappingSession::SendTileEditExtra(const STileEditEntry &Entry)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_TILE_EDIT);
	WriteS32(vPacket, Entry.m_GroupIdx);
	WriteS32(vPacket, Entry.m_LayerIdx);
	WriteS32(vPacket, Entry.m_TileX);
	WriteS32(vPacket, Entry.m_TileY);
	WriteU8(vPacket, Entry.m_Index);
	WriteU8(vPacket, Entry.m_Flags);
	WriteU8(vPacket, Entry.m_ExtraType);
	if(Entry.m_ExtraType == 1) // tele
	{
		WriteU8(vPacket, Entry.m_ExNumber);
		WriteU8(vPacket, Entry.m_ExType);
	}
	else if(Entry.m_ExtraType == 2) // speedup
	{
		WriteU8(vPacket, Entry.m_ExForce);
		WriteU8(vPacket, Entry.m_ExMaxSpeed);
		WriteU8(vPacket, (uint8_t)(Entry.m_ExAngle & 0xFF));
		WriteU8(vPacket, (uint8_t)((Entry.m_ExAngle >> 8) & 0xFF));
	}
	else if(Entry.m_ExtraType == 3) // switch
	{
		WriteU8(vPacket, Entry.m_ExNumber);
		WriteU8(vPacket, Entry.m_ExType);
		WriteU8(vPacket, Entry.m_ExSwitchFlags);
		WriteU8(vPacket, Entry.m_ExDelay);
	}
	else if(Entry.m_ExtraType == 4) // tune
	{
		WriteU8(vPacket, Entry.m_ExNumber);
		WriteU8(vPacket, Entry.m_ExType);
	}
	SendFrame(vPacket);
}

void CMultiMappingSession::FlushTileEdits()
{
	if(m_Socket == nullptr || m_vPendingTileEdits.empty())
		return;
	m_LastTileEditSentTime = time_get();
	for(const auto &Edit : m_vPendingTileEdits)
	{
		if(Edit.m_ExtraType == 0)
			SendTileEdit(Edit.m_GroupIdx, Edit.m_LayerIdx, Edit.m_TileX, Edit.m_TileY, Edit.m_Index, Edit.m_Flags);
		else
			SendTileEditExtra(Edit);
	}
	m_vPendingTileEdits.clear();
}

void CMultiMappingSession::SendGoodbye()
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_GOODBYE);
	SendFrame(vPacket);
}

void CMultiMappingSession::SendPing()
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_PING);
	SendFramePrio(vPacket);
	m_LastPingSentTime = time_get();
}

void CMultiMappingSession::AppendAuth(std::vector<uint8_t> &vPacket) const
{
	uint8_t aKey[32];
	DeobfuscateKey(aKey);

	uint8_t aKeyPad[64];
	memset(aKeyPad, 0, 64);
	memcpy(aKeyPad, aKey, 32);

	uint8_t aIpad[64], aOpad[64];
	for(int i = 0; i < 64; i++)
	{
		aIpad[i] = aKeyPad[i] ^ 0x36;
		aOpad[i] = aKeyPad[i] ^ 0x5c;
	}

	SHA256_CTX Ctx;
	sha256_init(&Ctx);
	sha256_update(&Ctx, aIpad, 64);
	sha256_update(&Ctx, vPacket.data(), vPacket.size());
	SHA256_DIGEST Inner = sha256_finish(&Ctx);

	sha256_init(&Ctx);
	sha256_update(&Ctx, aOpad, 64);
	sha256_update(&Ctx, Inner.data, 32);
	SHA256_DIGEST Hmac = sha256_finish(&Ctx);

	WriteBytes(vPacket, Hmac.data, 32);
}

void CMultiMappingSession::ProcessNetwork()
{
	if(m_Socket == nullptr)
		return;

	// grow buffer to fit incoming data
	const int ChunkSize = 4096;
	m_vRecvBuf.resize(m_RecvBufLen + ChunkSize);
	int Bytes = net_tcp_recv(m_Socket, m_vRecvBuf.data() + m_RecvBufLen, ChunkSize);
	if(Bytes > 0)
		m_RecvBufLen += Bytes;
	else if(Bytes == 0)
	{
		str_copy(m_aErrorMsg, "Connection closed");
		m_State = STATE_ERROR;
		CloseSocket();
		return;
	}
	// Bytes < 0: WOULDBLOCK РІР‚вЂќ normal for non-blocking

	int Offset = 0;
	while(Offset + 4 <= m_RecvBufLen)
	{
		uint32_t MsgLen = (static_cast<uint32_t>(m_vRecvBuf[Offset]) << 24) |
		                  (static_cast<uint32_t>(m_vRecvBuf[Offset + 1]) << 16) |
		                  (static_cast<uint32_t>(m_vRecvBuf[Offset + 2]) << 8) |
		                   static_cast<uint32_t>(m_vRecvBuf[Offset + 3]);
		if(MsgLen == 0)
		{
			m_RecvBufLen = 0;
			return;
		}
		if(MsgLen > 8 * 1024 * 1024)
		{
			str_copy(m_aErrorMsg, "Protocol error: oversized packet");
			m_State = STATE_ERROR;
			CloseSocket();
			return;
		}
		if(Offset + 4 + (int)MsgLen > m_RecvBufLen)
			break; // wait for more data
		HandleMessage(m_vRecvBuf.data() + Offset + 4, (int)MsgLen);
		Offset += 4 + (int)MsgLen;
		if(m_Socket == nullptr)
			break;
	}

	if(Offset > 0 && Offset < m_RecvBufLen)
	{
		memmove(m_vRecvBuf.data(), m_vRecvBuf.data() + Offset, m_RecvBufLen - Offset);
		m_RecvBufLen -= Offset;
	}
	else if(Offset >= m_RecvBufLen)
		m_RecvBufLen = 0;
}

static void WriteQuad(std::vector<uint8_t> &v, const CQuad &q)
{
	for(int i = 0; i < 5; i++) { MultiMappingProtocol::WriteS32(v, q.m_aPoints[i].x); MultiMappingProtocol::WriteS32(v, q.m_aPoints[i].y); }
	for(int i = 0; i < 4; i++) { MultiMappingProtocol::WriteS32(v, q.m_aColors[i].r); MultiMappingProtocol::WriteS32(v, q.m_aColors[i].g); MultiMappingProtocol::WriteS32(v, q.m_aColors[i].b); MultiMappingProtocol::WriteS32(v, q.m_aColors[i].a); }
	for(int i = 0; i < 4; i++) { MultiMappingProtocol::WriteS32(v, q.m_aTexcoords[i].x); MultiMappingProtocol::WriteS32(v, q.m_aTexcoords[i].y); }
	MultiMappingProtocol::WriteS32(v, q.m_PosEnv);
	MultiMappingProtocol::WriteS32(v, q.m_PosEnvOffset);
	MultiMappingProtocol::WriteS32(v, q.m_ColorEnv);
	MultiMappingProtocol::WriteS32(v, q.m_ColorEnvOffset);
}

static bool ReadQuad(MultiMappingProtocol::CPacketReader &r, CQuad &q)
{
	for(int i = 0; i < 5; i++) { q.m_aPoints[i].x = r.ReadS32(); q.m_aPoints[i].y = r.ReadS32(); }
	for(int i = 0; i < 4; i++) { q.m_aColors[i].r = r.ReadS32(); q.m_aColors[i].g = r.ReadS32(); q.m_aColors[i].b = r.ReadS32(); q.m_aColors[i].a = r.ReadS32(); }
	for(int i = 0; i < 4; i++) { q.m_aTexcoords[i].x = r.ReadS32(); q.m_aTexcoords[i].y = r.ReadS32(); }
	q.m_PosEnv = r.ReadS32();
	q.m_PosEnvOffset = r.ReadS32();
	q.m_ColorEnv = r.ReadS32();
	q.m_ColorEnvOffset = r.ReadS32();
	return true;
}

static void WriteSoundSource(std::vector<uint8_t> &v, const CSoundSource &s)
{
	MultiMappingProtocol::WriteS32(v, s.m_Position.x);
	MultiMappingProtocol::WriteS32(v, s.m_Position.y);
	MultiMappingProtocol::WriteS32(v, s.m_Loop);
	MultiMappingProtocol::WriteS32(v, s.m_Pan);
	MultiMappingProtocol::WriteS32(v, s.m_TimeDelay);
	MultiMappingProtocol::WriteS32(v, s.m_Falloff);
	MultiMappingProtocol::WriteS32(v, s.m_PosEnv);
	MultiMappingProtocol::WriteS32(v, s.m_PosEnvOffset);
	MultiMappingProtocol::WriteS32(v, s.m_SoundEnv);
	MultiMappingProtocol::WriteS32(v, s.m_SoundEnvOffset);
	MultiMappingProtocol::WriteU8(v, (uint8_t)s.m_Shape.m_Type);
	MultiMappingProtocol::WriteS32(v, s.m_Shape.m_Circle.m_Radius);
	MultiMappingProtocol::WriteS32(v, s.m_Shape.m_Rectangle.m_Height);
}

static bool ReadSoundSource(MultiMappingProtocol::CPacketReader &r, CSoundSource &s)
{
	s.m_Position.x = r.ReadS32(); s.m_Position.y = r.ReadS32();
	s.m_Loop = r.ReadS32(); s.m_Pan = r.ReadS32();
	s.m_TimeDelay = r.ReadS32(); s.m_Falloff = r.ReadS32();
	s.m_PosEnv = r.ReadS32(); s.m_PosEnvOffset = r.ReadS32();
	s.m_SoundEnv = r.ReadS32(); s.m_SoundEnvOffset = r.ReadS32();
	s.m_Shape.m_Type = r.ReadU8();
	s.m_Shape.m_Circle.m_Radius = r.ReadS32();
	s.m_Shape.m_Rectangle.m_Height = r.ReadS32();
	return true;
}

void CMultiMappingSession::HandleMessage(const uint8_t *pData, int Size)
{
	CPacketReader Reader(pData, Size);
	EPacketType Type;
	if(!Reader.ValidateHeader(&Type))
		return;

	m_LastServerPacketTime = time_get();

	if(Type >= PACKET_QUAD_ADD && Type <= PACKET_LAYER_FLAGS)
		m_DbgQuadRecv++;

	switch(Type)
	{
	case PACKET_HELLO_ACK:
	{
		uint16_t CodeLen = Reader.ReadU16();
		if(CodeLen > 0 && CodeLen <= ROOM_CODE_LEN)
		{
			Reader.ReadBytes(reinterpret_cast<uint8_t *>(m_aRoomCode), CodeLen);
			m_aRoomCode[CodeLen] = '\0';
		}
		m_LocalSlot = Reader.ReadU8();
		m_State = STATE_WAITING;
		if(m_IsCreator)
		{
			char aBuf[64];
			str_format(aBuf, sizeof(aBuf), "Room created: %s", m_aRoomCode);
			PushLog(aBuf);
		}
		else
		{
			PushLog("Waiting for session start...");
		}
		break;
	}
	case PACKET_ROOM_STATE:
	{
		m_ParticipantCount = Reader.ReadU8();
		uint8_t Live = Reader.ReadU8();
		if(Live)
		{
			m_State = STATE_LIVE;
			m_LastEnvUndoSize = (int)Editor()->Map()->m_EnvelopeEditorHistory.m_vpUndoActions.size();
			m_EnvDirty = false;
		}
		break;
	}
	case PACKET_ROSTER:
	{
		uint8_t Count = Reader.ReadU8();
		bool aSeen[MAX_MULTIMAPPING_PLAYERS] = {};
		for(uint8_t i = 0; i < Count; i++)
		{
			if(!Reader.HasBytes(4))
				break;
			uint8_t Slot = Reader.ReadU8();
			uint8_t IsCreator = Reader.ReadU8();
			uint8_t Connected = Reader.ReadU8();
			uint8_t NickLen = Reader.ReadU8();
			if(Slot >= MAX_MULTIMAPPING_PLAYERS || NickLen > MAX_NICKNAME_LEN || !Reader.HasBytes(NickLen))
				break;
			char aNick[MAX_NICKNAME_LEN + 1] = {};
			Reader.ReadBytes(reinterpret_cast<uint8_t *>(aNick), NickLen);
			aNick[NickLen] = '\0';

			aSeen[Slot] = true;
			bool WasConnected = m_aPeers[Slot].m_Connected;
			if(m_IsCreator && !WasConnected && Connected && Slot != m_LocalSlot)
			{
				bool AlreadyQueued = false;
				for(uint8_t QueuedSlot : m_vPendingLateJoinTargets)
				{
					if(QueuedSlot == Slot)
					{
						AlreadyQueued = true;
						break;
					}
				}
				if(!AlreadyQueued)
					m_vPendingLateJoinTargets.push_back(Slot);
			}
			if(Slot != m_LocalSlot && !WasConnected && Connected)
			{
				char aMsg[160];
				str_format(aMsg, sizeof(aMsg), "%s joined", aNick[0] ? aNick : "Player");
				PushLog(aMsg);
			}
			m_aPeers[Slot].m_Connected = Connected != 0;
			m_aPeers[Slot].m_IsCreator = IsCreator != 0;
			str_copy(m_aPeers[Slot].m_aNickname, aNick);
			if(!Connected)
				m_aPeers[Slot].m_HasCursor = false;
			// authority handoff: the server reassigns isCreator on disconnect РІР‚вЂќ
			// mirror it locally so TriggerEnvelopeResync/late-join logic follow
			if(Slot == m_LocalSlot && m_IsCreator != (IsCreator != 0))
			{
				m_IsCreator = IsCreator != 0;
				if(m_IsCreator)
					PushLog("You are now the room host");
			}
		}
		for(int Slot = 0; Slot < MAX_MULTIMAPPING_PLAYERS; Slot++)
		{
			if(!aSeen[Slot] && m_aPeers[Slot].m_Connected)
			{
				if(Slot != m_LocalSlot)
				{
					char aMsg[160];
					str_format(aMsg, sizeof(aMsg), "%s disconnected", m_aPeers[Slot].m_aNickname[0] ? m_aPeers[Slot].m_aNickname : "Player");
					PushLog(aMsg);
				}
				m_aPeers[Slot].m_Connected = false;
				m_aPeers[Slot].m_HasCursor = false;
			}
		}
		// "Alone in room" only means no OTHER peer is currently connected РІР‚вЂќ
		// with up to 8 players, one person leaving must not blank the status
		// for everyone else who is still here and mapping fine.
		bool AnyOtherConnected = false;
		for(int Slot = 0; Slot < MAX_MULTIMAPPING_PLAYERS; Slot++)
		{
			if(Slot != m_LocalSlot && m_aPeers[Slot].m_Connected)
			{
				AnyOtherConnected = true;
				break;
			}
		}
		m_RemoteDisconnected = !AnyOtherConnected;
		break;
	}
	case PACKET_START:
	{
		m_State = STATE_LIVE;
		m_LastEnvUndoSize = (int)Editor()->Map()->m_EnvelopeEditorHistory.m_vpUndoActions.size();
		m_EnvDirty = false;
		PushLog("Session started");
		if(m_IsCreator)
			StartMapTransfer();
		break;
	}
	case PACKET_CURSOR_RELAY:
	{
		uint8_t Slot = Reader.ReadU8();
		int32_t wx = Reader.ReadS32();
		int32_t wy = Reader.ReadS32();
		if(Slot >= MAX_MULTIMAPPING_PLAYERS)
			break;
		m_aPeers[Slot].m_CursorX = static_cast<float>(wx) / 1000.0f;
		m_aPeers[Slot].m_CursorY = static_cast<float>(wy) / 1000.0f;
		// Only show the cursor while that peer is actively mapping. A late
		// cursor relay must not resurrect the cursor we hid on activity change.
		m_aPeers[Slot].m_HasCursor = (m_aPeers[Slot].m_Activity == ACTIVITY_MAPPING);
		break;
	}
	case PACKET_TILE_RELAY:
	{
		int32_t GroupIdx = Reader.ReadS32();
		int32_t LayerIdx = Reader.ReadS32();
		int32_t TileX = Reader.ReadS32();
		int32_t TileY = Reader.ReadS32();
		uint8_t TileIndex = Reader.ReadU8();
		uint8_t TileFlags = Reader.ReadU8();
		uint8_t ExtraType = Reader.HasBytes(1) ? Reader.ReadU8() : 0;

		auto &vGroups = Editor()->Map()->m_vpGroups;
		if(GroupIdx >= 0 && GroupIdx < (int)vGroups.size())
		{
			auto &vLayers = vGroups[GroupIdx]->m_vpLayers;
			if(LayerIdx >= 0 && LayerIdx < (int)vLayers.size())
			{
				if(vLayers[LayerIdx]->m_Type == LAYERTYPE_TILES)
				{
					auto pTiles = std::static_pointer_cast<CLayerTiles>(vLayers[LayerIdx]);
					if(TileX >= 0 && TileX < pTiles->m_Width && TileY >= 0 && TileY < pTiles->m_Height)
					{
						int TgtIndex = TileY * pTiles->m_Width + TileX;
						CTile Tile;
						Tile.m_Index = TileIndex;
						Tile.m_Flags = TileFlags & 0x0F;
						Tile.m_Skip = 0;
						Tile.m_Reserved = 0;
						m_ApplyingRemote = true;
						if(ExtraType == 1) // tele
						{
							uint8_t Number = Reader.ReadU8();
							uint8_t TileType = Reader.ReadU8();
							auto pTele = std::dynamic_pointer_cast<CLayerTele>(pTiles);
							if(pTele)
							{
								pTele->m_pTeleTile[TgtIndex].m_Number = Number;
								pTele->m_pTeleTile[TgtIndex].m_Type = TileType;
								pTele->m_pTiles[TgtIndex] = Tile;
							}
						}
						else if(ExtraType == 2) // speedup
						{
							uint8_t Force = Reader.ReadU8();
							uint8_t MaxSpeed = Reader.ReadU8();
							uint8_t AngleLo = Reader.ReadU8();
							uint8_t AngleHi = Reader.ReadU8();
							int16_t Angle = (int16_t)(AngleLo | (AngleHi << 8));
							auto pSpeedup = std::dynamic_pointer_cast<CLayerSpeedup>(pTiles);
							if(pSpeedup)
							{
								pSpeedup->m_pSpeedupTile[TgtIndex].m_Force = Force;
								pSpeedup->m_pSpeedupTile[TgtIndex].m_MaxSpeed = MaxSpeed;
								pSpeedup->m_pSpeedupTile[TgtIndex].m_Angle = Angle;
								pSpeedup->m_pSpeedupTile[TgtIndex].m_Type = TileIndex;
								pSpeedup->m_pTiles[TgtIndex] = Tile;
							}
						}
						else if(ExtraType == 3) // switch
						{
							uint8_t Number = Reader.ReadU8();
							uint8_t TileType = Reader.ReadU8();
							uint8_t SwitchFlags = Reader.ReadU8();
							uint8_t Delay = Reader.ReadU8();
							auto pSwitch = std::dynamic_pointer_cast<CLayerSwitch>(pTiles);
							if(pSwitch)
							{
								pSwitch->m_pSwitchTile[TgtIndex].m_Number = Number;
								pSwitch->m_pSwitchTile[TgtIndex].m_Type = TileType;
								pSwitch->m_pSwitchTile[TgtIndex].m_Flags = SwitchFlags;
								pSwitch->m_pSwitchTile[TgtIndex].m_Delay = Delay;
								pSwitch->m_pTiles[TgtIndex] = Tile;
							}
						}
						else if(ExtraType == 4) // tune
						{
							uint8_t Number = Reader.ReadU8();
							uint8_t TileType = Reader.ReadU8();
							auto pTune = std::dynamic_pointer_cast<CLayerTune>(pTiles);
							if(pTune)
							{
								pTune->m_pTuneTile[TgtIndex].m_Number = Number;
								pTune->m_pTuneTile[TgtIndex].m_Type = TileType;
								pTune->m_pTiles[TgtIndex] = Tile;
							}
						}
						else
						{
							pTiles->SetTileIgnoreHistory(TileX, TileY, Tile);
						}
						m_ApplyingRemote = false;
						// measure e2e tile latency
						if(m_LastTileEditSentTime != 0)
						{
							int64_t Elapsed = time_get() - m_LastTileEditSentTime;
							int Ms = (int)(Elapsed * 1000 / time_freq());
							m_TileRelayLatencyMs = Ms;
						}
					}
				}
			}
		}
		break;
	}
	case PACKET_ERROR:
	{
		uint8_t Code = Reader.ReadU8();
		// Bad map rejection is non-fatal: the server refused to relay a
		// transfer that didn't look like a real map. Keep the session alive.
		if(Code == ERROR_BAD_MAP)
		{
			PushLog("Map transfer rejected (not a valid map)");
			break;
		}
		switch(Code)
		{
		case ERROR_ROOM_FULL: str_copy(m_aErrorMsg, "Room is full"); break;
		case ERROR_ROOM_NOT_FOUND: str_copy(m_aErrorMsg, "Room not found"); break;
		case ERROR_AUTH_FAILED: str_copy(m_aErrorMsg, "Auth failed"); break;
		case ERROR_RATE_LIMITED: str_copy(m_aErrorMsg, "Rate limited"); break;
		default: str_copy(m_aErrorMsg, "Unknown error"); break;
		}
		PushLog(m_aErrorMsg);
		m_State = STATE_ERROR;
		CloseSocket();
		break;
	}
	case PACKET_SYNC_CHECK:
	{
		// partner finished a stroke РІР‚вЂќ check if our layer matches their CRC
		int32_t GroupIdx = Reader.ReadS32();
		int32_t LayerIdx = Reader.ReadS32();
		uint32_t TheirCrc = Reader.ReadU32();

		auto &vGroups = Editor()->Map()->m_vpGroups;
		if(GroupIdx < 0 || GroupIdx >= (int)vGroups.size())
			break;
		auto &vLayers = vGroups[GroupIdx]->m_vpLayers;
		if(LayerIdx < 0 || LayerIdx >= (int)vLayers.size())
			break;
		if(vLayers[LayerIdx]->m_Type != LAYERTYPE_TILES)
			break;
		auto pTiles = std::static_pointer_cast<CLayerTiles>(vLayers[LayerIdx]);
		int Count = pTiles->m_Width * pTiles->m_Height * (int)sizeof(CTile);
		uint32_t OurCrc = CalcLayerCRC(reinterpret_cast<const uint8_t *>(pTiles->m_pTiles), Count);

		if(auto pTele = std::dynamic_pointer_cast<CLayerTele>(pTiles))
		{
			int ExCount = pTele->m_Width * pTele->m_Height * (int)sizeof(CTeleTile);
			OurCrc = CalcLayerCRC(reinterpret_cast<const uint8_t *>(pTele->m_pTeleTile), ExCount, OurCrc);
		}
		else if(auto pSpeedup = std::dynamic_pointer_cast<CLayerSpeedup>(pTiles))
		{
			int ExCount = pSpeedup->m_Width * pSpeedup->m_Height * (int)sizeof(CSpeedupTile);
			OurCrc = CalcLayerCRC(reinterpret_cast<const uint8_t *>(pSpeedup->m_pSpeedupTile), ExCount, OurCrc);
		}
		else if(auto pSwitch = std::dynamic_pointer_cast<CLayerSwitch>(pTiles))
		{
			int ExCount = pSwitch->m_Width * pSwitch->m_Height * (int)sizeof(CSwitchTile);
			OurCrc = CalcLayerCRC(reinterpret_cast<const uint8_t *>(pSwitch->m_pSwitchTile), ExCount, OurCrc);
		}
		else if(auto pTune = std::dynamic_pointer_cast<CLayerTune>(pTiles))
		{
			int ExCount = pTune->m_Width * pTune->m_Height * (int)sizeof(CTuneTile);
			OurCrc = CalcLayerCRC(reinterpret_cast<const uint8_t *>(pTune->m_pTuneTile), ExCount, OurCrc);
		}

		if(OurCrc != TheirCrc)
		{
			// defer by 500ms РІР‚вЂќ TILE_RELAY packets for this stroke may still be in flight
			int64_t RequestAfter = time_get() + time_freq() / 2;
			bool Found = false;
			for(auto &Req : m_vPendingSyncRequests)
			{
				if(Req.m_GroupIdx == GroupIdx && Req.m_LayerIdx == LayerIdx)
				{
					Req.m_RequestAfter = RequestAfter;
					Found = true;
					break;
				}
			}
			if(!Found)
				m_vPendingSyncRequests.push_back({GroupIdx, LayerIdx, RequestAfter});
		}
		break;
	}
	case PACKET_SYNC_REQUEST:
	{
		// partner detected desync РІР‚вЂќ queue for OnBackgroundUpdate() to avoid large sends in recv path
		int32_t GroupIdx = Reader.ReadS32();
		int32_t LayerIdx = Reader.ReadS32();
		bool Found = false;
		for(auto &Resp : m_vPendingSyncResponses)
		{
			if(Resp.m_GroupIdx == GroupIdx && Resp.m_LayerIdx == LayerIdx)
			{
				Found = true;
				break;
			}
		}
		if(!Found)
			m_vPendingSyncResponses.push_back({GroupIdx, LayerIdx});
		break;
	}
	case PACKET_SYNC_DATA:
	{
		// one row of full layer dump from partner РІР‚вЂќ apply it
		int32_t GroupIdx = Reader.ReadS32();
		int32_t LayerIdx = Reader.ReadS32();
		int32_t Width    = Reader.ReadS32();
		int32_t Height   = Reader.ReadS32();
		int32_t Row      = Reader.ReadS32();
		// Validate wire-supplied dimensions BEFORE deriving any byte count from
		// them РІР‚вЂќ Width * sizeof(CTile) would overflow int for a hostile Width.
		if(Width <= 0 || Width > 10000 || Height <= 0 || Height > 10000 || Row < 0 || Row >= Height)
			break;

		// ExtraType byte: 0=CTile row, 1=tele, 2=speedup, 3=switch, 4=tune
		uint8_t ExtraType = Reader.HasBytes(1) ? Reader.ReadU8() : 0;

		auto &vGroups = Editor()->Map()->m_vpGroups;
		if(GroupIdx < 0 || GroupIdx >= (int)vGroups.size())
			break;
		auto &vLayers = vGroups[GroupIdx]->m_vpLayers;
		if(LayerIdx < 0 || LayerIdx >= (int)vLayers.size())
			break;
		if(vLayers[LayerIdx]->m_Type != LAYERTYPE_TILES)
			break;
		auto pTiles = std::static_pointer_cast<CLayerTiles>(vLayers[LayerIdx]);
		if(pTiles->m_Width != Width || pTiles->m_Height != Height)
			break;

		if(ExtraType == 0) // CTile row
		{
			int RowBytes = Width * (int)sizeof(CTile);
			if(!Reader.HasBytes(RowBytes))
				break;
			Reader.ReadBytes(reinterpret_cast<uint8_t *>(pTiles->m_pTiles + Row * Width), RowBytes);
		}
		else if(ExtraType == 1) // tele
		{
			auto pTele = std::dynamic_pointer_cast<CLayerTele>(pTiles);
			if(!pTele) break;
			int RowBytes = Width * (int)sizeof(CTeleTile);
			if(!Reader.HasBytes(RowBytes)) break;
			Reader.ReadBytes(reinterpret_cast<uint8_t *>(pTele->m_pTeleTile + Row * Width), RowBytes);
		}
		else if(ExtraType == 2) // speedup
		{
			auto pSpeedup = std::dynamic_pointer_cast<CLayerSpeedup>(pTiles);
			if(!pSpeedup) break;
			int RowBytes = Width * (int)sizeof(CSpeedupTile);
			if(!Reader.HasBytes(RowBytes)) break;
			Reader.ReadBytes(reinterpret_cast<uint8_t *>(pSpeedup->m_pSpeedupTile + Row * Width), RowBytes);
		}
		else if(ExtraType == 3) // switch
		{
			auto pSwitch = std::dynamic_pointer_cast<CLayerSwitch>(pTiles);
			if(!pSwitch) break;
			int RowBytes = Width * (int)sizeof(CSwitchTile);
			if(!Reader.HasBytes(RowBytes)) break;
			Reader.ReadBytes(reinterpret_cast<uint8_t *>(pSwitch->m_pSwitchTile + Row * Width), RowBytes);
		}
		else if(ExtraType == 4) // tune
		{
			auto pTune = std::dynamic_pointer_cast<CLayerTune>(pTiles);
			if(!pTune) break;
			int RowBytes = Width * (int)sizeof(CTuneTile);
			if(!Reader.HasBytes(RowBytes)) break;
			Reader.ReadBytes(reinterpret_cast<uint8_t *>(pTune->m_pTuneTile + Row * Width), RowBytes);
		}
		break;
	}
	case PACKET_STRUCT_ADD_GROUP:
	{
		if(!Reader.HasBytes(4))
			break;
		int InsertIdx = Reader.ReadS32();
		m_ApplyingRemote = true;
		Editor()->Map()->NewGroup();
		int NewIdx = (int)Editor()->Map()->m_vpGroups.size() - 1;
		if(InsertIdx >= 0 && InsertIdx < NewIdx)
			Editor()->Map()->MoveGroup(NewIdx, InsertIdx);
		else if(InsertIdx > NewIdx)
			PushLog("MultiMapping: group index mismatch (map drifted out of sync)");
		Editor()->Map()->m_SelectedGroup = InsertIdx >= 0 ? InsertIdx : NewIdx;
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_STRUCT_DEL_GROUP:
	{
		if(!Reader.HasBytes(4))
			break;
		int GroupIdx = Reader.ReadS32();
		auto &vGroups = Editor()->Map()->m_vpGroups;
		if(GroupIdx < 0 || GroupIdx >= (int)vGroups.size())
			break;
		if(vGroups[GroupIdx] == Editor()->Map()->m_pGameGroup)
			break;
		m_ApplyingRemote = true;
		// Null any special-layer map pointers owned by layers in this group,
		// otherwise DeleteGroup leaves dangling references (DeleteGroup does no
		// such cleanup, unlike the DEL_LAYER path).
		for(auto &pLayer : vGroups[GroupIdx]->m_vpLayers)
		{
			if(pLayer->m_Type != LAYERTYPE_TILES)
				continue;
			auto pTiles = std::static_pointer_cast<CLayerTiles>(pLayer);
			if(pTiles->m_HasFront)        Editor()->Map()->m_pFrontLayer   = nullptr;
			else if(pTiles->m_HasTele)    Editor()->Map()->m_pTeleLayer    = nullptr;
			else if(pTiles->m_HasSpeedup) Editor()->Map()->m_pSpeedupLayer = nullptr;
			else if(pTiles->m_HasSwitch)  Editor()->Map()->m_pSwitchLayer  = nullptr;
			else if(pTiles->m_HasTune)    Editor()->Map()->m_pTuneLayer    = nullptr;
		}
		// NOTE: do NOT record into the undo history (see DEL_LAYER above) РІР‚вЂќ
		// a joiner-side undo of a remote delete would echo back to the owner.
		Editor()->Map()->DeleteGroup(GroupIdx);
		Editor()->Map()->m_SelectedGroup = maximum(0, GroupIdx - 1);
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_STRUCT_ADD_LAYER:
	{
		if(!Reader.HasBytes(10))
			break;
		int GroupIdx  = Reader.ReadS32();
		int LayerIdx  = Reader.ReadS32();
		int LayerType = Reader.ReadU8();
		int SubType   = Reader.ReadU8();
		auto &vGroups = Editor()->Map()->m_vpGroups;
		if(GroupIdx < 0 || GroupIdx >= (int)vGroups.size())
			break;
		m_ApplyingRemote = true;
		Editor()->Map()->m_SelectedGroup = GroupIdx;
		if(LayerType == LAYERTYPE_TILES)
		{
			switch(SubType)
			{
			case 1: Editor()->AddFrontLayer();   break;
			case 2: Editor()->AddTeleLayer();    break;
			case 3: Editor()->AddSpeedupLayer(); break;
			case 4: Editor()->AddSwitchLayer();  break;
			case 5: Editor()->AddTuneLayer();    break;
			default: Editor()->AddTileLayer();   break;
			}
		}
		else if(LayerType == LAYERTYPE_QUADS)
			Editor()->AddQuadsLayer();
		else if(LayerType == LAYERTYPE_SOUNDS)
			Editor()->AddSoundLayer();
		// The Add*Layer helpers always append; move the new layer to the index
		// the sender used, mirroring the ADD_GROUP handler. Without this, every
		// subsequent index-addressed packet hits the wrong layer on the receiver.
		{
			int NewIdx = (int)vGroups[GroupIdx]->m_vpLayers.size() - 1;
			if(LayerIdx >= 0 && LayerIdx < NewIdx)
				vGroups[GroupIdx]->MoveLayer(NewIdx, LayerIdx);
		}
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_STRUCT_DEL_LAYER:
	{
		if(!Reader.HasBytes(8))
			break;
		int GroupIdx = Reader.ReadS32();
		int LayerIdx = Reader.ReadS32();
		auto &vGroups = Editor()->Map()->m_vpGroups;
		if(GroupIdx < 0 || GroupIdx >= (int)vGroups.size())
			break;
		auto &vLayers = vGroups[GroupIdx]->m_vpLayers;
		if(LayerIdx < 0 || LayerIdx >= (int)vLayers.size())
			break;
		m_ApplyingRemote = true;
		Editor()->Map()->m_SelectedGroup = GroupIdx;
		Editor()->Map()->SelectLayer(LayerIdx);
		// Reset special layer pointers before deletion to avoid dangling references
		auto pLayer = vLayers[LayerIdx];
		if(pLayer->m_Type == LAYERTYPE_TILES)
		{
			auto pTiles = std::static_pointer_cast<CLayerTiles>(pLayer);
			if(pTiles->m_HasFront)        Editor()->Map()->m_pFrontLayer   = nullptr;
			else if(pTiles->m_HasTele)    Editor()->Map()->m_pTeleLayer    = nullptr;
			else if(pTiles->m_HasSpeedup) Editor()->Map()->m_pSpeedupLayer = nullptr;
			else if(pTiles->m_HasSwitch)  Editor()->Map()->m_pSwitchLayer  = nullptr;
			else if(pTiles->m_HasTune)    Editor()->Map()->m_pTuneLayer    = nullptr;
		}
		// NOTE: do NOT record this into the undo history. A remote structural
		// delete is owned by the initiator; if the joiner could undo it, the
		// action's Undo()/Redo() would fire NotifyAddLayer/NotifyDelLayer back
		// to the owner (m_ApplyingRemote is false at undo time), creating a
		// duplicate layer and then an out-of-bounds delete on the owner РІР‚вЂќ a crash.
		vGroups[GroupIdx]->DeleteLayer(LayerIdx);
		Editor()->Map()->SelectPreviousLayer();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_STRUCT_SET_IMAGE:
	{
		if(!Reader.HasBytes(12))
			break;
		int GroupIdx = Reader.ReadS32();
		int LayerIdx = Reader.ReadS32();
		int ImageIdx = Reader.ReadS32();
		auto &vGroups = Editor()->Map()->m_vpGroups;
		if(GroupIdx < 0 || GroupIdx >= (int)vGroups.size())
			break;
		auto &vLayers = vGroups[GroupIdx]->m_vpLayers;
		if(LayerIdx < 0 || LayerIdx >= (int)vLayers.size())
			break;
		if(ImageIdx < -1 || ImageIdx >= (int)Editor()->Map()->m_vpImages.size())
			break;
		m_ApplyingRemote = true;
		if(vLayers[LayerIdx]->m_Type == LAYERTYPE_TILES)
			std::static_pointer_cast<CLayerTiles>(vLayers[LayerIdx])->m_Image = ImageIdx;
		else if(vLayers[LayerIdx]->m_Type == LAYERTYPE_QUADS)
			std::static_pointer_cast<CLayerQuads>(vLayers[LayerIdx])->m_Image = ImageIdx;
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_STRUCT_RENAME_GROUP:
	{
		if(!Reader.HasBytes(6))
			break;
		int GroupIdx = Reader.ReadS32();
		uint16_t NameLen = Reader.ReadU16();
		auto &vGroups = Editor()->Map()->m_vpGroups;
		if(GroupIdx < 0 || GroupIdx >= (int)vGroups.size())
			break;
		if(!Reader.HasBytes(NameLen))
			break;
		char aName[128] = {};
		int CopyLen = minimum((int)NameLen, (int)sizeof(aName) - 1);
		Reader.ReadBytes(reinterpret_cast<uint8_t *>(aName), CopyLen);
		m_ApplyingRemote = true;
		str_copy(vGroups[GroupIdx]->m_aName, aName, sizeof(vGroups[GroupIdx]->m_aName));
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_STRUCT_RENAME_LAYER:
	{
		if(!Reader.HasBytes(10))
			break;
		int GroupIdx = Reader.ReadS32();
		int LayerIdx = Reader.ReadS32();
		uint16_t NameLen = Reader.ReadU16();
		auto &vGroups = Editor()->Map()->m_vpGroups;
		if(GroupIdx < 0 || GroupIdx >= (int)vGroups.size())
			break;
		auto &vLayers = vGroups[GroupIdx]->m_vpLayers;
		if(LayerIdx < 0 || LayerIdx >= (int)vLayers.size())
			break;
		if(!Reader.HasBytes(NameLen))
			break;
		char aName[128] = {};
		int CopyLen = minimum((int)NameLen, (int)sizeof(aName) - 1);
		Reader.ReadBytes(reinterpret_cast<uint8_t *>(aName), CopyLen);
		m_ApplyingRemote = true;
		str_copy(vLayers[LayerIdx]->m_aName, aName, sizeof(vLayers[LayerIdx]->m_aName));
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_STRUCT_LAYER_PROP:
	{
		if(!Reader.HasBytes(13))
			break;
		int GroupIdx = Reader.ReadS32();
		int LayerIdx = Reader.ReadS32();
		int PropId = Reader.ReadU8();
		int Value = Reader.ReadS32();
		auto &vGroups = Editor()->Map()->m_vpGroups;
		if(GroupIdx < 0 || GroupIdx >= (int)vGroups.size())
			break;
		auto &vLayers = vGroups[GroupIdx]->m_vpLayers;
		if(LayerIdx < 0 || LayerIdx >= (int)vLayers.size())
			break;
		// Handle generic layer props (ORDER, HQ) before the tiles-only path
		// These props are only sent for non-tiles layers; tiles use ETilesProp values.
		if(vLayers[LayerIdx]->m_Type != LAYERTYPE_TILES)
		{
			if(PropId == (int)ELayerProp::ORDER)
			{
				m_ApplyingRemote = true;
				int NewIdx = Value;
				if(NewIdx >= 0 && NewIdx < (int)vLayers.size())
					vGroups[GroupIdx]->MoveLayer(LayerIdx, NewIdx);
				Editor()->Map()->OnModify();
				m_ApplyingRemote = false;
				break;
			}
			if(PropId == (int)ELayerProp::HQ)
			{
				m_ApplyingRemote = true;
				vLayers[LayerIdx]->m_Flags = Value;
				Editor()->Map()->OnModify();
				m_ApplyingRemote = false;
				break;
			}
			break; // unknown prop for non-tiles layer
		}
		auto pTiles = std::static_pointer_cast<CLayerTiles>(vLayers[LayerIdx]);
		m_ApplyingRemote = true;
		ETilesProp Prop = static_cast<ETilesProp>(PropId);
		if(Prop == ETilesProp::WIDTH)
		{
			if(Value <= 0 || Value > 10000)
			{
				m_ApplyingRemote = false;
				break;
			}
			pTiles->Resize(Value, pTiles->m_Height);
		}
		else if(Prop == ETilesProp::HEIGHT)
		{
			if(Value <= 0 || Value > 10000)
			{
				m_ApplyingRemote = false;
				break;
			}
			pTiles->Resize(pTiles->m_Width, Value);
		}
		else if(Prop == ETilesProp::COLOR)
			pTiles->m_Color = UnpackColor(Value);
		else if(Prop == ETilesProp::AUTOMAPPER)
		{
			if(pTiles->m_Image >= 0 && (int)Editor()->Map()->m_vpImages.size() > pTiles->m_Image &&
				Editor()->Map()->m_vpImages[pTiles->m_Image]->m_AutoMapper.ConfigNamesNum() > 0 && Value >= 0)
				pTiles->m_AutoMapperConfig = Value % Editor()->Map()->m_vpImages[pTiles->m_Image]->m_AutoMapper.ConfigNamesNum();
			else
				pTiles->m_AutoMapperConfig = -1;
		}
		else if(Prop == ETilesProp::SEED)
			pTiles->m_Seed = Value;
		else if(Prop == ETilesProp::COLOR_ENV)
			pTiles->m_ColorEnv = Value;
		else if(Prop == ETilesProp::COLOR_ENV_OFFSET)
			pTiles->m_ColorEnvOffset = Value;
		else if(Prop == ETilesProp::LIVE_GAMETILES)
			pTiles->m_LiveGameTiles = Value != 0;
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_STRUCT_ADD_IMAGE:
	{
		if(!Reader.HasBytes(4))
			break;
		uint8_t External = Reader.ReadU8();
		uint16_t NameLen = Reader.ReadU16();
		if(!Reader.HasBytes(NameLen))
			break;
		char aName[128] = {};
		int CopyLen = minimum((int)NameLen, (int)sizeof(aName) - 1);
		Reader.ReadBytes(reinterpret_cast<uint8_t *>(aName), CopyLen);
		// skip remaining name bytes if truncated
		if((int)NameLen > CopyLen)
		{
			uint8_t aDummy[1];
			for(int i = CopyLen; i < (int)NameLen; i++)
				Reader.ReadBytes(aDummy, 1);
		}
		if(!Reader.HasBytes(4))
			break;
		int DataSize = Reader.ReadS32();
		// check if image with this name already exists
		for(const auto &pImg : Editor()->Map()->m_vpImages)
		{
			if(!str_comp(pImg->m_aName, aName))
				goto skip_add_image;
		}
		if(External)
		{
			// external image РІР‚вЂќ load from local mapres/
			auto pImg = std::make_shared<CEditorImage>(Editor()->Map());
			str_copy(pImg->m_aName, aName, sizeof(pImg->m_aName));
			pImg->m_External = 1;
			char aBuf[IO_MAX_PATH_LENGTH];
			str_format(aBuf, sizeof(aBuf), "mapres/%s.png", aName);
			CImageInfo ImgInfo;
			if(Editor()->Graphics()->LoadPng(ImgInfo, aBuf, IStorage::TYPE_ALL))
			{
				pImg->m_Width = ImgInfo.m_Width;
				pImg->m_Height = ImgInfo.m_Height;
				pImg->m_Format = ImgInfo.m_Format;
				pImg->m_pData = ImgInfo.m_pData;
				ConvertToRgba(*pImg);
				int TexFlag = Editor()->Graphics()->Uses2DTextureArrays() ? IGraphics::TEXLOAD_TO_2D_ARRAY_TEXTURE : IGraphics::TEXLOAD_TO_3D_TEXTURE;
				if(pImg->m_Width % 16 != 0 || pImg->m_Height % 16 != 0)
					TexFlag = 0;
				pImg->m_Texture = Editor()->Graphics()->LoadTextureRaw(*pImg, TexFlag, aBuf);
			}
			pImg->m_AutoMapper.Load(pImg->m_aName);
			m_ApplyingRemote = true;
			Editor()->Map()->m_vpImages.push_back(pImg);
			Editor()->Map()->SortImages();
			m_ApplyingRemote = false;
		}
		else if(DataSize > 0 && Reader.HasBytes(DataSize))
		{
			std::vector<uint8_t> vData(DataSize);
			Reader.ReadBytes(vData.data(), DataSize);
			// decode PNG from memory
			CImageInfo ImgInfo;
			if(Editor()->Graphics()->LoadPng(ImgInfo, vData.data(), (size_t)DataSize, aName))
			{
				auto pImg = std::make_shared<CEditorImage>(Editor()->Map());
				pImg->m_Width = ImgInfo.m_Width;
				pImg->m_Height = ImgInfo.m_Height;
				pImg->m_Format = ImgInfo.m_Format;
				pImg->m_pData = ImgInfo.m_pData;
				pImg->m_External = 0;
				str_copy(pImg->m_aName, aName, sizeof(pImg->m_aName));
				ConvertToRgba(*pImg);
				DilateImage(*pImg);
				int TexFlag = Editor()->Graphics()->Uses2DTextureArrays() ? IGraphics::TEXLOAD_TO_2D_ARRAY_TEXTURE : IGraphics::TEXLOAD_TO_3D_TEXTURE;
				if(pImg->m_Width % 16 != 0 || pImg->m_Height % 16 != 0)
					TexFlag = 0;
				pImg->m_Texture = Editor()->Graphics()->LoadTextureRaw(*pImg, TexFlag, aName);
				pImg->m_AutoMapper.Load(pImg->m_aName);
				m_ApplyingRemote = true;
				Editor()->Map()->m_vpImages.push_back(pImg);
				Editor()->Map()->SortImages();
				m_ApplyingRemote = false;
			}
		}
		skip_add_image:;
		break;
	}
	case PACKET_STRUCT_DEL_IMAGE:
	{
		if(!Reader.HasBytes(4))
			break;
		int ImageIdx = Reader.ReadS32();
		if(ImageIdx < 0 || ImageIdx >= (int)Editor()->Map()->m_vpImages.size())
			break;
		m_ApplyingRemote = true;
		Editor()->Map()->m_vpImages.erase(Editor()->Map()->m_vpImages.begin() + ImageIdx);
		Editor()->Map()->ModifyImageIndex([ImageIdx](int *pIndex) {
			if(*pIndex == ImageIdx) *pIndex = -1;
			else if(*pIndex > ImageIdx) *pIndex = *pIndex - 1;
		});
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_STRUCT_EMBED_IMAGE:
	{
		if(!Reader.HasBytes(8))
			break;
		int ImageIdx = Reader.ReadS32();
		int DataSize = Reader.ReadS32();
		if(ImageIdx < 0 || ImageIdx >= (int)Editor()->Map()->m_vpImages.size())
			break;
		if(DataSize <= 0 || !Reader.HasBytes(DataSize))
			break;
		std::vector<uint8_t> vData(DataSize);
		Reader.ReadBytes(vData.data(), DataSize);
		auto pImg = Editor()->Map()->m_vpImages[ImageIdx];
		CImageInfo ImgInfo;
		if(Editor()->Graphics()->LoadPng(ImgInfo, vData.data(), (size_t)DataSize, pImg->m_aName))
		{
			pImg->CEditorImage::Free();
			pImg->m_Width = ImgInfo.m_Width;
			pImg->m_Height = ImgInfo.m_Height;
			pImg->m_Format = ImgInfo.m_Format;
			pImg->m_pData = ImgInfo.m_pData;
			ConvertToRgba(*pImg);
			DilateImage(*pImg);
			int TexFlag = Editor()->Graphics()->Uses2DTextureArrays() ? IGraphics::TEXLOAD_TO_2D_ARRAY_TEXTURE : IGraphics::TEXLOAD_TO_3D_TEXTURE;
			if(pImg->m_Width % 16 != 0 || pImg->m_Height % 16 != 0)
				TexFlag = 0;
			pImg->m_Texture = Editor()->Graphics()->LoadTextureRaw(*pImg, TexFlag, pImg->m_aName);
			m_ApplyingRemote = true;
			pImg->m_External = 0;
			Editor()->Map()->OnModify();
			m_ApplyingRemote = false;
		}
		break;
	}
	case PACKET_STRUCT_EXTERN_IMAGE:
	{
		if(!Reader.HasBytes(4))
			break;
		int ImageIdx = Reader.ReadS32();
		if(ImageIdx < 0 || ImageIdx >= (int)Editor()->Map()->m_vpImages.size())
			break;
		m_ApplyingRemote = true;
		Editor()->Map()->m_vpImages[ImageIdx]->m_External = 1;
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_STRUCT_ADD_SOUND:
	{
		if(!Reader.HasBytes(2))
			break;
		uint16_t NameLen = Reader.ReadU16();
		if(!Reader.HasBytes(NameLen))
			break;
		char aName[128] = {};
		int CopyLen = minimum((int)NameLen, (int)sizeof(aName) - 1);
		Reader.ReadBytes(reinterpret_cast<uint8_t *>(aName), CopyLen);
		if((int)NameLen > CopyLen)
		{
			uint8_t aDummy[1];
			for(int i = CopyLen; i < (int)NameLen; i++)
				Reader.ReadBytes(aDummy, 1);
		}
		if(!Reader.HasBytes(4))
			break;
		int DataSize = Reader.ReadS32();
		if(DataSize <= 0 || !Reader.HasBytes(DataSize))
			break;
		// skip if sound with this name already exists
		for(const auto &pSnd : Editor()->Map()->m_vpSounds)
		{
			if(!str_comp(pSnd->m_aName, aName))
				goto skip_add_sound;
		}
		{
			void *pSoundData = malloc(DataSize);
			if(!pSoundData)
				break;
			Reader.ReadBytes(reinterpret_cast<uint8_t *>(pSoundData), DataSize);
			const int SoundId = Editor()->Sound()->LoadOpusFromMem(pSoundData, (unsigned)DataSize, true, aName);
			if(SoundId == -1)
			{
				free(pSoundData);
				break;
			}
			auto pSound = std::make_shared<CEditorSound>(Editor()->Map());
			pSound->m_SoundId = SoundId;
			pSound->m_pData = pSoundData;
			pSound->m_DataSize = (unsigned)DataSize;
			str_copy(pSound->m_aName, aName, sizeof(pSound->m_aName));
			m_ApplyingRemote = true;
			Editor()->Map()->m_vpSounds.push_back(pSound);
			Editor()->Map()->OnModify();
			m_ApplyingRemote = false;
		}
		skip_add_sound:;
		break;
	}
	case PACKET_STRUCT_DEL_SOUND:
	{
		if(!Reader.HasBytes(4))
			break;
		int SoundIdx = Reader.ReadS32();
		if(SoundIdx < 0 || SoundIdx >= (int)Editor()->Map()->m_vpSounds.size())
			break;
		m_ApplyingRemote = true;
		Editor()->Map()->m_vpSounds.erase(Editor()->Map()->m_vpSounds.begin() + SoundIdx);
		Editor()->Map()->ModifySoundIndex([SoundIdx](int *pIndex) {
			if(*pIndex == SoundIdx) *pIndex = -1;
			else if(*pIndex > SoundIdx) *pIndex = *pIndex - 1;
		});
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_STRUCT_SET_SOUND:
	{
		if(!Reader.HasBytes(12)) break;
		int GroupIdx = Reader.ReadS32();
		int LayerIdx = Reader.ReadS32();
		int SoundIdx = Reader.ReadS32();
		auto &vGroups = Editor()->Map()->m_vpGroups;
		if(GroupIdx < 0 || GroupIdx >= (int)vGroups.size()) break;
		auto &vLayers = vGroups[GroupIdx]->m_vpLayers;
		if(LayerIdx < 0 || LayerIdx >= (int)vLayers.size()) break;
		if(vLayers[LayerIdx]->m_Type != LAYERTYPE_SOUNDS) break;
		if(SoundIdx < -1 || SoundIdx >= (int)Editor()->Map()->m_vpSounds.size()) break;
		m_ApplyingRemote = true;
		std::static_pointer_cast<CLayerSounds>(vLayers[LayerIdx])->m_Sound = SoundIdx;
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_STRUCT_ADD_SOUND_SOURCE:
	{
		if(!Reader.HasBytes(12)) break;
		int GroupIdx = Reader.ReadS32();
		int LayerIdx = Reader.ReadS32();
		Reader.ReadS32(); // SourceIdx hint РІР‚вЂќ not needed, we always append
		auto &vGroups = Editor()->Map()->m_vpGroups;
		if(GroupIdx < 0 || GroupIdx >= (int)vGroups.size()) break;
		auto &vLayers = vGroups[GroupIdx]->m_vpLayers;
		if(LayerIdx < 0 || LayerIdx >= (int)vLayers.size()) break;
		if(vLayers[LayerIdx]->m_Type != LAYERTYPE_SOUNDS) break;
		CSoundSource Src = {};
		if(!ReadSoundSource(Reader, Src)) break;
		m_ApplyingRemote = true;
		std::static_pointer_cast<CLayerSounds>(vLayers[LayerIdx])->m_vSources.push_back(Src);
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_STRUCT_DEL_SOUND_SOURCE:
	{
		if(!Reader.HasBytes(12)) break;
		int GroupIdx = Reader.ReadS32();
		int LayerIdx = Reader.ReadS32();
		int SourceIdx = Reader.ReadS32();
		auto &vGroups = Editor()->Map()->m_vpGroups;
		if(GroupIdx < 0 || GroupIdx >= (int)vGroups.size()) break;
		auto &vLayers = vGroups[GroupIdx]->m_vpLayers;
		if(LayerIdx < 0 || LayerIdx >= (int)vLayers.size()) break;
		if(vLayers[LayerIdx]->m_Type != LAYERTYPE_SOUNDS) break;
		auto pSounds = std::static_pointer_cast<CLayerSounds>(vLayers[LayerIdx]);
		if(SourceIdx < 0 || SourceIdx >= (int)pSounds->m_vSources.size()) break;
		m_ApplyingRemote = true;
		pSounds->m_vSources.erase(pSounds->m_vSources.begin() + SourceIdx);
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_STRUCT_EDIT_SOUND_SOURCE:
	{
		if(!Reader.HasBytes(13)) break;
		int GroupIdx = Reader.ReadS32();
		int LayerIdx = Reader.ReadS32();
		int SourceIdx = Reader.ReadS32();
		int PropId = Reader.ReadU8();
		int Value = Reader.ReadS32();
		auto &vGroups = Editor()->Map()->m_vpGroups;
		if(GroupIdx < 0 || GroupIdx >= (int)vGroups.size()) break;
		auto &vLayers = vGroups[GroupIdx]->m_vpLayers;
		if(LayerIdx < 0 || LayerIdx >= (int)vLayers.size()) break;
		if(vLayers[LayerIdx]->m_Type != LAYERTYPE_SOUNDS) break;
		auto pSounds = std::static_pointer_cast<CLayerSounds>(vLayers[LayerIdx]);
		if(SourceIdx < 0 || SourceIdx >= (int)pSounds->m_vSources.size()) break;
		CSoundSource &Src = pSounds->m_vSources[SourceIdx];
		m_ApplyingRemote = true;
		ESoundProp Prop = (ESoundProp)PropId;
		if(Prop == ESoundProp::POS_X) Src.m_Position.x = Value;
		else if(Prop == ESoundProp::POS_Y) Src.m_Position.y = Value;
		else if(Prop == ESoundProp::LOOP) Src.m_Loop = Value;
		else if(Prop == ESoundProp::PAN) Src.m_Pan = Value;
		else if(Prop == ESoundProp::TIME_DELAY) Src.m_TimeDelay = Value;
		else if(Prop == ESoundProp::FALLOFF) Src.m_Falloff = Value;
		else if(Prop == ESoundProp::POS_ENV) Src.m_PosEnv = Value;
		else if(Prop == ESoundProp::POS_ENV_OFFSET) Src.m_PosEnvOffset = Value;
		else if(Prop == ESoundProp::SOUND_ENV) Src.m_SoundEnv = Value;
		else if(Prop == ESoundProp::SOUND_ENV_OFFSET) Src.m_SoundEnvOffset = Value;
		else if(PropId == 20) Src.m_Shape.m_Type = Value; // shape type
		else if(PropId == 21) Src.m_Shape.m_Circle.m_Radius = Value; // width/radius
		else if(PropId == 22) Src.m_Shape.m_Rectangle.m_Height = Value; // height
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_QUAD_ADD:
	{
		if(!Reader.HasBytes(12))
			break;
		int GroupIdx = Reader.ReadS32();
		int LayerIdx = Reader.ReadS32();
		int QuadIdx  = Reader.ReadS32();
		CQuad Quad = {};
		if(!ReadQuad(Reader, Quad))
			break;
		if(GroupIdx < 0 || GroupIdx >= (int)Editor()->Map()->m_vpGroups.size())
			break;
		auto &vpLayers = Editor()->Map()->m_vpGroups[GroupIdx]->m_vpLayers;
		if(LayerIdx < 0 || LayerIdx >= (int)vpLayers.size() || vpLayers[LayerIdx]->m_Type != LAYERTYPE_QUADS)
			break;
		auto pLayerQuads = std::static_pointer_cast<CLayerQuads>(vpLayers[LayerIdx]);
		m_ApplyingRemote = true;
		int InsertIdx = maximum(0, minimum(QuadIdx, (int)pLayerQuads->m_vQuads.size()));
		if(InsertIdx != QuadIdx)
			PushLog("MultiMapping: quad index mismatch (map drifted out of sync)");
		pLayerQuads->m_vQuads.insert(pLayerQuads->m_vQuads.begin() + InsertIdx, Quad);
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_QUAD_DEL:
	{
		if(!Reader.HasBytes(12))
			break;
		int GroupIdx = Reader.ReadS32();
		int LayerIdx = Reader.ReadS32();
		int QuadIdx  = Reader.ReadS32();
		if(GroupIdx < 0 || GroupIdx >= (int)Editor()->Map()->m_vpGroups.size())
			break;
		auto &vpLayers = Editor()->Map()->m_vpGroups[GroupIdx]->m_vpLayers;
		if(LayerIdx < 0 || LayerIdx >= (int)vpLayers.size() || vpLayers[LayerIdx]->m_Type != LAYERTYPE_QUADS)
			break;
		auto pLayerQuads = std::static_pointer_cast<CLayerQuads>(vpLayers[LayerIdx]);
		if(QuadIdx < 0 || QuadIdx >= (int)pLayerQuads->m_vQuads.size())
			break;
		m_ApplyingRemote = true;
		pLayerQuads->m_vQuads.erase(pLayerQuads->m_vQuads.begin() + QuadIdx);
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_QUAD_POINTS:
	{
		if(!Reader.HasBytes(12 + 5 * 8))
			break;
		int GroupIdx = Reader.ReadS32();
		int LayerIdx = Reader.ReadS32();
		int QuadIdx  = Reader.ReadS32();
		CPoint aPoints[5];
		for(int i = 0; i < 5; i++) { aPoints[i].x = Reader.ReadS32(); aPoints[i].y = Reader.ReadS32(); }
		if(GroupIdx < 0 || GroupIdx >= (int)Editor()->Map()->m_vpGroups.size())
			break;
		auto &vpLayers = Editor()->Map()->m_vpGroups[GroupIdx]->m_vpLayers;
		if(LayerIdx < 0 || LayerIdx >= (int)vpLayers.size() || vpLayers[LayerIdx]->m_Type != LAYERTYPE_QUADS)
			break;
		auto pLayerQuads = std::static_pointer_cast<CLayerQuads>(vpLayers[LayerIdx]);
		if(QuadIdx < 0 || QuadIdx >= (int)pLayerQuads->m_vQuads.size())
			break;
		m_ApplyingRemote = true;
		std::copy_n(aPoints, 5, pLayerQuads->m_vQuads[QuadIdx].m_aPoints);
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_QUAD_COLORS:
	{
		if(!Reader.HasBytes(12 + 4 * 16))
			break;
		int GroupIdx = Reader.ReadS32();
		int LayerIdx = Reader.ReadS32();
		int QuadIdx  = Reader.ReadS32();
		CColor aColors[4];
		for(int i = 0; i < 4; i++) { aColors[i].r = Reader.ReadS32(); aColors[i].g = Reader.ReadS32(); aColors[i].b = Reader.ReadS32(); aColors[i].a = Reader.ReadS32(); }
		if(GroupIdx < 0 || GroupIdx >= (int)Editor()->Map()->m_vpGroups.size())
			break;
		auto &vpLayers = Editor()->Map()->m_vpGroups[GroupIdx]->m_vpLayers;
		if(LayerIdx < 0 || LayerIdx >= (int)vpLayers.size() || vpLayers[LayerIdx]->m_Type != LAYERTYPE_QUADS)
			break;
		auto pLayerQuads = std::static_pointer_cast<CLayerQuads>(vpLayers[LayerIdx]);
		if(QuadIdx < 0 || QuadIdx >= (int)pLayerQuads->m_vQuads.size())
			break;
		m_ApplyingRemote = true;
		std::copy_n(aColors, 4, pLayerQuads->m_vQuads[QuadIdx].m_aColors);
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_QUAD_PROP:
	{
		if(!Reader.HasBytes(13 + 4))
			break;
		int GroupIdx = Reader.ReadS32();
		int LayerIdx = Reader.ReadS32();
		int QuadIdx  = Reader.ReadS32();
		int Prop     = Reader.ReadU8();
		int Value    = Reader.ReadS32();
		if(GroupIdx < 0 || GroupIdx >= (int)Editor()->Map()->m_vpGroups.size())
			break;
		auto &vpLayers = Editor()->Map()->m_vpGroups[GroupIdx]->m_vpLayers;
		if(LayerIdx < 0 || LayerIdx >= (int)vpLayers.size() || vpLayers[LayerIdx]->m_Type != LAYERTYPE_QUADS)
			break;
		auto pLayerQuads = std::static_pointer_cast<CLayerQuads>(vpLayers[LayerIdx]);
		if(QuadIdx < 0 || QuadIdx >= (int)pLayerQuads->m_vQuads.size())
			break;
		m_ApplyingRemote = true;
		if(Prop == (int)EQuadProp::ORDER)
		{
			pLayerQuads->SwapQuads(QuadIdx, Value);
		}
		else
		{
			CQuad &q = pLayerQuads->m_vQuads[QuadIdx];
			if(Prop == (int)EQuadProp::POS_ENV)            q.m_PosEnv = Value;
			else if(Prop == (int)EQuadProp::POS_ENV_OFFSET) q.m_PosEnvOffset = Value;
			else if(Prop == (int)EQuadProp::COLOR_ENV)       q.m_ColorEnv = Value;
			else if(Prop == (int)EQuadProp::COLOR_ENV_OFFSET) q.m_ColorEnvOffset = Value;
		}
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_QUAD_POINT_PROP:
	{
		if(!Reader.HasBytes(12 + 2 + 4))
			break;
		int GroupIdx  = Reader.ReadS32();
		int LayerIdx  = Reader.ReadS32();
		int QuadIdx   = Reader.ReadS32();
		int PointIdx  = Reader.ReadU8();
		int Prop      = Reader.ReadU8();
		int Value     = Reader.ReadS32();
		if(GroupIdx < 0 || GroupIdx >= (int)Editor()->Map()->m_vpGroups.size())
			break;
		auto &vpLayers = Editor()->Map()->m_vpGroups[GroupIdx]->m_vpLayers;
		if(LayerIdx < 0 || LayerIdx >= (int)vpLayers.size() || vpLayers[LayerIdx]->m_Type != LAYERTYPE_QUADS)
			break;
		auto pLayerQuads = std::static_pointer_cast<CLayerQuads>(vpLayers[LayerIdx]);
		if(QuadIdx < 0 || QuadIdx >= (int)pLayerQuads->m_vQuads.size())
			break;
		if(PointIdx < 0 || PointIdx > 3)
			break;
		m_ApplyingRemote = true;
		CQuad &q = pLayerQuads->m_vQuads[QuadIdx];
		if(Prop == (int)EQuadPointProp::COLOR)
		{
			const ColorRGBA ColorPick = ColorRGBA::UnpackAlphaLast<ColorRGBA>(Value);
			q.m_aColors[PointIdx].r = (int)(ColorPick.r * 255.0f);
			q.m_aColors[PointIdx].g = (int)(ColorPick.g * 255.0f);
			q.m_aColors[PointIdx].b = (int)(ColorPick.b * 255.0f);
			q.m_aColors[PointIdx].a = (int)(ColorPick.a * 255.0f);
		}
		else if(Prop == (int)EQuadPointProp::TEX_U)
			q.m_aTexcoords[PointIdx].x = Value;
		else if(Prop == (int)EQuadPointProp::TEX_V)
			q.m_aTexcoords[PointIdx].y = Value;
		else if(Prop == (int)EQuadPointProp::POS_X)
			q.m_aPoints[PointIdx].x = Value;
		else if(Prop == (int)EQuadPointProp::POS_Y)
			q.m_aPoints[PointIdx].y = Value;
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_LAYER_FLAGS:
	{
		if(!Reader.HasBytes(12))
			break;
		int GroupIdx = Reader.ReadS32();
		int LayerIdx = Reader.ReadS32();
		int Flags    = Reader.ReadS32();
		if(GroupIdx < 0 || GroupIdx >= (int)Editor()->Map()->m_vpGroups.size())
			break;
		auto &vpLayers = Editor()->Map()->m_vpGroups[GroupIdx]->m_vpLayers;
		if(LayerIdx < 0 || LayerIdx >= (int)vpLayers.size())
			break;
		m_ApplyingRemote = true;
		vpLayers[LayerIdx]->m_Flags = Flags;
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_GROUP_PROP:
	{
		if(!Reader.HasBytes(9))
			break;
		int GroupIdx = Reader.ReadS32();
		int PropId   = Reader.ReadU8();
		int Value    = Reader.ReadS32();
		auto &vGroups = Editor()->Map()->m_vpGroups;
		if(GroupIdx < 0 || GroupIdx >= (int)vGroups.size())
			break;
		auto pGroup = vGroups[GroupIdx];
		m_ApplyingRemote = true;
		EGroupProp Prop = static_cast<EGroupProp>(PropId);
		if(Prop == EGroupProp::ORDER)
		{
			Editor()->Map()->m_SelectedGroup = Editor()->Map()->MoveGroup(GroupIdx, Value);
		}
		else if(Prop == EGroupProp::POS_X)           pGroup->m_OffsetX = Value;
		else if(Prop == EGroupProp::POS_Y)      pGroup->m_OffsetY = Value;
		else if(Prop == EGroupProp::PARA_X)     pGroup->m_ParallaxX = Value;
		else if(Prop == EGroupProp::PARA_Y)     pGroup->m_ParallaxY = Value;
		else if(Prop == EGroupProp::USE_CLIPPING) pGroup->m_UseClipping = Value;
		else if(Prop == EGroupProp::CLIP_X)     pGroup->m_ClipX = Value;
		else if(Prop == EGroupProp::CLIP_Y)     pGroup->m_ClipY = Value;
		else if(Prop == EGroupProp::CLIP_W)     pGroup->m_ClipW = Value;
		else if(Prop == EGroupProp::CLIP_H)     pGroup->m_ClipH = Value;
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_SETTING_ADD:
	{
		if(!Reader.HasBytes(2)) break;
		int Len = Reader.ReadU16();
		if(!Reader.HasBytes(Len)) break;
		char aCmd[256] = {};
		int CopyLen = minimum(Len, (int)sizeof(aCmd) - 1);
		Reader.ReadBytes(reinterpret_cast<uint8_t *>(aCmd), CopyLen);
		m_ApplyingRemote = true;
		Editor()->Map()->m_vSettings.emplace_back(aCmd);
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_SETTING_DEL:
	{
		if(!Reader.HasBytes(4)) break;
		int CmdIdx = Reader.ReadS32();
		auto &vSettings = Editor()->Map()->m_vSettings;
		if(CmdIdx < 0 || CmdIdx >= (int)vSettings.size()) break;
		m_ApplyingRemote = true;
		vSettings.erase(vSettings.begin() + CmdIdx);
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_SETTING_EDIT:
	{
		if(!Reader.HasBytes(6)) break;
		int CmdIdx = Reader.ReadS32();
		int Len = Reader.ReadU16();
		if(!Reader.HasBytes(Len)) break;
		auto &vSettings = Editor()->Map()->m_vSettings;
		if(CmdIdx < 0 || CmdIdx >= (int)vSettings.size()) break;
		char aCmd[256] = {};
		int CopyLen = minimum(Len, (int)sizeof(aCmd) - 1);
		Reader.ReadBytes(reinterpret_cast<uint8_t *>(aCmd), CopyLen);
		m_ApplyingRemote = true;
		str_copy(vSettings[CmdIdx].m_aCommand, aCmd);
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_SETTING_MOVE:
	{
		if(!Reader.HasBytes(8)) break;
		int CmdIdx = Reader.ReadS32();
		int Direction = Reader.ReadS32();
		auto &vSettings = Editor()->Map()->m_vSettings;
		int Other = CmdIdx + Direction;
		if(CmdIdx < 0 || CmdIdx >= (int)vSettings.size()) break;
		if(Other < 0 || Other >= (int)vSettings.size()) break;
		m_ApplyingRemote = true;
		std::swap(vSettings[CmdIdx], vSettings[Other]);
		Editor()->Map()->OnModify();
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_MAP_START:
	{
		if(!Reader.HasBytes(7)) break;
		Reader.ReadU8(); // TargetSlot РІР‚вЂќ routing already handled server-side, irrelevant to the receiver
		int TotalSize = Reader.ReadS32();
		int NameLen = Reader.ReadU16();
		if(!Reader.HasBytes(NameLen)) break;
		if(TotalSize <= 0 || TotalSize > 50 * 1024 * 1024) break;
		char aName[256] = {};
		int CopyLen = minimum(NameLen, (int)sizeof(aName) - 1);
		Reader.ReadBytes(reinterpret_cast<uint8_t *>(aName), CopyLen);
		m_MapTransferActive = true;
		m_MapTransferTotal = TotalSize;
		m_MapTransferReceived = 0;
		SanitizeMapName(aName, m_aMapTransferName, sizeof(m_aMapTransferName));
		m_vMapTransferBuf.clear();
		m_vMapTransferBuf.resize(TotalSize, 0);
		dbg_msg("multimapping", "MAP_START: '%s' -> '%s' %d bytes", aName, m_aMapTransferName, TotalSize);
		break;
	}
	case PACKET_MAP_CHUNK:
	{
		if(!m_MapTransferActive) break;
		if(!Reader.HasBytes(6)) break;
		int Offset = Reader.ReadS32();
		int DataLen = Reader.ReadU16();
		if(!Reader.HasBytes(DataLen)) break;
		if(Offset < 0 || (size_t)Offset + (size_t)DataLen > m_vMapTransferBuf.size()) break;
		Reader.ReadBytes(m_vMapTransferBuf.data() + Offset, DataLen);
		m_MapTransferReceived = minimum(m_MapTransferReceived + DataLen, m_MapTransferTotal);
		break;
	}
	case PACKET_MAP_END:
	{
		if(!m_MapTransferActive) break;
		m_MapTransferActive = false;
		dbg_msg("multimapping", "MAP_END: received %d / %d bytes", m_MapTransferReceived, m_MapTransferTotal);
		if(m_MapTransferReceived < m_MapTransferTotal) break;

		// If we have our own local envelope edit that hasn't been sent yet,
		// applying the incoming transfer now would silently discard it.
		// Push ours instead so both sides converge on the local edit rather
		// than on whichever MAP_END happened to arrive first.
		if(m_EnvDirty || m_PendingMapTransfer)
		{
			m_vMapTransferBuf.clear();
			PushLog("MultiMapping: incoming map skipped, local edit pending");
			TriggerEnvelopeResync();
			break;
		}

		// Save to temp file and load
		char aTmpPath[512];
		str_format(aTmpPath, sizeof(aTmpPath), "maps/multimapping_recv_%s", m_aMapTransferName);
		IOHANDLE File = Editor()->Storage()->OpenFile(aTmpPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File) break;
		io_write(File, m_vMapTransferBuf.data(), m_vMapTransferBuf.size());
		io_close(File);
		m_vMapTransferBuf.clear();

		m_ApplyingRemote = true;
		Editor()->Load(aTmpPath, IStorage::TYPE_SAVE);
		m_ApplyingRemote = false;
		m_State = STATE_LIVE;
		m_LastEnvUndoSize = (int)Editor()->Map()->m_EnvelopeEditorHistory.m_vpUndoActions.size();
		m_EnvDirty = false;
		{
			char aBuf[64];
			str_format(aBuf, sizeof(aBuf), "Map loaded: %s", m_aMapTransferName);
			PushLog(aBuf);
		}
		dbg_msg("multimapping", "MAP_END: loaded '%s'", aTmpPath);
		// Hub redistribution: with 3+ players only the authority pushes full
		// map state to everyone. If we (the authority) just received this
		// transfer, it came from a non-authority peer's local edit РІР‚вЂќ become
		// the canonical source again and fan it out to everyone else.
		if(m_IsCreator)
			StartMapTransfer();
		break;
	}
	case PACKET_MAP_NEW:
	{
		if(m_IsCreator) break;
		m_PendingMapNew = true;
		break;
	}
	case PACKET_EDITOR_SETTINGS:
	{
		if(Reader.Remaining() < 9) break;
		uint8_t BrushColor = Reader.ReadU8();
		uint8_t AllowUnused = Reader.ReadU8();
		uint8_t ShowInfo = Reader.ReadU8();
		uint8_t EnvPreview = Reader.ReadU8();
		uint8_t AlignQuads = Reader.ReadU8();
		uint8_t ShowQuadsRect = Reader.ReadU8();
		uint8_t AutoReload = Reader.ReadU8();
		uint8_t LayerSelector = Reader.ReadU8();
		uint8_t ShowIngame = Reader.ReadU8();

		m_ApplyingRemote = true;
		Editor()->m_BrushColorEnabled = BrushColor != 0;
		if(AllowUnused == 1)
			Editor()->m_AllowPlaceUnusedTiles = CEditor::EUnusedEntities::ALLOWED_EXPLICIT;
		else if(AllowUnused == 2)
			Editor()->m_AllowPlaceUnusedTiles = CEditor::EUnusedEntities::ALLOWED_IMPLICIT;
		else
			Editor()->m_AllowPlaceUnusedTiles = CEditor::EUnusedEntities::NOT_ALLOWED;
		Editor()->m_ShowTileInfo = static_cast<CEditor::EShowTile>(ShowInfo % 3);
		Editor()->m_ShowEnvelopePreview = EnvPreview != 0;
		g_Config.m_EdAlignQuads = AlignQuads != 0;
		g_Config.m_EdShowQuadsRect = ShowQuadsRect != 0;
		g_Config.m_EdAutoMapReload = AutoReload != 0;
		g_Config.m_EdLayerSelector = LayerSelector != 0;
		g_Config.m_EdShowIngameEntities = ShowIngame != 0;
		m_ApplyingRemote = false;
		break;
	}
	case PACKET_ACTIVITY:
	{
		if(Reader.Remaining() < 2) break;
		uint8_t Slot = Reader.ReadU8();
		uint8_t Act = Reader.ReadU8();
		if(Slot >= MAX_MULTIMAPPING_PLAYERS) break;
		if(Act <= ACTIVITY_PICKER)
		{
			EActivity Prev = m_aPeers[Slot].m_Activity;
			m_aPeers[Slot].m_Activity = static_cast<EActivity>(Act);
			if(m_aPeers[Slot].m_Activity != Prev)
			{
				const char *pMsg = nullptr;
				switch(m_aPeers[Slot].m_Activity)
				{
				case ACTIVITY_DIALOG: pMsg = "Partner: selecting file..."; break;
				case ACTIVITY_ENVELOPES: pMsg = "Partner: editing envelopes"; break;
				case ACTIVITY_SETTINGS: pMsg = "Partner: server settings"; break;
				case ACTIVITY_TESTING: pMsg = "Partner: local testing"; break;
				case ACTIVITY_AWAY: pMsg = "Partner: left editor"; break;
				case ACTIVITY_PICKER: pMsg = "Partner: selecting tileset"; break;
				case ACTIVITY_MAPPING: pMsg = "Partner: back to mapping"; break;
				default: break;
				}
				if(pMsg)
					PushLog(pMsg);
			}
		}
		if(m_aPeers[Slot].m_Activity != ACTIVITY_MAPPING)
			m_aPeers[Slot].m_HasCursor = false;
		break;
	}
	case PACKET_PING:
	{
		// partner sent ping РІР‚вЂќ reply with pong so they can measure RTT
		std::vector<uint8_t> vPong;
		WriteHeader(vPong, PACKET_PONG);
		SendFramePrio(vPong);
		break;
	}
	case PACKET_PONG:
	{
		// our ping came back РІР‚вЂќ measure RTT
		if(m_LastPingSentTime != 0)
		{
			int64_t Elapsed = time_get() - m_LastPingSentTime;
			m_PingMs = (int)(Elapsed * 1000 / time_freq());
			m_LastPingSentTime = 0;
		}
		break;
	}
	default:
		break;
	}
}

bool CMultiMappingSession::FindGroupAndLayer(const void *pLayerPtr, int &GroupIdx, int &LayerIdx) const
{
	// Check cache first
	if(m_LayerCacheValid)
	{
		auto It = m_LayerIndexCache.find(pLayerPtr);
		if(It != m_LayerIndexCache.end())
		{
			GroupIdx = It->second.first;
			LayerIdx = It->second.second;
			return true;
		}
	}
	// Rebuild cache on miss
	m_LayerIndexCache.clear();
	auto &vGroups = Editor()->Map()->m_vpGroups;
	for(int g = 0; g < (int)vGroups.size(); g++)
	{
		auto &vLayers = vGroups[g]->m_vpLayers;
		for(int l = 0; l < (int)vLayers.size(); l++)
			m_LayerIndexCache[vLayers[l].get()] = {g, l};
	}
	m_LayerCacheValid = true;
	auto It = m_LayerIndexCache.find(pLayerPtr);
	if(It != m_LayerIndexCache.end())
	{
		GroupIdx = It->second.first;
		LayerIdx = It->second.second;
		return true;
	}
	GroupIdx = -1;
	LayerIdx = -1;
	return false;
}

void CMultiMappingSession::NotifyTileEdit(int GroupIdx, int LayerIdx, int TileX, int TileY, uint8_t Index, uint8_t Flags)
{
	if(m_State != STATE_LIVE)
		return;
	m_vPendingTileEdits.push_back({GroupIdx, LayerIdx, TileX, TileY, Index, Flags});
	m_DirtyLayers.emplace(GroupIdx, LayerIdx);
}

void CMultiMappingSession::NotifyTileEditTele(int GroupIdx, int LayerIdx, int TileX, int TileY, uint8_t Index, uint8_t Number, uint8_t Type)
{
	if(m_State != STATE_LIVE)
		return;
	STileEditEntry Entry{GroupIdx, LayerIdx, TileX, TileY, Index, 0};
	Entry.m_ExtraType = 1;
	Entry.m_ExNumber = Number;
	Entry.m_ExType = Type;
	m_vPendingTileEdits.push_back(Entry);
	m_DirtyLayers.emplace(GroupIdx, LayerIdx);
}

void CMultiMappingSession::NotifyTileEditSpeedup(int GroupIdx, int LayerIdx, int TileX, int TileY, uint8_t Index, uint8_t Force, uint8_t MaxSpeed, int16_t Angle)
{
	if(m_State != STATE_LIVE)
		return;
	STileEditEntry Entry{GroupIdx, LayerIdx, TileX, TileY, Index, 0};
	Entry.m_ExtraType = 2;
	Entry.m_ExForce = Force;
	Entry.m_ExMaxSpeed = MaxSpeed;
	Entry.m_ExAngle = Angle;
	m_vPendingTileEdits.push_back(Entry);
	m_DirtyLayers.emplace(GroupIdx, LayerIdx);
}

void CMultiMappingSession::NotifyTileEditSwitch(int GroupIdx, int LayerIdx, int TileX, int TileY, uint8_t Index, uint8_t Flags, uint8_t Number, uint8_t SwitchType, uint8_t SwitchFlags, uint8_t Delay)
{
	if(m_State != STATE_LIVE)
		return;
	STileEditEntry Entry{GroupIdx, LayerIdx, TileX, TileY, Index, Flags};
	Entry.m_ExtraType = 3;
	Entry.m_ExNumber = Number;
	Entry.m_ExType = SwitchType;
	Entry.m_ExSwitchFlags = SwitchFlags;
	Entry.m_ExDelay = Delay;
	m_vPendingTileEdits.push_back(Entry);
	m_DirtyLayers.emplace(GroupIdx, LayerIdx);
}

void CMultiMappingSession::NotifyTileEditTune(int GroupIdx, int LayerIdx, int TileX, int TileY, uint8_t Index, uint8_t Number, uint8_t Type)
{
	if(m_State != STATE_LIVE)
		return;
	STileEditEntry Entry{GroupIdx, LayerIdx, TileX, TileY, Index, 0};
	Entry.m_ExtraType = 4;
	Entry.m_ExNumber = Number;
	Entry.m_ExType = Type;
	m_vPendingTileEdits.push_back(Entry);
	m_DirtyLayers.emplace(GroupIdx, LayerIdx);
}

uint32_t CMultiMappingSession::CalcLayerCRC(const uint8_t *pTiles, int Count, uint32_t InitCrc)
{
	// CRC-32 (ISO 3309)
	static uint32_t s_aTable[256] = {};
	static bool s_Init = false;
	if(!s_Init)
	{
		for(uint32_t i = 0; i < 256; i++)
		{
			uint32_t c = i;
			for(int j = 0; j < 8; j++)
				c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			s_aTable[i] = c;
		}
		s_Init = true;
	}
	uint32_t crc = InitCrc ^ 0xFFFFFFFFu;
	for(int i = 0; i < Count; i++)
		crc = s_aTable[(crc ^ pTiles[i]) & 0xFF] ^ (crc >> 8);
	return crc ^ 0xFFFFFFFFu;
}

void CMultiMappingSession::NotifyStrokeEnd()
{
	if(m_State != STATE_LIVE || m_DirtyLayers.empty())
		return;
	for(const auto &[g, l] : m_DirtyLayers)
		SendSyncCheck(g, l);
	m_DirtyLayers.clear();
}

void CMultiMappingSession::NotifyFullSync()
{
	if(m_State != STATE_LIVE)
		return;
	int64_t Now = time_get();
	if(Now - m_LastFullSyncTime < time_freq())
		return;
	m_LastFullSyncTime = Now;
	auto &vGroups = Editor()->Map()->m_vpGroups;
	for(int g = 0; g < (int)vGroups.size(); g++)
	{
		auto &vLayers = vGroups[g]->m_vpLayers;
		for(int l = 0; l < (int)vLayers.size(); l++)
		{
			if(vLayers[l]->m_Type == LAYERTYPE_TILES)
				SendSyncCheck(g, l);
		}
	}
}

void CMultiMappingSession::NotifyAddGroup(int InsertIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	InvalidateLayerCache();
	SendStructAddGroup(InsertIdx);
}

void CMultiMappingSession::NotifyDelGroup(int GroupIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	InvalidateLayerCache();
	SendStructDelGroup(GroupIdx);
}

void CMultiMappingSession::NotifyAddLayer(int GroupIdx, int LayerIdx, int LayerType, const char *pName, int SubType)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	InvalidateLayerCache();
	SendStructAddLayer(GroupIdx, LayerIdx, LayerType, pName, SubType);
}

void CMultiMappingSession::NotifyDelLayer(int GroupIdx, int LayerIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	InvalidateLayerCache();
	SendStructDelLayer(GroupIdx, LayerIdx);
}

void CMultiMappingSession::SyncLayerContents(int GroupIdx, int LayerIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	auto &vGroups = Editor()->Map()->m_vpGroups;
	if(GroupIdx < 0 || GroupIdx >= (int)vGroups.size())
		return;
	auto &vLayers = vGroups[GroupIdx]->m_vpLayers;
	if(LayerIdx < 0 || LayerIdx >= (int)vLayers.size())
		return;
	auto &pLayer = vLayers[LayerIdx];

	if(pLayer->m_Type == LAYERTYPE_TILES)
	{
		auto pTiles = std::static_pointer_cast<CLayerTiles>(pLayer);
		// Resend dimensions/props first so the receiver's recreated layer matches
		// before relying on the CRC/row-sync path below, which refuses to repair
		// a size mismatch.
		SendStructLayerProp(GroupIdx, LayerIdx, (int)ETilesProp::WIDTH, pTiles->m_Width);
		SendStructLayerProp(GroupIdx, LayerIdx, (int)ETilesProp::HEIGHT, pTiles->m_Height);
		SendStructLayerProp(GroupIdx, LayerIdx, (int)ETilesProp::COLOR, PackColor(pTiles->m_Color));
		SendStructLayerProp(GroupIdx, LayerIdx, (int)ETilesProp::SEED, pTiles->m_Seed);
		SendStructLayerProp(GroupIdx, LayerIdx, (int)ETilesProp::COLOR_ENV, pTiles->m_ColorEnv);
		SendStructLayerProp(GroupIdx, LayerIdx, (int)ETilesProp::COLOR_ENV_OFFSET, pTiles->m_ColorEnvOffset);
		SendStructLayerProp(GroupIdx, LayerIdx, (int)ETilesProp::LIVE_GAMETILES, pTiles->m_LiveGameTiles ? 1 : 0);
		if(pTiles->m_Image >= 0)
			SendStructSetImage(GroupIdx, LayerIdx, pTiles->m_Image);
		SendStructLayerProp(GroupIdx, LayerIdx, (int)ETilesProp::AUTOMAPPER, pTiles->m_AutoMapperConfig);
		// tile contents will be synced by NotifyFullSync CRC check
		SendSyncCheck(GroupIdx, LayerIdx);
	}
	else if(pLayer->m_Type == LAYERTYPE_QUADS)
	{
		auto pQuads = std::static_pointer_cast<CLayerQuads>(pLayer);
		if(pQuads->m_Image >= 0)
			SendStructSetImage(GroupIdx, LayerIdx, pQuads->m_Image);
		for(int i = 0; i < (int)pQuads->m_vQuads.size(); i++)
			SendQuadAdd(GroupIdx, LayerIdx, i, pQuads->m_vQuads[i]);
	}
	else if(pLayer->m_Type == LAYERTYPE_SOUNDS)
	{
		auto pSounds = std::static_pointer_cast<CLayerSounds>(pLayer);
		if(pSounds->m_Sound >= 0)
			SendStructSetSound(GroupIdx, LayerIdx, pSounds->m_Sound);
		for(int i = 0; i < (int)pSounds->m_vSources.size(); i++)
			SendStructAddSoundSource(GroupIdx, LayerIdx, i);
	}
}

void CMultiMappingSession::NotifySetImage(int GroupIdx, int LayerIdx, int ImageIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructSetImage(GroupIdx, LayerIdx, ImageIdx);
}

void CMultiMappingSession::NotifyRenameGroup(int GroupIdx, const char *pName)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructRenameGroup(GroupIdx, pName);
}

void CMultiMappingSession::NotifyRenameLayer(int GroupIdx, int LayerIdx, const char *pName)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructRenameLayer(GroupIdx, LayerIdx, pName);
}

void CMultiMappingSession::SendSyncCheck(int GroupIdx, int LayerIdx)
{
	if(m_Socket == nullptr)
		return;
	auto &vGroups = Editor()->Map()->m_vpGroups;
	if(GroupIdx < 0 || GroupIdx >= (int)vGroups.size())
		return;
	auto &vLayers = vGroups[GroupIdx]->m_vpLayers;
	if(LayerIdx < 0 || LayerIdx >= (int)vLayers.size())
		return;
	if(vLayers[LayerIdx]->m_Type != LAYERTYPE_TILES)
		return;
	auto pTiles = std::static_pointer_cast<CLayerTiles>(vLayers[LayerIdx]);
	int Count = pTiles->m_Width * pTiles->m_Height * (int)sizeof(CTile);
	uint32_t Crc = CalcLayerCRC(reinterpret_cast<const uint8_t *>(pTiles->m_pTiles), Count);

	// include special tile arrays in CRC
	if(auto pTele = std::dynamic_pointer_cast<CLayerTele>(pTiles))
	{
		int ExCount = pTele->m_Width * pTele->m_Height * (int)sizeof(CTeleTile);
		Crc = CalcLayerCRC(reinterpret_cast<const uint8_t *>(pTele->m_pTeleTile), ExCount, Crc);
	}
	else if(auto pSpeedup = std::dynamic_pointer_cast<CLayerSpeedup>(pTiles))
	{
		int ExCount = pSpeedup->m_Width * pSpeedup->m_Height * (int)sizeof(CSpeedupTile);
		Crc = CalcLayerCRC(reinterpret_cast<const uint8_t *>(pSpeedup->m_pSpeedupTile), ExCount, Crc);
	}
	else if(auto pSwitch = std::dynamic_pointer_cast<CLayerSwitch>(pTiles))
	{
		int ExCount = pSwitch->m_Width * pSwitch->m_Height * (int)sizeof(CSwitchTile);
		Crc = CalcLayerCRC(reinterpret_cast<const uint8_t *>(pSwitch->m_pSwitchTile), ExCount, Crc);
	}
	else if(auto pTune = std::dynamic_pointer_cast<CLayerTune>(pTiles))
	{
		int ExCount = pTune->m_Width * pTune->m_Height * (int)sizeof(CTuneTile);
		Crc = CalcLayerCRC(reinterpret_cast<const uint8_t *>(pTune->m_pTuneTile), ExCount, Crc);
	}

	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_SYNC_CHECK);
	WriteS32(vPacket, GroupIdx);
	WriteS32(vPacket, LayerIdx);
	WriteU32(vPacket, Crc);
	SendFrame(vPacket);
}

void CMultiMappingSession::SendSyncRequest(int GroupIdx, int LayerIdx)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_SYNC_REQUEST);
	WriteS32(vPacket, GroupIdx);
	WriteS32(vPacket, LayerIdx);
	SendFrame(vPacket);
}

void CMultiMappingSession::SendSyncData(int GroupIdx, int LayerIdx)
{
	if(m_Socket == nullptr)
		return;
	auto &vGroups = Editor()->Map()->m_vpGroups;
	if(GroupIdx < 0 || GroupIdx >= (int)vGroups.size())
		return;
	auto &vLayers = vGroups[GroupIdx]->m_vpLayers;
	if(LayerIdx < 0 || LayerIdx >= (int)vLayers.size())
		return;
	if(vLayers[LayerIdx]->m_Type != LAYERTYPE_TILES)
		return;
	auto pTiles = std::static_pointer_cast<CLayerTiles>(vLayers[LayerIdx]);
	int W = pTiles->m_Width;
	int H = pTiles->m_Height;
	int RowBytes = W * (int)sizeof(CTile);

	// send one row per frame to keep individual packets small
	for(int row = 0; row < H; row++)
	{
		std::vector<uint8_t> vPacket;
		WriteHeader(vPacket, PACKET_SYNC_DATA);
		WriteS32(vPacket, GroupIdx);
		WriteS32(vPacket, LayerIdx);
		WriteS32(vPacket, W);
		WriteS32(vPacket, H);
		WriteS32(vPacket, row);
		WriteU8(vPacket, 0); // ExtraType=0: CTile row
		WriteBytes(vPacket, reinterpret_cast<const uint8_t *>(pTiles->m_pTiles + row * W), RowBytes);
		SendFrame(vPacket);
		if(m_Socket == nullptr)
			return;
	}

	// send special tile arrays if applicable
	auto pTele = std::dynamic_pointer_cast<CLayerTele>(pTiles);
	auto pSpeedup = std::dynamic_pointer_cast<CLayerSpeedup>(pTiles);
	auto pSwitch = std::dynamic_pointer_cast<CLayerSwitch>(pTiles);
	auto pTune = std::dynamic_pointer_cast<CLayerTune>(pTiles);
	uint8_t ExtraType = pTele ? 1 : pSpeedup ? 2 : pSwitch ? 3 : pTune ? 4 : 0;
	if(ExtraType == 0)
		return;

	const uint8_t *pExData = nullptr;
	int ExItemSize = 0;
	if(pTele) { pExData = reinterpret_cast<const uint8_t *>(pTele->m_pTeleTile); ExItemSize = (int)sizeof(CTeleTile); }
	else if(pSpeedup) { pExData = reinterpret_cast<const uint8_t *>(pSpeedup->m_pSpeedupTile); ExItemSize = (int)sizeof(CSpeedupTile); }
	else if(pSwitch) { pExData = reinterpret_cast<const uint8_t *>(pSwitch->m_pSwitchTile); ExItemSize = (int)sizeof(CSwitchTile); }
	else if(pTune) { pExData = reinterpret_cast<const uint8_t *>(pTune->m_pTuneTile); ExItemSize = (int)sizeof(CTuneTile); }

	int ExRowBytes = W * ExItemSize;
	for(int row = 0; row < H; row++)
	{
		std::vector<uint8_t> vPacket;
		WriteHeader(vPacket, PACKET_SYNC_DATA);
		WriteS32(vPacket, GroupIdx);
		WriteS32(vPacket, LayerIdx);
		WriteS32(vPacket, W);
		WriteS32(vPacket, H);
		WriteS32(vPacket, row);
		WriteU8(vPacket, ExtraType);
		WriteBytes(vPacket, pExData + row * ExRowBytes, ExRowBytes);
		SendFrame(vPacket);
		if(m_Socket == nullptr)
			return;
	}
}

void CMultiMappingSession::SendStructAddGroup(int InsertIdx)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_STRUCT_ADD_GROUP);
	WriteS32(vPacket, InsertIdx);
	SendFrame(vPacket);
}

void CMultiMappingSession::SendStructDelGroup(int GroupIdx)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_STRUCT_DEL_GROUP);
	WriteS32(vPacket, GroupIdx);
	SendFrame(vPacket);
}

void CMultiMappingSession::SendStructAddLayer(int GroupIdx, int LayerIdx, int LayerType, const char *pName, int SubType)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_STRUCT_ADD_LAYER);
	WriteS32(vPacket, GroupIdx);
	WriteS32(vPacket, LayerIdx);
	WriteU8(vPacket, (uint8_t)LayerType);
	WriteU8(vPacket, (uint8_t)SubType);
	int NameLen = str_length(pName);
	WriteString(vPacket, pName, NameLen);
	SendFrame(vPacket);
}

void CMultiMappingSession::SendStructDelLayer(int GroupIdx, int LayerIdx)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_STRUCT_DEL_LAYER);
	WriteS32(vPacket, GroupIdx);
	WriteS32(vPacket, LayerIdx);
	SendFrame(vPacket);
}

void CMultiMappingSession::SendStructSetImage(int GroupIdx, int LayerIdx, int ImageIdx)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_STRUCT_SET_IMAGE);
	WriteS32(vPacket, GroupIdx);
	WriteS32(vPacket, LayerIdx);
	WriteS32(vPacket, ImageIdx);
	SendFrame(vPacket);
}

void CMultiMappingSession::SendStructRenameGroup(int GroupIdx, const char *pName)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_STRUCT_RENAME_GROUP);
	WriteS32(vPacket, GroupIdx);
	int NameLen = str_length(pName);
	WriteString(vPacket, pName, NameLen);
	SendFrame(vPacket);
}

void CMultiMappingSession::SendStructRenameLayer(int GroupIdx, int LayerIdx, const char *pName)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_STRUCT_RENAME_LAYER);
	WriteS32(vPacket, GroupIdx);
	WriteS32(vPacket, LayerIdx);
	int NameLen = str_length(pName);
	WriteString(vPacket, pName, NameLen);
	SendFrame(vPacket);
}

void CMultiMappingSession::SendStructLayerProp(int GroupIdx, int LayerIdx, int PropId, int Value)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_STRUCT_LAYER_PROP);
	WriteS32(vPacket, GroupIdx);
	WriteS32(vPacket, LayerIdx);
	WriteU8(vPacket, (uint8_t)PropId);
	WriteS32(vPacket, Value);
	SendFrame(vPacket);
}

void CMultiMappingSession::SendStructAddImage(const char *pName, bool External, const uint8_t *pData, int DataSize)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_STRUCT_ADD_IMAGE);
	WriteU8(vPacket, External ? 1 : 0);
	int NameLen = str_length(pName);
	WriteString(vPacket, pName, NameLen);
	WriteS32(vPacket, DataSize);
	if(DataSize > 0 && pData)
		WriteBytes(vPacket, pData, DataSize);
	SendFrame(vPacket);
}

void CMultiMappingSession::NotifyLayerProp(int GroupIdx, int LayerIdx, int PropId, int Value)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructLayerProp(GroupIdx, LayerIdx, PropId, Value);
}

void CMultiMappingSession::NotifyAddImage(const char *pName, bool External, const uint8_t *pData, int DataSize)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructAddImage(pName, External, pData, DataSize);
}

void CMultiMappingSession::SendStructDelImage(int ImageIdx)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_STRUCT_DEL_IMAGE);
	WriteS32(vPacket, ImageIdx);
	SendFrame(vPacket);
}

void CMultiMappingSession::SendStructEmbedImage(int ImageIdx, const uint8_t *pData, int DataSize)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_STRUCT_EMBED_IMAGE);
	WriteS32(vPacket, ImageIdx);
	WriteS32(vPacket, DataSize);
	if(DataSize > 0 && pData)
		WriteBytes(vPacket, pData, DataSize);
	SendFrame(vPacket);
}

void CMultiMappingSession::SendStructExternImage(int ImageIdx)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_STRUCT_EXTERN_IMAGE);
	WriteS32(vPacket, ImageIdx);
	SendFrame(vPacket);
}

void CMultiMappingSession::NotifyDelImage(int ImageIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructDelImage(ImageIdx);
}

void CMultiMappingSession::NotifyEmbedImage(int ImageIdx, const uint8_t *pData, int DataSize)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructEmbedImage(ImageIdx, pData, DataSize);
}

void CMultiMappingSession::NotifyExternImage(int ImageIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructExternImage(ImageIdx);
}

void CMultiMappingSession::SendStructAddSound(const char *pName, const uint8_t *pData, int DataSize)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> v;
	WriteHeader(v, PACKET_STRUCT_ADD_SOUND);
	int NameLen = str_length(pName);
	WriteString(v, pName, NameLen);
	WriteS32(v, DataSize);
	if(DataSize > 0 && pData)
		WriteBytes(v, pData, DataSize);
	SendFrame(v);
}

void CMultiMappingSession::SendStructDelSound(int SoundIdx)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> v;
	WriteHeader(v, PACKET_STRUCT_DEL_SOUND);
	WriteS32(v, SoundIdx);
	SendFrame(v);
}

void CMultiMappingSession::NotifyAddSound(const char *pName, const uint8_t *pData, int DataSize)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructAddSound(pName, pData, DataSize);
}

void CMultiMappingSession::NotifyDelSound(int SoundIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructDelSound(SoundIdx);
}

void CMultiMappingSession::SendStructSetSound(int GroupIdx, int LayerIdx, int SoundIdx)
{
	if(m_Socket == nullptr) return;
	std::vector<uint8_t> v;
	WriteHeader(v, PACKET_STRUCT_SET_SOUND);
	WriteS32(v, GroupIdx); WriteS32(v, LayerIdx); WriteS32(v, SoundIdx);
	SendFrame(v);
}

void CMultiMappingSession::NotifySetSound(int GroupIdx, int LayerIdx, int SoundIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendStructSetSound(GroupIdx, LayerIdx, SoundIdx);
}

void CMultiMappingSession::SendStructAddSoundSource(int GroupIdx, int LayerIdx, int SourceIdx)
{
	if(m_Socket == nullptr) return;
	auto &vGroups = Editor()->Map()->m_vpGroups;
	if(GroupIdx < 0 || GroupIdx >= (int)vGroups.size()) return;
	auto &vLayers = vGroups[GroupIdx]->m_vpLayers;
	if(LayerIdx < 0 || LayerIdx >= (int)vLayers.size()) return;
	if(vLayers[LayerIdx]->m_Type != LAYERTYPE_SOUNDS) return;
	auto pSounds = std::static_pointer_cast<CLayerSounds>(vLayers[LayerIdx]);
	if(SourceIdx < 0 || SourceIdx >= (int)pSounds->m_vSources.size()) return;

	std::vector<uint8_t> v;
	WriteHeader(v, PACKET_STRUCT_ADD_SOUND_SOURCE);
	WriteS32(v, GroupIdx); WriteS32(v, LayerIdx); WriteS32(v, SourceIdx);
	WriteSoundSource(v, pSounds->m_vSources[SourceIdx]);
	SendFrame(v);
}

void CMultiMappingSession::NotifyAddSoundSource(int GroupIdx, int LayerIdx, int SourceIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendStructAddSoundSource(GroupIdx, LayerIdx, SourceIdx);
}

void CMultiMappingSession::SendStructDelSoundSource(int GroupIdx, int LayerIdx, int SourceIdx)
{
	if(m_Socket == nullptr) return;
	std::vector<uint8_t> v;
	WriteHeader(v, PACKET_STRUCT_DEL_SOUND_SOURCE);
	WriteS32(v, GroupIdx); WriteS32(v, LayerIdx); WriteS32(v, SourceIdx);
	SendFrame(v);
}

void CMultiMappingSession::NotifyDelSoundSource(int GroupIdx, int LayerIdx, int SourceIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendStructDelSoundSource(GroupIdx, LayerIdx, SourceIdx);
}

void CMultiMappingSession::SendStructEditSoundSource(int GroupIdx, int LayerIdx, int SourceIdx, int PropId, int Value)
{
	if(m_Socket == nullptr) return;
	std::vector<uint8_t> v;
	WriteHeader(v, PACKET_STRUCT_EDIT_SOUND_SOURCE);
	WriteS32(v, GroupIdx); WriteS32(v, LayerIdx); WriteS32(v, SourceIdx);
	WriteU8(v, (uint8_t)PropId); WriteS32(v, Value);
	SendFrame(v);
}

void CMultiMappingSession::NotifyEditSoundSource(int GroupIdx, int LayerIdx, int SourceIdx, int PropId, int Value)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendStructEditSoundSource(GroupIdx, LayerIdx, SourceIdx, PropId, Value);
}

void CMultiMappingSession::SendQuadAdd(int GroupIdx, int LayerIdx, int QuadIdx, const CQuad &Quad)
{
	std::vector<uint8_t> v;
	MultiMappingProtocol::WriteHeader(v, MultiMappingProtocol::PACKET_QUAD_ADD);
	MultiMappingProtocol::WriteS32(v, GroupIdx);
	MultiMappingProtocol::WriteS32(v, LayerIdx);
	MultiMappingProtocol::WriteS32(v, QuadIdx);
	WriteQuad(v, Quad);
	SendFrame(v);
}

void CMultiMappingSession::SendQuadDel(int GroupIdx, int LayerIdx, int QuadIdx)
{
	std::vector<uint8_t> v;
	MultiMappingProtocol::WriteHeader(v, MultiMappingProtocol::PACKET_QUAD_DEL);
	MultiMappingProtocol::WriteS32(v, GroupIdx);
	MultiMappingProtocol::WriteS32(v, LayerIdx);
	MultiMappingProtocol::WriteS32(v, QuadIdx);
	SendFrame(v);
}

void CMultiMappingSession::SendQuadPoints(int GroupIdx, int LayerIdx, int QuadIdx, const CPoint *pPoints)
{
	std::vector<uint8_t> v;
	MultiMappingProtocol::WriteHeader(v, MultiMappingProtocol::PACKET_QUAD_POINTS);
	MultiMappingProtocol::WriteS32(v, GroupIdx);
	MultiMappingProtocol::WriteS32(v, LayerIdx);
	MultiMappingProtocol::WriteS32(v, QuadIdx);
	for(int i = 0; i < 5; i++) { MultiMappingProtocol::WriteS32(v, pPoints[i].x); MultiMappingProtocol::WriteS32(v, pPoints[i].y); }
	SendFrame(v);
}

void CMultiMappingSession::SendQuadColors(int GroupIdx, int LayerIdx, int QuadIdx, const CColor *pColors)
{
	std::vector<uint8_t> v;
	MultiMappingProtocol::WriteHeader(v, MultiMappingProtocol::PACKET_QUAD_COLORS);
	MultiMappingProtocol::WriteS32(v, GroupIdx);
	MultiMappingProtocol::WriteS32(v, LayerIdx);
	MultiMappingProtocol::WriteS32(v, QuadIdx);
	for(int i = 0; i < 4; i++) { MultiMappingProtocol::WriteS32(v, pColors[i].r); MultiMappingProtocol::WriteS32(v, pColors[i].g); MultiMappingProtocol::WriteS32(v, pColors[i].b); MultiMappingProtocol::WriteS32(v, pColors[i].a); }
	SendFrame(v);
}

void CMultiMappingSession::SendQuadProp(int GroupIdx, int LayerIdx, int QuadIdx, int Prop, int Value)
{
	std::vector<uint8_t> v;
	MultiMappingProtocol::WriteHeader(v, MultiMappingProtocol::PACKET_QUAD_PROP);
	MultiMappingProtocol::WriteS32(v, GroupIdx);
	MultiMappingProtocol::WriteS32(v, LayerIdx);
	MultiMappingProtocol::WriteS32(v, QuadIdx);
	MultiMappingProtocol::WriteU8(v, (uint8_t)Prop);
	MultiMappingProtocol::WriteS32(v, Value);
	SendFrame(v);
}

void CMultiMappingSession::SendQuadPointProp(int GroupIdx, int LayerIdx, int QuadIdx, int PointIdx, int Prop, int Value)
{
	std::vector<uint8_t> v;
	MultiMappingProtocol::WriteHeader(v, MultiMappingProtocol::PACKET_QUAD_POINT_PROP);
	MultiMappingProtocol::WriteS32(v, GroupIdx);
	MultiMappingProtocol::WriteS32(v, LayerIdx);
	MultiMappingProtocol::WriteS32(v, QuadIdx);
	MultiMappingProtocol::WriteU8(v, (uint8_t)PointIdx);
	MultiMappingProtocol::WriteU8(v, (uint8_t)Prop);
	MultiMappingProtocol::WriteS32(v, Value);
	SendFrame(v);
}

void CMultiMappingSession::NotifyAddQuad(int GroupIdx, int LayerIdx, int QuadIdx, const CQuad &Quad)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	m_DbgQuadSent++;
	SendQuadAdd(GroupIdx, LayerIdx, QuadIdx, Quad);
}

void CMultiMappingSession::NotifyDelQuad(int GroupIdx, int LayerIdx, int QuadIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	m_DbgQuadSent++;
	SendQuadDel(GroupIdx, LayerIdx, QuadIdx);
}

void CMultiMappingSession::NotifyQuadPoints(int GroupIdx, int LayerIdx, int QuadIdx, const CPoint *pPoints)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	m_DbgQuadSent++;
	SendQuadPoints(GroupIdx, LayerIdx, QuadIdx, pPoints);
}

void CMultiMappingSession::NotifyQuadColors(int GroupIdx, int LayerIdx, int QuadIdx, const CColor *pColors)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	m_DbgQuadSent++;
	SendQuadColors(GroupIdx, LayerIdx, QuadIdx, pColors);
}

void CMultiMappingSession::NotifyQuadProp(int GroupIdx, int LayerIdx, int QuadIdx, int Prop, int Value)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	m_DbgQuadSent++;
	SendQuadProp(GroupIdx, LayerIdx, QuadIdx, Prop, Value);
}

void CMultiMappingSession::NotifyQuadPointProp(int GroupIdx, int LayerIdx, int QuadIdx, int PointIdx, int Prop, int Value)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	m_DbgQuadSent++;
	SendQuadPointProp(GroupIdx, LayerIdx, QuadIdx, PointIdx, Prop, Value);
}

void CMultiMappingSession::SendLayerFlags(int GroupIdx, int LayerIdx, int Flags)
{
	std::vector<uint8_t> v;
	MultiMappingProtocol::WriteHeader(v, MultiMappingProtocol::PACKET_LAYER_FLAGS);
	MultiMappingProtocol::WriteS32(v, GroupIdx);
	MultiMappingProtocol::WriteS32(v, LayerIdx);
	MultiMappingProtocol::WriteS32(v, Flags);
	SendFrame(v);
}

void CMultiMappingSession::NotifyLayerFlags(int GroupIdx, int LayerIdx, int Flags)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendLayerFlags(GroupIdx, LayerIdx, Flags);
}

void CMultiMappingSession::SendGroupProp(int GroupIdx, int PropId, int Value)
{
	std::vector<uint8_t> v;
	MultiMappingProtocol::WriteHeader(v, MultiMappingProtocol::PACKET_GROUP_PROP);
	MultiMappingProtocol::WriteS32(v, GroupIdx);
	MultiMappingProtocol::WriteU8(v, (uint8_t)PropId);
	MultiMappingProtocol::WriteS32(v, Value);
	SendFrame(v);
}

void CMultiMappingSession::NotifyGroupProp(int GroupIdx, int PropId, int Value)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendGroupProp(GroupIdx, PropId, Value);
}

void CMultiMappingSession::SendSettingAdd(const char *pCmd)
{
	std::vector<uint8_t> v;
	MultiMappingProtocol::WriteHeader(v, MultiMappingProtocol::PACKET_SETTING_ADD);
	MultiMappingProtocol::WriteString(v, pCmd, str_length(pCmd));
	SendFrame(v);
}

void CMultiMappingSession::SendSettingDel(int CmdIdx)
{
	std::vector<uint8_t> v;
	MultiMappingProtocol::WriteHeader(v, MultiMappingProtocol::PACKET_SETTING_DEL);
	MultiMappingProtocol::WriteS32(v, CmdIdx);
	SendFrame(v);
}

void CMultiMappingSession::SendSettingEdit(int CmdIdx, const char *pCmd)
{
	std::vector<uint8_t> v;
	MultiMappingProtocol::WriteHeader(v, MultiMappingProtocol::PACKET_SETTING_EDIT);
	MultiMappingProtocol::WriteS32(v, CmdIdx);
	MultiMappingProtocol::WriteString(v, pCmd, str_length(pCmd));
	SendFrame(v);
}

void CMultiMappingSession::SendSettingMove(int CmdIdx, int Direction)
{
	std::vector<uint8_t> v;
	MultiMappingProtocol::WriteHeader(v, MultiMappingProtocol::PACKET_SETTING_MOVE);
	MultiMappingProtocol::WriteS32(v, CmdIdx);
	MultiMappingProtocol::WriteS32(v, Direction);
	SendFrame(v);
}

void CMultiMappingSession::NotifySettingAdd(const char *pCmd)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendSettingAdd(pCmd);
}

void CMultiMappingSession::NotifySettingDel(int CmdIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendSettingDel(CmdIdx);
}

void CMultiMappingSession::NotifySettingEdit(int CmdIdx, const char *pCmd)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendSettingEdit(CmdIdx, pCmd);
}

void CMultiMappingSession::NotifySettingMove(int CmdIdx, int Direction)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendSettingMove(CmdIdx, Direction);
}

void CMultiMappingSession::NotifyEditorSettings()
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendEditorSettings();
}

void CMultiMappingSession::SendMapStart(const char *pName, int TotalSize, uint8_t TargetSlot)
{
	std::vector<uint8_t> v;
	WriteHeader(v, PACKET_MAP_START);
	WriteU8(v, TargetSlot);
	WriteS32(v, TotalSize);
	WriteString(v, pName, str_length(pName));
	SendFrame(v);
}

void CMultiMappingSession::SendMapChunk(int Offset, const uint8_t *pData, int DataLen)
{
	std::vector<uint8_t> v;
	WriteHeader(v, PACKET_MAP_CHUNK);
	WriteS32(v, Offset);
	WriteU16(v, (uint16_t)DataLen);
	v.insert(v.end(), pData, pData + DataLen);
	SendFrame(v);
}

void CMultiMappingSession::SendMapEnd()
{
	std::vector<uint8_t> v;
	WriteHeader(v, PACKET_MAP_END);
	SendFrame(v);
}

void CMultiMappingSession::SendMapNew()
{
	if(m_State != STATE_LIVE || !m_IsCreator)
		return;
	std::vector<uint8_t> v;
	WriteHeader(v, PACKET_MAP_NEW);
	SendFrame(v);
}

void CMultiMappingSession::SendEditorSettings()
{
	if(m_State != STATE_LIVE)
		return;
	std::vector<uint8_t> v;
	WriteHeader(v, PACKET_EDITOR_SETTINGS);
	WriteU8(v, Editor()->m_BrushColorEnabled ? 1 : 0);
	int AllowUnused = 0;
	if(Editor()->m_AllowPlaceUnusedTiles == CEditor::EUnusedEntities::ALLOWED_EXPLICIT)
		AllowUnused = 1;
	else if(Editor()->m_AllowPlaceUnusedTiles == CEditor::EUnusedEntities::ALLOWED_IMPLICIT)
		AllowUnused = 2;
	WriteU8(v, static_cast<uint8_t>(AllowUnused));
	WriteU8(v, static_cast<uint8_t>(Editor()->m_ShowTileInfo));
	WriteU8(v, Editor()->m_ShowEnvelopePreview ? 1 : 0);
	WriteU8(v, g_Config.m_EdAlignQuads ? 1 : 0);
	WriteU8(v, g_Config.m_EdShowQuadsRect ? 1 : 0);
	WriteU8(v, g_Config.m_EdAutoMapReload ? 1 : 0);
	WriteU8(v, g_Config.m_EdLayerSelector ? 1 : 0);
	WriteU8(v, g_Config.m_EdShowIngameEntities ? 1 : 0);
	SendFrame(v);
}

void CMultiMappingSession::SendActivity(EActivity Activity)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> v;
	WriteHeader(v, PACKET_ACTIVITY);
	WriteU8(v, static_cast<uint8_t>(Activity));
	SendFrame(v);
}

void CMultiMappingSession::OnBackgroundUpdate()
{
	if(m_State < STATE_CONNECTING || m_Socket == nullptr)
		return;
	if(m_State == STATE_CONNECTING && m_ConnectStartTime != 0)
		return;

	ProcessNetwork();
	if(m_Socket == nullptr)
		return;

	int64_t Now = time_get();
	int64_t Freq = time_freq();

	// Stale server detection
	if(Now - m_LastServerPacketTime > Freq * 30)
	{
		str_copy(m_aErrorMsg, "Connection lost");
		PushLog("Connection lost");
		m_State = STATE_ERROR;
		CloseSocket();
		return;
	}

	// Heartbeat every 5 seconds
	if(m_State >= STATE_WAITING && Now - m_LastHeartbeatTime > Freq * 5)
	{
		SendHeartbeat();
		m_LastHeartbeatTime = Now;
	}

	// Ping every 2 seconds to measure RTT
	if(m_State == STATE_LIVE && m_LastPingSentTime == 0 && Now - m_LastPingTime > Freq * 2)
	{
		SendPing();
		m_LastPingTime = Now;
	}

	// Determine current activity and send if changed
	EActivity Current = ACTIVITY_AWAY;
	if(g_Config.m_ClEditor)
	{
		if(Editor()->m_ShowPicker)
			Current = ACTIVITY_PICKER;
		else if(Editor()->m_Dialog != DIALOG_NONE)
			Current = ACTIVITY_DIALOG;
		else if(Editor()->m_ActiveExtraEditor == CEditor::EXTRAEDITOR_SERVER_SETTINGS)
			Current = ACTIVITY_SETTINGS;
		else if(Editor()->m_ActiveExtraEditor == CEditor::EXTRAEDITOR_ENVELOPES)
			Current = ACTIVITY_ENVELOPES;
		else
			Current = ACTIVITY_MAPPING;
	}
	else
	{
		Current = m_LocalTestingActive ? ACTIVITY_TESTING : ACTIVITY_AWAY;
	}

	if(Current != m_LastLocalActivity)
	{
		m_LastLocalActivity = Current;
		SendActivity(Current);
	}

	// process deferred SYNC_DATA responses РІР‚вЂќ send one per tick, only if send buffer has room
	if(!m_vPendingSyncResponses.empty() && (int)(m_vSendBuf.size() - m_SendBufOffset) < 512 * 1024)
	{
		auto Resp = m_vPendingSyncResponses.front();
		m_vPendingSyncResponses.erase(m_vPendingSyncResponses.begin());
		SendSyncData(Resp.m_GroupIdx, Resp.m_LayerIdx);
	}

	// process deferred SYNC_REQUESTs РІР‚вЂќ send only if CRC still mismatches after the delay
	if(!m_vPendingSyncRequests.empty())
	{
		int64_t Now2 = time_get();
		for(int i = (int)m_vPendingSyncRequests.size() - 1; i >= 0; --i)
		{
			auto &Req = m_vPendingSyncRequests[i];
			if(Now2 < Req.m_RequestAfter)
				continue;
			// re-check CRC: if TILE_RELAY packets arrived in the meantime, no need to request
			auto &vGroups2 = Editor()->Map()->m_vpGroups;
			bool Stale = Req.m_GroupIdx < 0 || Req.m_GroupIdx >= (int)vGroups2.size();
			if(!Stale)
			{
				auto &vLayers2 = vGroups2[Req.m_GroupIdx]->m_vpLayers;
				Stale = Req.m_LayerIdx < 0 || Req.m_LayerIdx >= (int)vLayers2.size();
				if(!Stale && vLayers2[Req.m_LayerIdx]->m_Type == LAYERTYPE_TILES)
				{
					// we don't have TheirCrc here anymore, so just send the request РІР‚вЂќ
					// the extra round-trip (check РІвЂ вЂ™ request РІвЂ вЂ™ data) only happens after 500ms
					SendSyncRequest(Req.m_GroupIdx, Req.m_LayerIdx);
				}
			}
			m_vPendingSyncRequests.erase(m_vPendingSyncRequests.begin() + i);
			if(m_Socket == nullptr)
				break;
		}
	}

	// Drain outbound TCP buffer even while the editor isn't the active view
	// (e.g. while test-playing the map) РІР‚вЂќ otherwise queued heartbeats/pings/
	// sync data just pile up until the editor regains focus.
	DrainSendBuf();
}

void CMultiMappingSession::StartMapTransfer(uint8_t TargetSlot)
{
	if(m_State != STATE_LIVE)
		return;

	static const char *s_pTmpPath = "multimapping_transfer_tmp.map";
	Editor()->Save(s_pTmpPath);
	m_PendingMapTransfer = true;
	m_PendingMapTransferTarget = TargetSlot;
}

uint8_t CMultiMappingSession::FindAuthoritySlot() const
{
	if(m_IsCreator)
		return m_LocalSlot;
	for(int Slot = 0; Slot < MultiMappingProtocol::MAX_MULTIMAPPING_PLAYERS; Slot++)
	{
		if(m_aPeers[Slot].m_Connected && m_aPeers[Slot].m_IsCreator)
			return static_cast<uint8_t>(Slot);
	}
	return 0xFF;
}

void CMultiMappingSession::TriggerEnvelopeResync()
{
	if(m_IsCreator)
	{
		StartMapTransfer();
		return;
	}
	uint8_t AuthoritySlot = FindAuthoritySlot();
	if(AuthoritySlot != 0xFF)
		StartMapTransfer(AuthoritySlot);
}

CUi::EPopupMenuFunctionResult CMultiMappingSession::PopupMultiMappingMain(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);
	CMultiMappingSession *pSession = &pEditor->m_MultiMappingSession;
	CUIRect Slot;

	if(pSession->m_State == STATE_ERROR && pSession->m_aErrorMsg[0])
	{
		View.HSplitTop(12.0f, &Slot, &View);
		pEditor->Ui()->DoLabel(&Slot, pSession->m_aErrorMsg, 10.0f, TEXTALIGN_MC);
		View.HSplitTop(4.0f, nullptr, &View);
	}

	if(pSession->m_State == STATE_IDLE || pSession->m_State == STATE_ERROR)
	{
		static int s_CreateButton = 0;
		View.HSplitTop(14.0f, &Slot, &View);
		if(pEditor->DoButton_MenuItem(&s_CreateButton, "Create room", 0, &Slot, BUTTONFLAG_LEFT, "Create a new MultiMapping room."))
		{
			pSession->Connect(nullptr, true);
			static SPopupMenuId s_PopupCreateId;
			pEditor->Ui()->DoPopupMenu(&s_PopupCreateId, View.x + View.w, View.y - 14.0f, 220.0f, 110.0f, pEditor, PopupMultiMappingCreate);
			return CUi::POPUP_CLOSE_CURRENT;
		}

		View.HSplitTop(4.0f, nullptr, &View);
		static int s_JoinButton = 0;
		View.HSplitTop(14.0f, &Slot, &View);
		if(pEditor->DoButton_MenuItem(&s_JoinButton, "Join room", 0, &Slot, BUTTONFLAG_LEFT, "Join an existing MultiMapping room."))
		{
			pSession->m_aErrorMsg[0] = '\0';
			static SPopupMenuId s_PopupJoinId;
			pEditor->Ui()->DoPopupMenu(&s_PopupJoinId, View.x + View.w, View.y - 14.0f, 220.0f, 110.0f, pEditor, PopupMultiMappingJoin);
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}
	else if(pSession->m_State == STATE_WAITING || pSession->m_State == STATE_LIVE)
	{
		View.HSplitTop(12.0f, &Slot, &View);
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "Room: %s", pSession->m_aRoomCode);
		pEditor->Ui()->DoLabel(&Slot, aBuf, 10.0f, TEXTALIGN_ML);

		View.HSplitTop(12.0f, &Slot, &View);
		str_format(aBuf, sizeof(aBuf), "Players: %d / 8", pSession->m_ParticipantCount);
		pEditor->Ui()->DoLabel(&Slot, aBuf, 10.0f, TEXTALIGN_ML);

		if(pSession->m_State == STATE_LIVE)
		{
			View.HSplitTop(12.0f, &Slot, &View);
			str_format(aBuf, sizeof(aBuf), "Q tx:%d rx:%d", pSession->m_DbgQuadSent, pSession->m_DbgQuadRecv);
			pEditor->Ui()->DoLabel(&Slot, aBuf, 10.0f, TEXTALIGN_ML);
		}
	}

	if(pSession->m_State == STATE_CONNECTING || pSession->m_State == STATE_WAITING || pSession->m_State == STATE_LIVE)
	{
		View.HSplitTop(4.0f, nullptr, &View);
		static int s_DisconnectButton = 0;
		View.HSplitTop(14.0f, &Slot, &View);
		if(pEditor->DoButton_MenuItem(&s_DisconnectButton, "Disconnect", 0, &Slot, BUTTONFLAG_LEFT, "Leave the MultiMapping session."))
		{
			pSession->Disconnect();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CMultiMappingSession::PopupMultiMappingCreate(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);
	CMultiMappingSession *pSession = &pEditor->m_MultiMappingSession;
	CUIRect Slot;

	View.HSplitTop(14.0f, &Slot, &View);
	char aBuf[64];
	if(pSession->m_aRoomCode[0])
		str_format(aBuf, sizeof(aBuf), "Code: %s", pSession->m_aRoomCode);
	else
		str_copy(aBuf, "Connecting...");
	pEditor->Ui()->DoLabel(&Slot, aBuf, 10.0f, TEXTALIGN_MC);

	View.HSplitTop(14.0f, &Slot, &View);
	str_format(aBuf, sizeof(aBuf), "Players: %d / 8", pSession->m_ParticipantCount);
	pEditor->Ui()->DoLabel(&Slot, aBuf, 10.0f, TEXTALIGN_MC);

	if(pSession->m_aRoomCode[0])
	{
		View.HSplitTop(4.0f, nullptr, &View);
		static int s_CopyButton = 0;
		View.HSplitTop(14.0f, &Slot, &View);
		if(pEditor->DoButton_MenuItem(&s_CopyButton, "Copy Code", 0, &Slot, BUTTONFLAG_LEFT, "Copy room code to clipboard."))
			pEditor->Input()->SetClipboardText(pSession->m_aRoomCode);
	}

	if(pSession->m_ParticipantCount >= 2)
	{
		// The session goes live automatically once the server reports both
		// participants present (see PACKET_ROOM_STATE) РІР‚вЂќ there is no client-sent
		// "start" request in this protocol, so this button only closes the popup.
		View.HSplitTop(4.0f, nullptr, &View);
		static int s_CloseButton = 0;
		View.HSplitTop(14.0f, &Slot, &View);
		if(pEditor->DoButton_MenuItem(&s_CloseButton, "Close", 0, &Slot, BUTTONFLAG_LEFT, "Close this window and keep mapping."))
		{
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}
	else
	{
		View.HSplitTop(14.0f, &Slot, &View);
		pEditor->Ui()->DoLabel(&Slot, "Waiting for partner...", 9.0f, TEXTALIGN_MC);
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CMultiMappingSession::PopupMultiMappingJoin(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);
	CMultiMappingSession *pSession = &pEditor->m_MultiMappingSession;
	CUIRect Slot;

	// Р С—Р С•Р Т‘Р С”Р В»РЎР‹РЎвЂЎР С‘Р В»Р С‘РЎРѓРЎРЉ РІР‚вЂќ Р С•Р С”Р Р…Р С• Р С•РЎРѓРЎвЂљР В°РЎвЂРЎвЂљРЎРѓРЎРЏ Р С•РЎвЂљР С”РЎР‚РЎвЂ№РЎвЂљРЎвЂ№Р С РЎРѓ Р С‘Р Р…РЎвЂћР С•Р в„– Р С• РЎРѓР ВµРЎРѓРЎРѓР С‘Р С‘,
	// Р Т‘Р В¶Р С•Р в„–Р Р…Р ВµРЎР‚ Р В·Р В°Р С”РЎР‚РЎвЂ№Р Р†Р В°Р ВµРЎвЂљ Р ВµР С–Р С• Р Р†РЎР‚РЎС“РЎвЂЎР Р…РЎС“РЎР‹ (Р С”Р В°Р С” РЎС“ РЎРѓР С•Р В·Р Т‘Р В°РЎвЂљР ВµР В»РЎРЏ)
	if(pSession->m_State == STATE_WAITING || pSession->m_State == STATE_LIVE)
	{
		char aBuf[64];
		View.HSplitTop(14.0f, &Slot, &View);
		str_format(aBuf, sizeof(aBuf), "Room: %s", pSession->m_aRoomCode);
		pEditor->Ui()->DoLabel(&Slot, aBuf, 10.0f, TEXTALIGN_MC);

		View.HSplitTop(14.0f, &Slot, &View);
		str_format(aBuf, sizeof(aBuf), "Players: %d / 8", pSession->m_ParticipantCount);
		pEditor->Ui()->DoLabel(&Slot, aBuf, 10.0f, TEXTALIGN_MC);

		View.HSplitTop(14.0f, &Slot, &View);
		const char *pStatus = pSession->m_State == STATE_LIVE ? "Connected!" : "Waiting for host...";
		pEditor->Ui()->DoLabel(&Slot, pStatus, 9.0f, TEXTALIGN_MC);

		View.HSplitTop(4.0f, nullptr, &View);
		static int s_CloseButton = 0;
		View.HSplitTop(14.0f, &Slot, &View);
		if(pEditor->DoButton_MenuItem(&s_CloseButton, "Close", 0, &Slot, BUTTONFLAG_LEFT, "Close this window and keep mapping."))
			return CUi::POPUP_CLOSE_CURRENT;

		return CUi::POPUP_KEEP_OPEN;
	}

	View.HSplitTop(14.0f, &Slot, &View);
	pEditor->Ui()->DoLabel(&Slot, "Enter room code:", 10.0f, TEXTALIGN_ML);

	View.HSplitTop(14.0f, &Slot, &View);
	static CLineInput s_CodeInput;
	s_CodeInput.SetBuffer(pSession->m_aJoinCodeInput, sizeof(pSession->m_aJoinCodeInput));

	bool bConnecting = pSession->m_State == STATE_CONNECTING;
	if(bConnecting)
		pEditor->Ui()->DoLabel(&Slot, pSession->m_aJoinCodeInput, 10.0f, TEXTALIGN_ML);
	else
		pEditor->DoEditBox(&s_CodeInput, &Slot, 10.0f);

	View.HSplitTop(4.0f, nullptr, &View);
	static int s_ConnectButton = 0;
	View.HSplitTop(14.0f, &Slot, &View);

	if(bConnecting)
	{
		pEditor->Ui()->DoLabel(&Slot, "Connecting...", 10.0f, TEXTALIGN_MC);
	}
	else
	{
		if(pEditor->DoButton_MenuItem(&s_ConnectButton, "Connect", 0, &Slot, BUTTONFLAG_LEFT, "Connect to the room."))
		{
			if(str_length(pSession->m_aJoinCodeInput) == ROOM_CODE_LEN)
				pSession->Connect(pSession->m_aJoinCodeInput, false);
		}
	}

	// Р С•РЎв‚¬Р С‘Р В±Р С”Р В° (Р Р…Р ВµР Р†Р ВµРЎР‚Р Р…РЎвЂ№Р в„– Р С”Р С•Р Т‘, Р С”Р С•Р СР Р…Р В°РЎвЂљР В° Р Р…Р Вµ Р Р…Р В°Р в„–Р Т‘Р ВµР Р…Р В°, Р С—Р С•Р В»Р Р…Р В°РЎРЏ)
	if(pSession->m_State == STATE_ERROR && pSession->m_aErrorMsg[0])
	{
		View.HSplitTop(6.0f, nullptr, &View);
		View.HSplitTop(14.0f, &Slot, &View);
		pEditor->Ui()->DoLabel(&Slot, pSession->m_aErrorMsg, 9.0f, TEXTALIGN_MC);
		View.HSplitTop(4.0f, nullptr, &View);
		static int s_RetryButton = 0;
		View.HSplitTop(14.0f, &Slot, &View);
		if(pEditor->DoButton_MenuItem(&s_RetryButton, "Try again", 0, &Slot, BUTTONFLAG_LEFT, "Clear error and try again."))
		{
			pSession->m_State = STATE_IDLE;
			pSession->m_aErrorMsg[0] = '\0';
		}
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CMultiMappingSession::PopupMultiMapping(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);
	CMultiMappingSession *pSession = &pEditor->m_MultiMappingSession;
	CUIRect Slot;
	static int64_t s_CopiedTime = 0;

	View.Margin(6.0f, &View);

	if(pSession->m_State == STATE_IDLE || pSession->m_State == STATE_ERROR)
	{
		// title
		View.HSplitTop(14.0f, &Slot, &View);
		pEditor->Ui()->DoLabel(&Slot, "MultiMapping", 11.0f, TEXTALIGN_MC);
		View.HSplitTop(5.0f, nullptr, &View);

		// create room
		static int s_CreateButton = 0;
		View.HSplitTop(16.0f, &Slot, &View);
		if(pEditor->DoButton_Editor(&s_CreateButton, "Create room", 0, &Slot, BUTTONFLAG_LEFT, "Create a new collaboration room."))
			pSession->Connect(nullptr, true);

		View.HSplitTop(5.0f, nullptr, &View);

		// join room label
		View.HSplitTop(12.0f, &Slot, &View);
		pEditor->Ui()->DoLabel(&Slot, "Join room:", 10.0f, TEXTALIGN_ML);
		View.HSplitTop(2.0f, nullptr, &View);

		// code input
		View.HSplitTop(16.0f, &Slot, &View);
		static CLineInput s_CodeInput;
		s_CodeInput.SetBuffer(pSession->m_aJoinCodeInput, sizeof(pSession->m_aJoinCodeInput));
		pEditor->DoEditBox(&s_CodeInput, &Slot, 10.0f);

		View.HSplitTop(3.0f, nullptr, &View);

		// connect
		static int s_ConnectButton = 0;
		View.HSplitTop(16.0f, &Slot, &View);
		if(pEditor->DoButton_Editor(&s_ConnectButton, "Connect", 0, &Slot, BUTTONFLAG_LEFT, "Connect to the room."))
		{
			if(str_length(pSession->m_aJoinCodeInput) == ROOM_CODE_LEN)
				pSession->Connect(pSession->m_aJoinCodeInput, false);
		}

		if(pSession->m_State == STATE_ERROR && pSession->m_aErrorMsg[0])
		{
			View.HSplitTop(5.0f, nullptr, &View);
			View.HSplitTop(12.0f, &Slot, &View);
			pEditor->Ui()->DoLabel(&Slot, pSession->m_aErrorMsg, 9.0f, TEXTALIGN_MC);
			View.HSplitTop(3.0f, nullptr, &View);
			static int s_RetryButton2 = 0;
			View.HSplitTop(16.0f, &Slot, &View);
			if(pEditor->DoButton_Editor(&s_RetryButton2, "Try again", 0, &Slot, BUTTONFLAG_LEFT, "Clear error and try again."))
			{
				pSession->m_State = STATE_IDLE;
				pSession->m_aErrorMsg[0] = '\0';
			}
		}
	}
	else if(pSession->m_State == STATE_CONNECTING)
	{
		View.HSplitTop(14.0f, &Slot, &View);
		pEditor->Ui()->DoLabel(&Slot, "MultiMapping", 11.0f, TEXTALIGN_MC);
		View.HSplitTop(8.0f, nullptr, &View);
		View.HSplitTop(14.0f, &Slot, &View);
		pEditor->Ui()->DoLabel(&Slot, "Connecting...", 10.0f, TEXTALIGN_MC);
		View.HSplitTop(6.0f, nullptr, &View);
		static int s_CancelButton = 0;
		View.HSplitTop(16.0f, &Slot, &View);
		if(pEditor->DoButton_Editor(&s_CancelButton, "Cancel", 0, &Slot, BUTTONFLAG_LEFT, "Cancel connection."))
			pSession->Disconnect();
	}
	else if(pSession->m_State == STATE_WAITING || pSession->m_State == STATE_LIVE)
	{
		View.HSplitTop(14.0f, &Slot, &View);
		pEditor->Ui()->DoLabel(&Slot, "MultiMapping", 11.0f, TEXTALIGN_MC);
		View.HSplitTop(5.0f, nullptr, &View);

		// room code row: label left, copy button right, same height
		View.HSplitTop(16.0f, &Slot, &View);
		CUIRect CodeRect, CopyRect;
		Slot.VSplitRight(58.0f, &CodeRect, &CopyRect);
		char aCodeLabel[32];
		str_format(aCodeLabel, sizeof(aCodeLabel), "Room: %s", pSession->m_aRoomCode);
		pEditor->Ui()->DoLabel(&CodeRect, aCodeLabel, 10.0f, TEXTALIGN_ML);

		static int s_CopyButton = 0;
		bool bJustCopied = (time_get() - s_CopiedTime) < time_freq();
		if(pEditor->DoButton_Editor(&s_CopyButton, bJustCopied ? "Copied!" : "Copy", bJustCopied ? 1 : 0, &CopyRect, BUTTONFLAG_LEFT, "Copy room code to clipboard."))
		{
			pEditor->Input()->SetClipboardText(pSession->m_aRoomCode);
			s_CopiedTime = time_get();
		}

		View.HSplitTop(4.0f, nullptr, &View);
		char aPlayers[32];
		str_format(aPlayers, sizeof(aPlayers), "Players: %d/8", pSession->m_ParticipantCount);
		View.HSplitTop(13.0f, &Slot, &View);
		pEditor->Ui()->DoLabel(&Slot, aPlayers, 10.0f, TEXTALIGN_ML);

		View.HSplitTop(2.0f, nullptr, &View);
		View.HSplitTop(13.0f, &Slot, &View);
		pEditor->Ui()->DoLabel(&Slot, pSession->m_IsCreator ? "Role: Owner" : "Role: Joiner", 10.0f, TEXTALIGN_ML);

		// roster: colored swatch (matches the in-world cursor color) + nickname
		View.HSplitTop(4.0f, nullptr, &View);
		for(int RosterSlot = 0; RosterSlot < MultiMappingProtocol::MAX_MULTIMAPPING_PLAYERS; RosterSlot++)
		{
			bool IsLocal = (RosterSlot == pSession->m_LocalSlot);
			const CMultiMappingSession::SPeer &Peer = pSession->m_aPeers[RosterSlot];
			if(!IsLocal && !Peer.m_Connected)
				continue;

			View.HSplitTop(13.0f, &Slot, &View);
			CUIRect SwatchRect, NameRect;
			Slot.VSplitLeft(12.0f, &SwatchRect, &NameRect);
			NameRect.VSplitLeft(4.0f, nullptr, &NameRect);

			const ColorRGBA &SwatchColor = s_aMultiMappingSlotColors[RosterSlot];
			pEditor->Graphics()->TextureClear();
			pEditor->Graphics()->QuadsBegin();
			pEditor->Graphics()->SetColor(SwatchColor.r, SwatchColor.g, SwatchColor.b, SwatchColor.a);
			IGraphics::CQuadItem SwatchItem(SwatchRect.x, SwatchRect.y + 1.0f, 10.0f, 10.0f);
			pEditor->Graphics()->QuadsDrawTL(&SwatchItem, 1);
			pEditor->Graphics()->QuadsEnd();

			const char *pNick = IsLocal ? pSession->m_aLocalNickname : Peer.m_aNickname;
			bool RosterIsCreator = IsLocal ? pSession->m_IsCreator : Peer.m_IsCreator;
			char aRosterName[80];
			str_format(aRosterName, sizeof(aRosterName), "%s%s%s", pNick[0] ? pNick : "Player",
				RosterIsCreator ? " (Owner)" : "", IsLocal ? " (You)" : "");
			pEditor->Ui()->DoLabel(&NameRect, aRosterName, 9.0f, TEXTALIGN_ML);
		}

		View.HSplitTop(4.0f, nullptr, &View);
		View.HSplitTop(13.0f, &Slot, &View);
		pEditor->Ui()->DoLabel(&Slot, pSession->m_State == STATE_WAITING ? "Waiting for partner..." : "Connected!", 10.0f, TEXTALIGN_MC);

		if(pSession->m_MapTransferActive && pSession->m_MapTransferTotal > 0)
		{
			View.HSplitTop(4.0f, nullptr, &View);
			View.HSplitTop(13.0f, &Slot, &View);
			char aProgress[64];
			int Pct = (int)(100.0f * pSession->m_MapTransferReceived / pSession->m_MapTransferTotal);
			str_format(aProgress, sizeof(aProgress), "Receiving map... %d%%", Pct);
			pEditor->Ui()->DoLabel(&Slot, aProgress, 9.0f, TEXTALIGN_MC);

			View.HSplitTop(3.0f, nullptr, &View);
			View.HSplitTop(8.0f, &Slot, &View);
			CUIRect BarBg = Slot;
			CUIRect BarFill = Slot;
			BarFill.w = Slot.w * (float)pSession->m_MapTransferReceived / pSession->m_MapTransferTotal;
			pEditor->Graphics()->TextureClear();
			pEditor->Graphics()->QuadsBegin();
			pEditor->Graphics()->SetColor(0.2f, 0.2f, 0.2f, 0.8f);
			IGraphics::CQuadItem BgItem(BarBg.x, BarBg.y, BarBg.w, BarBg.h);
			pEditor->Graphics()->QuadsDrawTL(&BgItem, 1);
			pEditor->Graphics()->SetColor(0.2f, 0.7f, 0.3f, 0.9f);
			if(BarFill.w > 0.0f)
			{
				IGraphics::CQuadItem FillItem(BarFill.x, BarFill.y, BarFill.w, BarFill.h);
				pEditor->Graphics()->QuadsDrawTL(&FillItem, 1);
			}
			pEditor->Graphics()->QuadsEnd();
		}

		View.HSplitTop(5.0f, nullptr, &View);
		static int s_DisconnectButton = 0;
		View.HSplitTop(16.0f, &Slot, &View);
		if(pEditor->DoButton_Editor(&s_DisconnectButton, "Disconnect", 0, &Slot, BUTTONFLAG_LEFT, "Disconnect from the room."))
			pSession->Disconnect();
	}

	return CUi::POPUP_KEEP_OPEN;
}
