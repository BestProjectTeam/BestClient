#include "duo_session.h"

#include <game/editor/editor.h>
#include <game/editor/editor_actions.h>
#include <game/editor/mapitems.h>
#include <game/editor/mapitems/layer_tiles.h>
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

using namespace DuoProtocol;

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

void CDuoSession::OnInit(CEditor *pEditor)
{
	CEditorComponent::OnInit(pEditor);
}

void CDuoSession::PushLog(const char *pText)
{
	// shift entries down, newest at index 0
	for(int i = LOG_CAPACITY - 1; i > 0; i--)
		m_aLog[i] = m_aLog[i - 1];
	str_copy(m_aLog[0].m_aText, pText);
	if(m_LogCount < LOG_CAPACITY)
		m_LogCount++;
}

void CDuoSession::SanitizeMapName(const char *pIn, char *pOut, int OutSize)
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

void CDuoSession::OnReset()
{
	if(!m_ApplyingRemote && !m_OwnerLoadingMap)
	{
		if(m_State == STATE_LIVE && m_IsCreator)
			return;
		Disconnect();
	}
}

void CDuoSession::OnUpdate()
{
	if(m_State == STATE_IDLE || m_State == STATE_ERROR)
		return;

	if(m_Socket == nullptr)
		return;

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
		static const char *s_pTmpPath = "duo_transfer_tmp.map";
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
				dbg_msg("duo", "MapTransfer: sending '%s' (%d bytes)", pBaseName, (int)vData.size());
				SendMapStart(pBaseName, (int)vData.size());
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
			StartMapTransfer();
		}
	}

	int64_t Now = time_get();
	int64_t Freq = time_freq();

	// Cursor sync at ~30 Hz — only while actively mapping, so the partner's
	// cursor disappears when we open a dialog / picker / extra editor.
	if(m_State == STATE_LIVE && m_LastLocalActivity == ACTIVITY_MAPPING && Now - m_LastCursorSendTime > Freq / 30)
	{
		SendCursor(Editor()->m_MouseWorldNoParaPos.x, Editor()->m_MouseWorldNoParaPos.y);
		m_LastCursorSendTime = Now;
	}

	// Flush batched tile edits
	if(m_State == STATE_LIVE)
		FlushTileEdits();
}

void CDuoSession::OnRender(CUIRect View)
{
	if(m_State != STATE_LIVE || !m_HasRemoteCursor)
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
	float ScreenX = (m_RemoteCursorX - aPoints[0]) / WorldWidth * pScreen->w;
	float ScreenY = (m_RemoteCursorY - aPoints[1]) / WorldHeight * pScreen->h;

	if(ScreenX < View.x || ScreenX > View.x + View.w || ScreenY < View.y || ScreenY > View.y + View.h)
		return;

	Graphics()->WrapClamp();
	Graphics()->TextureSet(Editor()->m_aCursorTextures[CEditor::CURSOR_NORMAL]);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.2f, 0.8f, 1.0f, 0.85f);
	IGraphics::CQuadItem QuadItem(ScreenX, ScreenY, 16.0f, 16.0f);
	Graphics()->QuadsDrawTL(&QuadItem, 1);
	Graphics()->QuadsEnd();
	Graphics()->WrapNormal();
}

void CDuoSession::Connect(const char *pRoomCode, bool Create)
{
	Disconnect();
	m_IsCreator = Create;
	m_ParticipantCount = 0;
	m_HasRemoteCursor = false;
	m_aErrorMsg[0] = '\0';
	m_vRecvBuf.clear();
	m_RecvBufLen = 0;
	m_LastHeartbeatTime = time_get();
	m_LastCursorSendTime = time_get();

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
	m_LastServerPacketTime = time_get();
	SendHello();
}

void CDuoSession::Disconnect()
{
	if(m_State >= STATE_CONNECTING)
		SendGoodbye();
	CloseSocket();
	m_State = STATE_IDLE;
	m_HasRemoteCursor = false;
	m_RemoteActivity = ACTIVITY_MAPPING;
	m_LastLocalActivity = ACTIVITY_MAPPING;
	m_RemoteDisconnected = false;
	m_LogCount = 0;
	m_ParticipantCount = 0;
	m_RecvBufLen = 0;
	m_vRecvBuf.clear();
	m_vPendingTileEdits.clear();
	m_DirtyLayers.clear();
}

void CDuoSession::OpenSocket()
{
	NETADDR Bind;
	mem_zero(&Bind, sizeof(Bind));
	Bind.type = NETTYPE_IPV4;
	Bind.port = 0;
	m_Socket = net_tcp_create(Bind);
	if(m_Socket == nullptr)
		return;
	if(net_tcp_connect(m_Socket, &m_ServerAddr) != 0)
	{
		net_tcp_close(m_Socket);
		m_Socket = nullptr;
		return;
	}
	net_set_non_blocking(m_Socket);
}

void CDuoSession::CloseSocket()
{
	if(m_Socket != nullptr)
	{
		net_tcp_close(m_Socket);
		m_Socket = nullptr;
	}
}

void CDuoSession::SendFrame(const std::vector<uint8_t> &vPayload)
{
	if(m_Socket == nullptr)
		return;
	uint32_t Size = (uint32_t)vPayload.size();
	uint8_t aLen[4];
	aLen[0] = (Size >> 24) & 0xFF;
	aLen[1] = (Size >> 16) & 0xFF;
	aLen[2] = (Size >> 8) & 0xFF;
	aLen[3] = Size & 0xFF;

	// switch to blocking for the duration of this send so large payloads
	// don't get partial writes or WOULDBLOCK on a non-blocking socket
	net_set_blocking(m_Socket);

	auto SendAll = [&](const uint8_t *pData, int Len) -> bool {
		int Sent = 0;
		while(Sent < Len)
		{
			int Ret = net_tcp_send(m_Socket, pData + Sent, Len - Sent);
			if(Ret <= 0)
				return false;
			Sent += Ret;
		}
		return true;
	};

	bool Ok = SendAll(aLen, 4) && SendAll(vPayload.data(), (int)vPayload.size());
	net_set_non_blocking(m_Socket);

	if(!Ok)
	{
		str_copy(m_aErrorMsg, "Connection lost");
		m_State = STATE_ERROR;
		CloseSocket();
	}
}

