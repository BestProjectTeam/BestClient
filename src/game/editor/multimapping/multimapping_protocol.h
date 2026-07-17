#ifndef GAME_EDITOR_MULTIMAPPING_PROTOCOL_H
#define GAME_EDITOR_MULTIMAPPING_PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <vector>

namespace MultiMappingProtocol {

constexpr uint32_t MULTIMAPPING_MAGIC = 0x444D5031; // "DMP1"
constexpr uint8_t MULTIMAPPING_VERSION = 2;
constexpr int MULTIMAPPING_DEFAULT_PORT = 5555;
constexpr int MULTIMAPPING_HEADER_SIZE = 6;
constexpr int ROOM_CODE_LEN = 6;
constexpr int SESSION_TOKEN_SIZE = 16;
constexpr int HMAC_SIZE = 32;
constexpr int MAX_PACKET_SIZE = 65535;
constexpr int MAX_MULTIMAPPING_PLAYERS = 8;
constexpr int MAX_NICKNAME_LEN = 16; // matches g_Config.m_PlayerName limit, config_variables.h

enum EPacketType : uint8_t
{
	PACKET_HELLO = 1,
	PACKET_HELLO_ACK = 2,
	PACKET_ROOM_STATE = 3,
	PACKET_START = 4,
	PACKET_HEARTBEAT = 5,
	PACKET_CURSOR = 6,
	PACKET_CURSOR_RELAY = 7,
	PACKET_TILE_EDIT = 8,
	PACKET_TILE_RELAY = 9,
	PACKET_GOODBYE = 10,
	PACKET_ERROR = 11,
	PACKET_SYNC_CHECK = 12,         // sender в†’ relay в†’ receiver: GroupIdx(4)+LayerIdx(4)+CRC32(4)
	PACKET_SYNC_REQUEST = 13,       // receiver в†’ relay в†’ sender: GroupIdx(4)+LayerIdx(4)
	PACKET_SYNC_DATA = 14,          // sender в†’ relay в†’ receiver: GroupIdx(4)+LayerIdx(4)+W(4)+H(4)+Row(4)+tiles(W*4)
	PACKET_STRUCT_ADD_GROUP = 15,   // InsertIdx(4): -1 = append, else insert at index
	PACKET_STRUCT_DEL_GROUP = 16,   // GroupIdx(4)
	PACKET_STRUCT_ADD_LAYER = 17,   // GroupIdx(4)+LayerIdx(4)+Type(1)+Name(2+N)
	PACKET_STRUCT_DEL_LAYER = 18,   // GroupIdx(4)+LayerIdx(4)
	PACKET_STRUCT_SET_IMAGE = 19,   // GroupIdx(4)+LayerIdx(4)+ImageIdx(4)
	PACKET_STRUCT_RENAME_GROUP = 20,// GroupIdx(4)+Name(2+N)
	PACKET_STRUCT_RENAME_LAYER = 21,// GroupIdx(4)+LayerIdx(4)+Name(2+N)
	PACKET_STRUCT_LAYER_PROP = 22,  // GroupIdx(4)+LayerIdx(4)+PropId(1)+Value(4)
	PACKET_STRUCT_ADD_IMAGE = 23,   // External(1)+Name(2+N)+RawData(4+N)
	PACKET_STRUCT_DEL_IMAGE = 24,   // ImageIdx(4)
	PACKET_STRUCT_EMBED_IMAGE = 25, // ImageIdx(4)+RawData(4+N)  (externalв†’embedded)
	PACKET_STRUCT_EXTERN_IMAGE = 26,// ImageIdx(4)               (embeddedв†’external)
	// Quad sync
	PACKET_QUAD_ADD        = 27,    // GroupIdx(4)+LayerIdx(4)+QuadIdx(4)+QuadData(N)
	PACKET_QUAD_DEL        = 28,    // GroupIdx(4)+LayerIdx(4)+QuadIdx(4)
	PACKET_QUAD_POINTS     = 29,    // GroupIdx(4)+LayerIdx(4)+QuadIdx(4)+Points(5*8)
	PACKET_QUAD_COLORS     = 30,    // GroupIdx(4)+LayerIdx(4)+QuadIdx(4)+Colors(4*16)
	PACKET_QUAD_PROP       = 31,    // GroupIdx(4)+LayerIdx(4)+QuadIdx(4)+Prop(1)+Value(4)
	PACKET_QUAD_POINT_PROP = 32,    // GroupIdx(4)+LayerIdx(4)+QuadIdx(4)+PointIdx(1)+Prop(1)+Value(4)
	PACKET_LAYER_FLAGS     = 33,    // GroupIdx(4)+LayerIdx(4)+Flags(4)
	PACKET_GROUP_PROP      = 34,    // GroupIdx(4)+PropId(1)+Value(4)
	PACKET_SETTING_ADD     = 35,    // Command(2+N)
	PACKET_SETTING_DEL     = 36,    // CmdIdx(4)
	PACKET_SETTING_EDIT    = 37,    // CmdIdx(4)+Command(2+N)
	PACKET_SETTING_MOVE    = 38,    // CmdIdx(4)+Direction(4)
	PACKET_MAP_START       = 39,    // TotalSize(4)+NameLen(2)+Name(N)
	PACKET_MAP_CHUNK       = 40,    // Offset(4)+DataLen(2)+Data(N)
	PACKET_MAP_END         = 41,    // no payload
	PACKET_MAP_NEW         = 42,    // no payload вЂ” owner tells joiner to reset to empty map
	PACKET_EDITOR_SETTINGS = 43,    // settings sync: BrushColor(1)+AllowUnused(1)+ShowInfo(1)+EnvPreview(1)+AlignQuads(1)+ShowQuadsRect(1)+AutoReload(1)+LayerSelector(1)+ShowIngame(1)
	PACKET_ACTIVITY        = 44,    // activity state(1) вЂ” what the user is currently doing
	PACKET_STRUCT_ADD_SOUND = 45,  // Name(2+N)+DataSize(4)+Data(N)
	PACKET_STRUCT_DEL_SOUND = 46,  // SoundIdx(4)
	PACKET_STRUCT_SET_SOUND = 47,  // GroupIdx(4)+LayerIdx(4)+SoundIdx(4)
	PACKET_STRUCT_ADD_SOUND_SOURCE  = 48, // GroupIdx(4)+LayerIdx(4)+SourceIdx(4)+SourceData
	PACKET_STRUCT_DEL_SOUND_SOURCE  = 49, // GroupIdx(4)+LayerIdx(4)+SourceIdx(4)
	PACKET_STRUCT_EDIT_SOUND_SOURCE = 50, // GroupIdx(4)+LayerIdx(4)+SourceIdx(4)+PropId(1)+Value(4)
	// PropId values for EDIT_SOUND_SOURCE beyond ESoundProp:
	// 20 = shape type, 21 = shape width/radius, 22 = shape height
	PACKET_PING = 51, // no payload вЂ” forwarded to partner by server
	PACKET_PONG = 52, // no payload вЂ” reply to PING
	PACKET_ROSTER = 53, // serverв†’all: Count(1)+NГ—[SlotId(1)+IsCreator(1)+Connected(1)+NickLen(1)+Nick(N)]
};

enum EActivity : uint8_t
{
	ACTIVITY_MAPPING = 0,
	ACTIVITY_DIALOG = 1,
	ACTIVITY_ENVELOPES = 2,
	ACTIVITY_SETTINGS = 3,
	ACTIVITY_TESTING = 4,
	ACTIVITY_AWAY = 5,
	ACTIVITY_PICKER = 6,
};

enum EErrorCode : uint8_t
{
	ERROR_ROOM_FULL = 1,
	ERROR_ROOM_NOT_FOUND = 2,
	ERROR_AUTH_FAILED = 3,
	ERROR_RATE_LIMITED = 4,
	ERROR_REPLAY_DETECTED = 5,
	ERROR_BAD_MAP = 6,
};

inline void WriteU8(std::vector<uint8_t> &v, uint8_t val) { v.push_back(val); }

inline void WriteU16(std::vector<uint8_t> &v, uint16_t val)
{
	v.push_back((val >> 8) & 0xFF);
	v.push_back(val & 0xFF);
}

inline void WriteU32(std::vector<uint8_t> &v, uint32_t val)
{
	v.push_back((val >> 24) & 0xFF);
	v.push_back((val >> 16) & 0xFF);
	v.push_back((val >> 8) & 0xFF);
	v.push_back(val & 0xFF);
}

inline void WriteS32(std::vector<uint8_t> &v, int32_t val)
{
	WriteU32(v, static_cast<uint32_t>(val));
}

inline void WriteU64(std::vector<uint8_t> &v, uint64_t val)
{
	WriteU32(v, static_cast<uint32_t>(val >> 32));
	WriteU32(v, static_cast<uint32_t>(val & 0xFFFFFFFF));
}

inline void WriteBytes(std::vector<uint8_t> &v, const uint8_t *pData, int Size)
{
	v.insert(v.end(), pData, pData + Size);
}

inline void WriteString(std::vector<uint8_t> &v, const char *pStr, int Len)
{
	WriteU16(v, static_cast<uint16_t>(Len));
	v.insert(v.end(), reinterpret_cast<const uint8_t *>(pStr), reinterpret_cast<const uint8_t *>(pStr) + Len);
}

inline void WriteHeader(std::vector<uint8_t> &v, EPacketType Type)
{
	WriteU32(v, MULTIMAPPING_MAGIC);
	WriteU8(v, static_cast<uint8_t>(Type));
	WriteU8(v, MULTIMAPPING_VERSION);
}

class CPacketReader
{
	const uint8_t *m_pData;
	int m_Size;
	int m_Offset;

public:
	CPacketReader(const uint8_t *pData, int Size) :
		m_pData(pData), m_Size(Size), m_Offset(0) {}