void CDuoSession::SendHello()
{
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_HELLO);
	WriteU16(vPacket, 100); // client build
	WriteU8(vPacket, m_IsCreator ? 1 : 0);
	int CodeLen = str_length(m_aRoomCode);
	WriteString(vPacket, m_aRoomCode, CodeLen);
	WriteU64(vPacket, static_cast<uint64_t>(time(nullptr)));
	AppendAuth(vPacket);
	SendFrame(vPacket);
}

void CDuoSession::SendHeartbeat()
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_HEARTBEAT);
	SendFrame(vPacket);
}

void CDuoSession::SendCursor(float WorldX, float WorldY)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_CURSOR);
	WriteS32(vPacket, static_cast<int32_t>(WorldX * 1000.0f));
	WriteS32(vPacket, static_cast<int32_t>(WorldY * 1000.0f));
	SendFrame(vPacket);
}

void CDuoSession::SendTileEdit(int GroupIdx, int LayerIdx, int TileX, int TileY, uint8_t Index, uint8_t Flags)
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
	SendFrame(vPacket);
}

void CDuoSession::FlushTileEdits()
{
	if(m_Socket == nullptr || m_vPendingTileEdits.empty())
		return;
	for(const auto &Edit : m_vPendingTileEdits)
		SendTileEdit(Edit.m_GroupIdx, Edit.m_LayerIdx, Edit.m_TileX, Edit.m_TileY, Edit.m_Index, Edit.m_Flags);
	m_vPendingTileEdits.clear();
}

void CDuoSession::SendGoodbye()
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_GOODBYE);
	SendFrame(vPacket);
}

void CDuoSession::AppendAuth(std::vector<uint8_t> &vPacket) const
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

void CDuoSession::ProcessNetwork()
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
	// Bytes < 0: WOULDBLOCK — normal for non-blocking

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
	for(int i = 0; i < 5; i++) { DuoProtocol::WriteS32(v, q.m_aPoints[i].x); DuoProtocol::WriteS32(v, q.m_aPoints[i].y); }
	for(int i = 0; i < 4; i++) { DuoProtocol::WriteS32(v, q.m_aColors[i].r); DuoProtocol::WriteS32(v, q.m_aColors[i].g); DuoProtocol::WriteS32(v, q.m_aColors[i].b); DuoProtocol::WriteS32(v, q.m_aColors[i].a); }
	for(int i = 0; i < 4; i++) { DuoProtocol::WriteS32(v, q.m_aTexcoords[i].x); DuoProtocol::WriteS32(v, q.m_aTexcoords[i].y); }
	DuoProtocol::WriteS32(v, q.m_PosEnv);
	DuoProtocol::WriteS32(v, q.m_PosEnvOffset);
	DuoProtocol::WriteS32(v, q.m_ColorEnv);
	DuoProtocol::WriteS32(v, q.m_ColorEnvOffset);
}

static bool ReadQuad(DuoProtocol::CPacketReader &r, CQuad &q)
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
	DuoProtocol::WriteS32(v, s.m_Position.x);
	DuoProtocol::WriteS32(v, s.m_Position.y);
	DuoProtocol::WriteS32(v, s.m_Loop);
	DuoProtocol::WriteS32(v, s.m_Pan);
	DuoProtocol::WriteS32(v, s.m_TimeDelay);
	DuoProtocol::WriteS32(v, s.m_Falloff);
	DuoProtocol::WriteS32(v, s.m_PosEnv);
	DuoProtocol::WriteS32(v, s.m_PosEnvOffset);
	DuoProtocol::WriteS32(v, s.m_SoundEnv);
	DuoProtocol::WriteS32(v, s.m_SoundEnvOffset);
	DuoProtocol::WriteU8(v, (uint8_t)s.m_Shape.m_Type);
	DuoProtocol::WriteS32(v, s.m_Shape.m_Circle.m_Radius);
	DuoProtocol::WriteS32(v, s.m_Shape.m_Rectangle.m_Height);
}

static bool ReadSoundSource(DuoProtocol::CPacketReader &r, CSoundSource &s)
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

void CDuoSession::HandleMessage(const uint8_t *pData, int Size)
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
		bool WasLive = (m_State == STATE_LIVE);
		m_ParticipantCount = Reader.ReadU8();
		uint8_t Live = Reader.ReadU8();
		if(Live)
		{
			m_State = STATE_LIVE;
			m_LastEnvUndoSize = (int)Editor()->Map()->m_EnvelopeEditorHistory.m_vpUndoActions.size();
			m_EnvDirty = false;
			if(m_RemoteDisconnected)
				PushLog("Partner reconnected");
			m_RemoteDisconnected = false;
		}
		if(m_ParticipantCount < 2)
		{
			m_HasRemoteCursor = false;
			if(WasLive)
			{
				m_RemoteDisconnected = true;
				PushLog("Partner disconnected");
			}
		}
		else
		{
			m_RemoteDisconnected = false;
		}
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
		int32_t wx = Reader.ReadS32();
		int32_t wy = Reader.ReadS32();
		m_RemoteCursorX = static_cast<float>(wx) / 1000.0f;
		m_RemoteCursorY = static_cast<float>(wy) / 1000.0f;
		// Only show the cursor while the partner is actively mapping. A late
		// cursor relay must not resurrect the cursor we hid on activity change.
		m_HasRemoteCursor = (m_RemoteActivity == ACTIVITY_MAPPING);
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
						CTile Tile;
						Tile.m_Index = TileIndex;
						Tile.m_Flags = TileFlags & 0x0F;
						Tile.m_Skip = 0;
						Tile.m_Reserved = 0;
						pTiles->SetTileIgnoreHistory(TileX, TileY, Tile);
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
		// partner finished a stroke — check if our layer matches their CRC
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
		if(OurCrc != TheirCrc)
			SendSyncRequest(GroupIdx, LayerIdx);
		break;
	}
	case PACKET_SYNC_REQUEST:
	{
		// partner detected desync — send them our full layer
		int32_t GroupIdx = Reader.ReadS32();
		int32_t LayerIdx = Reader.ReadS32();
		SendSyncData(GroupIdx, LayerIdx);
		break;
	}
	case PACKET_SYNC_DATA:
	{
		// one row of full layer dump from partner — apply it
		int32_t GroupIdx = Reader.ReadS32();
		int32_t LayerIdx = Reader.ReadS32();
		int32_t Width    = Reader.ReadS32();
		int32_t Height   = Reader.ReadS32();
		int32_t Row      = Reader.ReadS32();
		int RowBytes = Width * (int)sizeof(CTile);
		if(!Reader.HasBytes(RowBytes) || Width <= 0 || Height <= 0 || Row < 0 || Row >= Height)
			break;

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
		Reader.ReadBytes(reinterpret_cast<uint8_t *>(pTiles->m_pTiles + Row * Width), RowBytes);
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
		Editor()->Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionGroup>(Editor()->Map(), GroupIdx, true));
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
		m_ApplyingRemote = false;
		(void)LayerIdx;
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
		Editor()->Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionDeleteLayer>(Editor()->Map(), GroupIdx, LayerIdx));
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
		if(vLayers[LayerIdx]->m_Type != LAYERTYPE_TILES)
			break;
		auto pTiles = std::static_pointer_cast<CLayerTiles>(vLayers[LayerIdx]);
		m_ApplyingRemote = true;
		ETilesProp Prop = static_cast<ETilesProp>(PropId);
		if(Prop == ETilesProp::WIDTH)
			pTiles->Resize(Value, pTiles->m_Height);
		else if(Prop == ETilesProp::HEIGHT)
			pTiles->Resize(pTiles->m_Width, Value);
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
			// external image — load from local mapres/
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
		Reader.ReadS32(); // SourceIdx hint — not needed, we always append
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
		if(!Reader.HasBytes(6)) break;
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
		dbg_msg("duo", "MAP_START: '%s' -> '%s' %d bytes", aName, m_aMapTransferName, TotalSize);
		break;
	}
	case PACKET_MAP_CHUNK:
	{
		if(!m_MapTransferActive) break;
		if(!Reader.HasBytes(6)) break;
		int Offset = Reader.ReadS32();
		int DataLen = Reader.ReadU16();
		if(!Reader.HasBytes(DataLen)) break;
		if(Offset < 0 || Offset + DataLen > m_MapTransferTotal) break;
		Reader.ReadBytes(m_vMapTransferBuf.data() + Offset, DataLen);
		m_MapTransferReceived = minimum(m_MapTransferReceived + DataLen, m_MapTransferTotal);
		break;
	}
	case PACKET_MAP_END:
	{
		if(!m_MapTransferActive) break;
		m_MapTransferActive = false;
		dbg_msg("duo", "MAP_END: received %d / %d bytes", m_MapTransferReceived, m_MapTransferTotal);
		if(m_MapTransferReceived < m_MapTransferTotal) break;

		// Save to temp file and load
		char aTmpPath[512];
		str_format(aTmpPath, sizeof(aTmpPath), "maps/duo_recv_%s", m_aMapTransferName);
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
		dbg_msg("duo", "MAP_END: loaded '%s'", aTmpPath);
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
		if(Reader.Remaining() < 1) break;
		uint8_t Act = Reader.ReadU8();
		if(Act <= ACTIVITY_PICKER)
		{
			EActivity Prev = m_RemoteActivity;
			m_RemoteActivity = static_cast<EActivity>(Act);
			if(m_RemoteActivity != Prev)
			{
				const char *pMsg = nullptr;
				switch(m_RemoteActivity)
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
		if(m_RemoteActivity != ACTIVITY_MAPPING)
			m_HasRemoteCursor = false;
		break;
	}
	default:
		break;
	}
}

void CDuoSession::NotifyTileEdit(int GroupIdx, int LayerIdx, int TileX, int TileY, uint8_t Index, uint8_t Flags)
{
	if(m_State != STATE_LIVE)
		return;
	m_vPendingTileEdits.push_back({GroupIdx, LayerIdx, TileX, TileY, Index, Flags});
	m_DirtyLayers.emplace(GroupIdx, LayerIdx);
}

uint32_t CDuoSession::CalcLayerCRC(const uint8_t *pTiles, int Count)
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
	uint32_t crc = 0xFFFFFFFFu;
	for(int i = 0; i < Count; i++)
		crc = s_aTable[(crc ^ pTiles[i]) & 0xFF] ^ (crc >> 8);
	return crc ^ 0xFFFFFFFFu;
}

void CDuoSession::NotifyStrokeEnd()
{
	if(m_State != STATE_LIVE || m_DirtyLayers.empty())
		return;
	for(const auto &[g, l] : m_DirtyLayers)
		SendSyncCheck(g, l);
	m_DirtyLayers.clear();
}

void CDuoSession::NotifyFullSync()
{
	if(m_State != STATE_LIVE)
		return;
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

void CDuoSession::NotifyAddGroup(int InsertIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructAddGroup(InsertIdx);
}

void CDuoSession::NotifyDelGroup(int GroupIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructDelGroup(GroupIdx);
}

void CDuoSession::NotifyAddLayer(int GroupIdx, int LayerIdx, int LayerType, const char *pName, int SubType)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructAddLayer(GroupIdx, LayerIdx, LayerType, pName, SubType);
}

void CDuoSession::NotifyDelLayer(int GroupIdx, int LayerIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructDelLayer(GroupIdx, LayerIdx);
}

void CDuoSession::SyncLayerContents(int GroupIdx, int LayerIdx)
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
		if(pTiles->m_Image >= 0)
			SendStructSetImage(GroupIdx, LayerIdx, pTiles->m_Image);
		// tiles will be synced by NotifyFullSync CRC check
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
}

void CDuoSession::NotifySetImage(int GroupIdx, int LayerIdx, int ImageIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructSetImage(GroupIdx, LayerIdx, ImageIdx);
}

void CDuoSession::NotifyRenameGroup(int GroupIdx, const char *pName)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructRenameGroup(GroupIdx, pName);
}

void CDuoSession::NotifyRenameLayer(int GroupIdx, int LayerIdx, const char *pName)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructRenameLayer(GroupIdx, LayerIdx, pName);
}

void CDuoSession::SendSyncCheck(int GroupIdx, int LayerIdx)
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

	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_SYNC_CHECK);
	WriteS32(vPacket, GroupIdx);
	WriteS32(vPacket, LayerIdx);
	WriteU32(vPacket, Crc);
	SendFrame(vPacket);
}