	bool HasBytes(int n) const { return n >= 0 && (uint32_t)m_Offset + (uint32_t)n <= (uint32_t)m_Size; }
	int Remaining() const { return m_Size - m_Offset; }

	uint8_t ReadU8()
	{
		if(!HasBytes(1)) return 0;
		return m_pData[m_Offset++];
	}

	uint16_t ReadU16()
	{
		if(!HasBytes(2)) return 0;
		uint16_t val = (static_cast<uint16_t>(m_pData[m_Offset]) << 8) | m_pData[m_Offset + 1];
		m_Offset += 2;
		return val;
	}

	uint32_t ReadU32()
	{
		if(!HasBytes(4)) return 0;
		uint32_t val = (static_cast<uint32_t>(m_pData[m_Offset]) << 24) |
			(static_cast<uint32_t>(m_pData[m_Offset + 1]) << 16) |
			(static_cast<uint32_t>(m_pData[m_Offset + 2]) << 8) |
			m_pData[m_Offset + 3];
		m_Offset += 4;
		return val;
	}

	int32_t ReadS32() { return static_cast<int32_t>(ReadU32()); }

	uint64_t ReadU64()
	{
		uint64_t hi = ReadU32();
		uint64_t lo = ReadU32();
		return (hi << 32) | lo;
	}

	bool ReadBytes(uint8_t *pOut, int Size)
	{
		if(!HasBytes(Size)) return false;
		memcpy(pOut, m_pData + m_Offset, Size);
		m_Offset += Size;
		return true;
	}

	bool ValidateHeader(EPacketType *pType)
	{
		if(!HasBytes(MULTIMAPPING_HEADER_SIZE)) return false;
		uint32_t Magic = ReadU32();
		if(Magic != MULTIMAPPING_MAGIC) return false;
		*pType = static_cast<EPacketType>(ReadU8());
		uint8_t Version = ReadU8();
		return Version == MULTIMAPPING_VERSION;
	}
};

} // namespace MultiMappingProtocol

#endif // GAME_EDITOR_MULTIMAPPING_PROTOCOL_H