void CDuoSession::SendSyncRequest(int GroupIdx, int LayerIdx)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_SYNC_REQUEST);
	WriteS32(vPacket, GroupIdx);
	WriteS32(vPacket, LayerIdx);
	SendFrame(vPacket);
}

void CDuoSession::SendSyncData(int GroupIdx, int LayerIdx)
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
		WriteBytes(vPacket, reinterpret_cast<const uint8_t *>(pTiles->m_pTiles + row * W), RowBytes);
		SendFrame(vPacket);
		if(m_Socket == nullptr)
			return;
	}
}

void CDuoSession::SendStructAddGroup(int InsertIdx)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_STRUCT_ADD_GROUP);
	WriteS32(vPacket, InsertIdx);
	SendFrame(vPacket);
}

void CDuoSession::SendStructDelGroup(int GroupIdx)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_STRUCT_DEL_GROUP);
	WriteS32(vPacket, GroupIdx);
	SendFrame(vPacket);
}

void CDuoSession::SendStructAddLayer(int GroupIdx, int LayerIdx, int LayerType, const char *pName, int SubType)
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

void CDuoSession::SendStructDelLayer(int GroupIdx, int LayerIdx)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_STRUCT_DEL_LAYER);
	WriteS32(vPacket, GroupIdx);
	WriteS32(vPacket, LayerIdx);
	SendFrame(vPacket);
}

void CDuoSession::SendStructSetImage(int GroupIdx, int LayerIdx, int ImageIdx)
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

void CDuoSession::SendStructRenameGroup(int GroupIdx, const char *pName)
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

void CDuoSession::SendStructRenameLayer(int GroupIdx, int LayerIdx, const char *pName)
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

void CDuoSession::SendStructLayerProp(int GroupIdx, int LayerIdx, int PropId, int Value)
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

void CDuoSession::SendStructAddImage(const char *pName, bool External, const uint8_t *pData, int DataSize)
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

void CDuoSession::NotifyLayerProp(int GroupIdx, int LayerIdx, int PropId, int Value)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructLayerProp(GroupIdx, LayerIdx, PropId, Value);
}

void CDuoSession::NotifyAddImage(const char *pName, bool External, const uint8_t *pData, int DataSize)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructAddImage(pName, External, pData, DataSize);
}

void CDuoSession::SendStructDelImage(int ImageIdx)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_STRUCT_DEL_IMAGE);
	WriteS32(vPacket, ImageIdx);
	SendFrame(vPacket);
}

void CDuoSession::SendStructEmbedImage(int ImageIdx, const uint8_t *pData, int DataSize)
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

void CDuoSession::SendStructExternImage(int ImageIdx)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> vPacket;
	WriteHeader(vPacket, PACKET_STRUCT_EXTERN_IMAGE);
	WriteS32(vPacket, ImageIdx);
	SendFrame(vPacket);
}

void CDuoSession::NotifyDelImage(int ImageIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructDelImage(ImageIdx);
}

void CDuoSession::NotifyEmbedImage(int ImageIdx, const uint8_t *pData, int DataSize)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructEmbedImage(ImageIdx, pData, DataSize);
}

void CDuoSession::NotifyExternImage(int ImageIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructExternImage(ImageIdx);
}

void CDuoSession::SendStructAddSound(const char *pName, const uint8_t *pData, int DataSize)
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

void CDuoSession::SendStructDelSound(int SoundIdx)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> v;
	WriteHeader(v, PACKET_STRUCT_DEL_SOUND);
	WriteS32(v, SoundIdx);
	SendFrame(v);
}

void CDuoSession::NotifyAddSound(const char *pName, const uint8_t *pData, int DataSize)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructAddSound(pName, pData, DataSize);
}

void CDuoSession::NotifyDelSound(int SoundIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote)
		return;
	SendStructDelSound(SoundIdx);
}

void CDuoSession::SendStructSetSound(int GroupIdx, int LayerIdx, int SoundIdx)
{
	if(m_Socket == nullptr) return;
	std::vector<uint8_t> v;
	WriteHeader(v, PACKET_STRUCT_SET_SOUND);
	WriteS32(v, GroupIdx); WriteS32(v, LayerIdx); WriteS32(v, SoundIdx);
	SendFrame(v);
}

void CDuoSession::NotifySetSound(int GroupIdx, int LayerIdx, int SoundIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendStructSetSound(GroupIdx, LayerIdx, SoundIdx);
}

void CDuoSession::SendStructAddSoundSource(int GroupIdx, int LayerIdx, int SourceIdx)
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

void CDuoSession::NotifyAddSoundSource(int GroupIdx, int LayerIdx, int SourceIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendStructAddSoundSource(GroupIdx, LayerIdx, SourceIdx);
}

void CDuoSession::SendStructDelSoundSource(int GroupIdx, int LayerIdx, int SourceIdx)
{
	if(m_Socket == nullptr) return;
	std::vector<uint8_t> v;
	WriteHeader(v, PACKET_STRUCT_DEL_SOUND_SOURCE);
	WriteS32(v, GroupIdx); WriteS32(v, LayerIdx); WriteS32(v, SourceIdx);
	SendFrame(v);
}

void CDuoSession::NotifyDelSoundSource(int GroupIdx, int LayerIdx, int SourceIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendStructDelSoundSource(GroupIdx, LayerIdx, SourceIdx);
}

void CDuoSession::SendStructEditSoundSource(int GroupIdx, int LayerIdx, int SourceIdx, int PropId, int Value)
{
	if(m_Socket == nullptr) return;
	std::vector<uint8_t> v;
	WriteHeader(v, PACKET_STRUCT_EDIT_SOUND_SOURCE);
	WriteS32(v, GroupIdx); WriteS32(v, LayerIdx); WriteS32(v, SourceIdx);
	WriteU8(v, (uint8_t)PropId); WriteS32(v, Value);
	SendFrame(v);
}

void CDuoSession::NotifyEditSoundSource(int GroupIdx, int LayerIdx, int SourceIdx, int PropId, int Value)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendStructEditSoundSource(GroupIdx, LayerIdx, SourceIdx, PropId, Value);
}

void CDuoSession::SendQuadAdd(int GroupIdx, int LayerIdx, int QuadIdx, const CQuad &Quad)
{
	std::vector<uint8_t> v;
	DuoProtocol::WriteHeader(v, DuoProtocol::PACKET_QUAD_ADD);
	DuoProtocol::WriteS32(v, GroupIdx);
	DuoProtocol::WriteS32(v, LayerIdx);
	DuoProtocol::WriteS32(v, QuadIdx);
	WriteQuad(v, Quad);
	SendFrame(v);
}

void CDuoSession::SendQuadDel(int GroupIdx, int LayerIdx, int QuadIdx)
{
	std::vector<uint8_t> v;
	DuoProtocol::WriteHeader(v, DuoProtocol::PACKET_QUAD_DEL);
	DuoProtocol::WriteS32(v, GroupIdx);
	DuoProtocol::WriteS32(v, LayerIdx);
	DuoProtocol::WriteS32(v, QuadIdx);
	SendFrame(v);
}

void CDuoSession::SendQuadPoints(int GroupIdx, int LayerIdx, int QuadIdx, const CPoint *pPoints)
{
	std::vector<uint8_t> v;
	DuoProtocol::WriteHeader(v, DuoProtocol::PACKET_QUAD_POINTS);
	DuoProtocol::WriteS32(v, GroupIdx);
	DuoProtocol::WriteS32(v, LayerIdx);
	DuoProtocol::WriteS32(v, QuadIdx);
	for(int i = 0; i < 5; i++) { DuoProtocol::WriteS32(v, pPoints[i].x); DuoProtocol::WriteS32(v, pPoints[i].y); }
	SendFrame(v);
}

void CDuoSession::SendQuadColors(int GroupIdx, int LayerIdx, int QuadIdx, const CColor *pColors)
{
	std::vector<uint8_t> v;
	DuoProtocol::WriteHeader(v, DuoProtocol::PACKET_QUAD_COLORS);
	DuoProtocol::WriteS32(v, GroupIdx);
	DuoProtocol::WriteS32(v, LayerIdx);
	DuoProtocol::WriteS32(v, QuadIdx);
	for(int i = 0; i < 4; i++) { DuoProtocol::WriteS32(v, pColors[i].r); DuoProtocol::WriteS32(v, pColors[i].g); DuoProtocol::WriteS32(v, pColors[i].b); DuoProtocol::WriteS32(v, pColors[i].a); }
	SendFrame(v);
}

void CDuoSession::SendQuadProp(int GroupIdx, int LayerIdx, int QuadIdx, int Prop, int Value)
{
	std::vector<uint8_t> v;
	DuoProtocol::WriteHeader(v, DuoProtocol::PACKET_QUAD_PROP);
	DuoProtocol::WriteS32(v, GroupIdx);
	DuoProtocol::WriteS32(v, LayerIdx);
	DuoProtocol::WriteS32(v, QuadIdx);
	DuoProtocol::WriteU8(v, (uint8_t)Prop);
	DuoProtocol::WriteS32(v, Value);
	SendFrame(v);
}

void CDuoSession::SendQuadPointProp(int GroupIdx, int LayerIdx, int QuadIdx, int PointIdx, int Prop, int Value)
{
	std::vector<uint8_t> v;
	DuoProtocol::WriteHeader(v, DuoProtocol::PACKET_QUAD_POINT_PROP);
	DuoProtocol::WriteS32(v, GroupIdx);
	DuoProtocol::WriteS32(v, LayerIdx);
	DuoProtocol::WriteS32(v, QuadIdx);
	DuoProtocol::WriteU8(v, (uint8_t)PointIdx);
	DuoProtocol::WriteU8(v, (uint8_t)Prop);
	DuoProtocol::WriteS32(v, Value);
	SendFrame(v);
}

void CDuoSession::NotifyAddQuad(int GroupIdx, int LayerIdx, int QuadIdx, const CQuad &Quad)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	m_DbgQuadSent++;
	SendQuadAdd(GroupIdx, LayerIdx, QuadIdx, Quad);
}

void CDuoSession::NotifyDelQuad(int GroupIdx, int LayerIdx, int QuadIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	m_DbgQuadSent++;
	SendQuadDel(GroupIdx, LayerIdx, QuadIdx);
}

void CDuoSession::NotifyQuadPoints(int GroupIdx, int LayerIdx, int QuadIdx, const CPoint *pPoints)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	m_DbgQuadSent++;
	SendQuadPoints(GroupIdx, LayerIdx, QuadIdx, pPoints);
}

void CDuoSession::NotifyQuadColors(int GroupIdx, int LayerIdx, int QuadIdx, const CColor *pColors)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	m_DbgQuadSent++;
	SendQuadColors(GroupIdx, LayerIdx, QuadIdx, pColors);
}

void CDuoSession::NotifyQuadProp(int GroupIdx, int LayerIdx, int QuadIdx, int Prop, int Value)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	m_DbgQuadSent++;
	SendQuadProp(GroupIdx, LayerIdx, QuadIdx, Prop, Value);
}

void CDuoSession::NotifyQuadPointProp(int GroupIdx, int LayerIdx, int QuadIdx, int PointIdx, int Prop, int Value)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	m_DbgQuadSent++;
	SendQuadPointProp(GroupIdx, LayerIdx, QuadIdx, PointIdx, Prop, Value);
}

void CDuoSession::SendLayerFlags(int GroupIdx, int LayerIdx, int Flags)
{
	std::vector<uint8_t> v;
	DuoProtocol::WriteHeader(v, DuoProtocol::PACKET_LAYER_FLAGS);
	DuoProtocol::WriteS32(v, GroupIdx);
	DuoProtocol::WriteS32(v, LayerIdx);
	DuoProtocol::WriteS32(v, Flags);
	SendFrame(v);
}

void CDuoSession::NotifyLayerFlags(int GroupIdx, int LayerIdx, int Flags)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendLayerFlags(GroupIdx, LayerIdx, Flags);
}

void CDuoSession::SendGroupProp(int GroupIdx, int PropId, int Value)
{
	std::vector<uint8_t> v;
	DuoProtocol::WriteHeader(v, DuoProtocol::PACKET_GROUP_PROP);
	DuoProtocol::WriteS32(v, GroupIdx);
	DuoProtocol::WriteU8(v, (uint8_t)PropId);
	DuoProtocol::WriteS32(v, Value);
	SendFrame(v);
}

void CDuoSession::NotifyGroupProp(int GroupIdx, int PropId, int Value)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendGroupProp(GroupIdx, PropId, Value);
}

void CDuoSession::SendSettingAdd(const char *pCmd)
{
	std::vector<uint8_t> v;
	DuoProtocol::WriteHeader(v, DuoProtocol::PACKET_SETTING_ADD);
	DuoProtocol::WriteString(v, pCmd, str_length(pCmd));
	SendFrame(v);
}

void CDuoSession::SendSettingDel(int CmdIdx)
{
	std::vector<uint8_t> v;
	DuoProtocol::WriteHeader(v, DuoProtocol::PACKET_SETTING_DEL);
	DuoProtocol::WriteS32(v, CmdIdx);
	SendFrame(v);
}

void CDuoSession::SendSettingEdit(int CmdIdx, const char *pCmd)
{
	std::vector<uint8_t> v;
	DuoProtocol::WriteHeader(v, DuoProtocol::PACKET_SETTING_EDIT);
	DuoProtocol::WriteS32(v, CmdIdx);
	DuoProtocol::WriteString(v, pCmd, str_length(pCmd));
	SendFrame(v);
}

void CDuoSession::SendSettingMove(int CmdIdx, int Direction)
{
	std::vector<uint8_t> v;
	DuoProtocol::WriteHeader(v, DuoProtocol::PACKET_SETTING_MOVE);
	DuoProtocol::WriteS32(v, CmdIdx);
	DuoProtocol::WriteS32(v, Direction);
	SendFrame(v);
}

void CDuoSession::NotifySettingAdd(const char *pCmd)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendSettingAdd(pCmd);
}

void CDuoSession::NotifySettingDel(int CmdIdx)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendSettingDel(CmdIdx);
}

void CDuoSession::NotifySettingEdit(int CmdIdx, const char *pCmd)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendSettingEdit(CmdIdx, pCmd);
}

void CDuoSession::NotifySettingMove(int CmdIdx, int Direction)
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendSettingMove(CmdIdx, Direction);
}

void CDuoSession::NotifyEditorSettings()
{
	if(m_State != STATE_LIVE || m_ApplyingRemote) return;
	SendEditorSettings();
}

void CDuoSession::SendMapStart(const char *pName, int TotalSize)
{
	std::vector<uint8_t> v;
	WriteHeader(v, PACKET_MAP_START);
	WriteS32(v, TotalSize);
	WriteString(v, pName, str_length(pName));
	SendFrame(v);
}

void CDuoSession::SendMapChunk(int Offset, const uint8_t *pData, int DataLen)
{
	std::vector<uint8_t> v;
	WriteHeader(v, PACKET_MAP_CHUNK);
	WriteS32(v, Offset);
	WriteU16(v, (uint16_t)DataLen);
	v.insert(v.end(), pData, pData + DataLen);
	SendFrame(v);
}

void CDuoSession::SendMapEnd()
{
	std::vector<uint8_t> v;
	WriteHeader(v, PACKET_MAP_END);
	SendFrame(v);
}

void CDuoSession::SendMapNew()
{
	if(m_State != STATE_LIVE || !m_IsCreator)
		return;
	std::vector<uint8_t> v;
	WriteHeader(v, PACKET_MAP_NEW);
	SendFrame(v);
}

void CDuoSession::SendEditorSettings()
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

void CDuoSession::SendActivity(EActivity Activity)
{
	if(m_Socket == nullptr)
		return;
	std::vector<uint8_t> v;
	WriteHeader(v, PACKET_ACTIVITY);
	WriteU8(v, static_cast<uint8_t>(Activity));
	SendFrame(v);
}

void CDuoSession::OnBackgroundUpdate()
{
	if(m_State < STATE_CONNECTING || m_Socket == nullptr)
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
}

void CDuoSession::StartMapTransfer()
{
	if(m_State != STATE_LIVE)
		return;

	static const char *s_pTmpPath = "duo_transfer_tmp.map";
	Editor()->Save(s_pTmpPath);
	m_PendingMapTransfer = true;
}

CUi::EPopupMenuFunctionResult CDuoSession::PopupDuoMain(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);
	CDuoSession *pDuo = &pEditor->m_DuoSession;
	CUIRect Slot;

	if(pDuo->m_State == STATE_ERROR && pDuo->m_aErrorMsg[0])
	{
		View.HSplitTop(12.0f, &Slot, &View);
		pEditor->Ui()->DoLabel(&Slot, pDuo->m_aErrorMsg, 10.0f, TEXTALIGN_MC);
		View.HSplitTop(4.0f, nullptr, &View);
	}

	if(pDuo->m_State == STATE_IDLE || pDuo->m_State == STATE_ERROR)
	{
		static int s_CreateButton = 0;
		View.HSplitTop(14.0f, &Slot, &View);
		if(pEditor->DoButton_MenuItem(&s_CreateButton, "Create room", 0, &Slot, BUTTONFLAG_LEFT, "Create a new duo mapping room."))
		{
			pDuo->Connect(nullptr, true);
			static SPopupMenuId s_PopupCreateId;
			pEditor->Ui()->DoPopupMenu(&s_PopupCreateId, View.x + View.w, View.y - 14.0f, 220.0f, 110.0f, pEditor, PopupDuoCreate);
			return CUi::POPUP_CLOSE_CURRENT;
		}

		View.HSplitTop(4.0f, nullptr, &View);
		static int s_JoinButton = 0;
		View.HSplitTop(14.0f, &Slot, &View);
		if(pEditor->DoButton_MenuItem(&s_JoinButton, "Join room", 0, &Slot, BUTTONFLAG_LEFT, "Join an existing duo mapping room."))
		{
			pDuo->m_aErrorMsg[0] = '\0';
			static SPopupMenuId s_PopupJoinId;
			pEditor->Ui()->DoPopupMenu(&s_PopupJoinId, View.x + View.w, View.y - 14.0f, 220.0f, 110.0f, pEditor, PopupDuoJoin);
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}
	else if(pDuo->m_State == STATE_WAITING || pDuo->m_State == STATE_LIVE)
	{
		View.HSplitTop(12.0f, &Slot, &View);
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "Room: %s", pDuo->m_aRoomCode);
		pEditor->Ui()->DoLabel(&Slot, aBuf, 10.0f, TEXTALIGN_ML);

		View.HSplitTop(12.0f, &Slot, &View);
		str_format(aBuf, sizeof(aBuf), "Players: %d / 2", pDuo->m_ParticipantCount);
		pEditor->Ui()->DoLabel(&Slot, aBuf, 10.0f, TEXTALIGN_ML);

		if(pDuo->m_State == STATE_LIVE)
		{
			View.HSplitTop(12.0f, &Slot, &View);
			str_format(aBuf, sizeof(aBuf), "Q tx:%d rx:%d", pDuo->m_DbgQuadSent, pDuo->m_DbgQuadRecv);
			pEditor->Ui()->DoLabel(&Slot, aBuf, 10.0f, TEXTALIGN_ML);
		}
	}

	if(pDuo->m_State == STATE_CONNECTING || pDuo->m_State == STATE_WAITING || pDuo->m_State == STATE_LIVE)
	{
		View.HSplitTop(4.0f, nullptr, &View);
		static int s_DisconnectButton = 0;
		View.HSplitTop(14.0f, &Slot, &View);
		if(pEditor->DoButton_MenuItem(&s_DisconnectButton, "Disconnect", 0, &Slot, BUTTONFLAG_LEFT, "Leave the duo session."))
		{
			pDuo->Disconnect();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CDuoSession::PopupDuoCreate(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);
	CDuoSession *pDuo = &pEditor->m_DuoSession;
	CUIRect Slot;

	View.HSplitTop(14.0f, &Slot, &View);
	char aBuf[64];
	if(pDuo->m_aRoomCode[0])
		str_format(aBuf, sizeof(aBuf), "Code: %s", pDuo->m_aRoomCode);
	else
		str_copy(aBuf, "Connecting...");
	pEditor->Ui()->DoLabel(&Slot, aBuf, 10.0f, TEXTALIGN_MC);

	View.HSplitTop(14.0f, &Slot, &View);
	str_format(aBuf, sizeof(aBuf), "Players: %d / 2", pDuo->m_ParticipantCount);
	pEditor->Ui()->DoLabel(&Slot, aBuf, 10.0f, TEXTALIGN_MC);

	if(pDuo->m_aRoomCode[0])
	{
		View.HSplitTop(4.0f, nullptr, &View);
		static int s_CopyButton = 0;
		View.HSplitTop(14.0f, &Slot, &View);
		if(pEditor->DoButton_MenuItem(&s_CopyButton, "Copy Code", 0, &Slot, BUTTONFLAG_LEFT, "Copy room code to clipboard."))
			pEditor->Input()->SetClipboardText(pDuo->m_aRoomCode);
	}

	if(pDuo->m_ParticipantCount >= 2)
	{
		View.HSplitTop(4.0f, nullptr, &View);
		static int s_StartButton = 0;
		View.HSplitTop(14.0f, &Slot, &View);
		if(pEditor->DoButton_MenuItem(&s_StartButton, "Start", 0, &Slot, BUTTONFLAG_LEFT, "Begin collaborative editing."))
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

CUi::EPopupMenuFunctionResult CDuoSession::PopupDuoJoin(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);
	CDuoSession *pDuo = &pEditor->m_DuoSession;
	CUIRect Slot;

	// подключились — окно остаётся открытым с инфой о сессии,
	// джойнер закрывает его вручную (как у создателя)
	if(pDuo->m_State == STATE_WAITING || pDuo->m_State == STATE_LIVE)
	{
		char aBuf[64];
		View.HSplitTop(14.0f, &Slot, &View);
		str_format(aBuf, sizeof(aBuf), "Room: %s", pDuo->m_aRoomCode);
		pEditor->Ui()->DoLabel(&Slot, aBuf, 10.0f, TEXTALIGN_MC);

		View.HSplitTop(14.0f, &Slot, &View);
		str_format(aBuf, sizeof(aBuf), "Players: %d / 2", pDuo->m_ParticipantCount);
		pEditor->Ui()->DoLabel(&Slot, aBuf, 10.0f, TEXTALIGN_MC);

		View.HSplitTop(14.0f, &Slot, &View);
		const char *pStatus = pDuo->m_State == STATE_LIVE ? "Connected!" : "Waiting for host...";
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
	s_CodeInput.SetBuffer(pDuo->m_aJoinCodeInput, sizeof(pDuo->m_aJoinCodeInput));

	bool bConnecting = pDuo->m_State == STATE_CONNECTING;
	if(bConnecting)
		pEditor->Ui()->DoLabel(&Slot, pDuo->m_aJoinCodeInput, 10.0f, TEXTALIGN_ML);
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
			if(str_length(pDuo->m_aJoinCodeInput) == ROOM_CODE_LEN)
				pDuo->Connect(pDuo->m_aJoinCodeInput, false);
		}
	}

	// ошибка (неверный код, комната не найдена, полная)
	if(pDuo->m_State == STATE_ERROR && pDuo->m_aErrorMsg[0])
	{
		View.HSplitTop(6.0f, nullptr, &View);
		View.HSplitTop(14.0f, &Slot, &View);
		pEditor->Ui()->DoLabel(&Slot, pDuo->m_aErrorMsg, 9.0f, TEXTALIGN_MC);
		View.HSplitTop(4.0f, nullptr, &View);
		static int s_RetryButton = 0;
		View.HSplitTop(14.0f, &Slot, &View);
		if(pEditor->DoButton_MenuItem(&s_RetryButton, "Try again", 0, &Slot, BUTTONFLAG_LEFT, "Clear error and try again."))
		{
			pDuo->m_State = STATE_IDLE;
			pDuo->m_aErrorMsg[0] = '\0';
		}
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CDuoSession::PopupDuo(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);
	CDuoSession *pDuo = &pEditor->m_DuoSession;
	CUIRect Slot;
	static int64_t s_CopiedTime = 0;

	View.Margin(6.0f, &View);

	if(pDuo->m_State == STATE_IDLE || pDuo->m_State == STATE_ERROR)
	{
		// title
		View.HSplitTop(14.0f, &Slot, &View);
		pEditor->Ui()->DoLabel(&Slot, "Duo Mapping", 11.0f, TEXTALIGN_MC);
		View.HSplitTop(5.0f, nullptr, &View);

		// create room
		static int s_CreateButton = 0;
		View.HSplitTop(16.0f, &Slot, &View);
		if(pEditor->DoButton_Editor(&s_CreateButton, "Create room", 0, &Slot, BUTTONFLAG_LEFT, "Create a new collaboration room."))
			pDuo->Connect(nullptr, true);

		View.HSplitTop(5.0f, nullptr, &View);

		// join room label
		View.HSplitTop(12.0f, &Slot, &View);
		pEditor->Ui()->DoLabel(&Slot, "Join room:", 10.0f, TEXTALIGN_ML);
		View.HSplitTop(2.0f, nullptr, &View);

		// code input
		View.HSplitTop(16.0f, &Slot, &View);
		static CLineInput s_CodeInput;
		s_CodeInput.SetBuffer(pDuo->m_aJoinCodeInput, sizeof(pDuo->m_aJoinCodeInput));
		pEditor->DoEditBox(&s_CodeInput, &Slot, 10.0f);

		View.HSplitTop(3.0f, nullptr, &View);

		// connect
		static int s_ConnectButton = 0;
		View.HSplitTop(16.0f, &Slot, &View);
		if(pEditor->DoButton_Editor(&s_ConnectButton, "Connect", 0, &Slot, BUTTONFLAG_LEFT, "Connect to the room."))
		{
			if(str_length(pDuo->m_aJoinCodeInput) == ROOM_CODE_LEN)
				pDuo->Connect(pDuo->m_aJoinCodeInput, false);
		}

		if(pDuo->m_State == STATE_ERROR && pDuo->m_aErrorMsg[0])
		{
			View.HSplitTop(5.0f, nullptr, &View);
			View.HSplitTop(12.0f, &Slot, &View);
			pEditor->Ui()->DoLabel(&Slot, pDuo->m_aErrorMsg, 9.0f, TEXTALIGN_MC);
			View.HSplitTop(3.0f, nullptr, &View);
			static int s_RetryButton2 = 0;
			View.HSplitTop(16.0f, &Slot, &View);
			if(pEditor->DoButton_Editor(&s_RetryButton2, "Try again", 0, &Slot, BUTTONFLAG_LEFT, "Clear error and try again."))
			{
				pDuo->m_State = STATE_IDLE;
				pDuo->m_aErrorMsg[0] = '\0';
			}
		}
	}
	else if(pDuo->m_State == STATE_CONNECTING)
	{
		View.HSplitTop(14.0f, &Slot, &View);
		pEditor->Ui()->DoLabel(&Slot, "Duo Mapping", 11.0f, TEXTALIGN_MC);
		View.HSplitTop(8.0f, nullptr, &View);
		View.HSplitTop(14.0f, &Slot, &View);
		pEditor->Ui()->DoLabel(&Slot, "Connecting...", 10.0f, TEXTALIGN_MC);
		View.HSplitTop(6.0f, nullptr, &View);
		static int s_CancelButton = 0;
		View.HSplitTop(16.0f, &Slot, &View);
		if(pEditor->DoButton_Editor(&s_CancelButton, "Cancel", 0, &Slot, BUTTONFLAG_LEFT, "Cancel connection."))
			pDuo->Disconnect();
	}
	else if(pDuo->m_State == STATE_WAITING || pDuo->m_State == STATE_LIVE)
	{
		View.HSplitTop(14.0f, &Slot, &View);
		pEditor->Ui()->DoLabel(&Slot, "Duo Mapping", 11.0f, TEXTALIGN_MC);
		View.HSplitTop(5.0f, nullptr, &View);

		// room code row: label left, copy button right, same height
		View.HSplitTop(16.0f, &Slot, &View);
		CUIRect CodeRect, CopyRect;
		Slot.VSplitRight(58.0f, &CodeRect, &CopyRect);
		char aCodeLabel[32];
		str_format(aCodeLabel, sizeof(aCodeLabel), "Room: %s", pDuo->m_aRoomCode);
		pEditor->Ui()->DoLabel(&CodeRect, aCodeLabel, 10.0f, TEXTALIGN_ML);

		static int s_CopyButton = 0;
		bool bJustCopied = (time_get() - s_CopiedTime) < time_freq();
		if(pEditor->DoButton_Editor(&s_CopyButton, bJustCopied ? "Copied!" : "Copy", bJustCopied ? 1 : 0, &CopyRect, BUTTONFLAG_LEFT, "Copy room code to clipboard."))
		{
			pEditor->Input()->SetClipboardText(pDuo->m_aRoomCode);
			s_CopiedTime = time_get();
		}

		View.HSplitTop(4.0f, nullptr, &View);
		char aPlayers[32];
		str_format(aPlayers, sizeof(aPlayers), "Players: %d/2", pDuo->m_ParticipantCount);
		View.HSplitTop(13.0f, &Slot, &View);
		pEditor->Ui()->DoLabel(&Slot, aPlayers, 10.0f, TEXTALIGN_ML);

		View.HSplitTop(2.0f, nullptr, &View);
		View.HSplitTop(13.0f, &Slot, &View);
		pEditor->Ui()->DoLabel(&Slot, pDuo->m_IsCreator ? "Role: Owner" : "Role: Joiner", 10.0f, TEXTALIGN_ML);

		View.HSplitTop(4.0f, nullptr, &View);
		View.HSplitTop(13.0f, &Slot, &View);
		pEditor->Ui()->DoLabel(&Slot, pDuo->m_State == STATE_WAITING ? "Waiting for partner..." : "Connected!", 10.0f, TEXTALIGN_MC);

		if(pDuo->m_MapTransferActive && pDuo->m_MapTransferTotal > 0)
		{
			View.HSplitTop(4.0f, nullptr, &View);
			View.HSplitTop(13.0f, &Slot, &View);
			char aProgress[64];
			int Pct = (int)(100.0f * pDuo->m_MapTransferReceived / pDuo->m_MapTransferTotal);
			str_format(aProgress, sizeof(aProgress), "Receiving map... %d%%", Pct);
			pEditor->Ui()->DoLabel(&Slot, aProgress, 9.0f, TEXTALIGN_MC);

			View.HSplitTop(3.0f, nullptr, &View);
			View.HSplitTop(8.0f, &Slot, &View);
			CUIRect BarBg = Slot;
			CUIRect BarFill = Slot;
			BarFill.w = Slot.w * (float)pDuo->m_MapTransferReceived / pDuo->m_MapTransferTotal;
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
			pDuo->Disconnect();
	}

	return CUi::POPUP_KEEP_OPEN;
}
