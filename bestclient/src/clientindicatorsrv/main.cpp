#include <base/logger.h>
#include <base/system.h>

#include "timestamp_compat.h"

#include <engine/shared/bestclient_indicator_protocol.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/uuid_manager.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
constexpr const char *LOG_SCOPE = "clientindicator-srv";
constexpr int64_t NONCE_RETENTION_SECONDS = 60;
constexpr int64_t HEARTBEAT_TIMEOUT_SECONDS = 15;
constexpr int MAX_UDP_PACKET_SIZE = 2048;

std::atomic_bool gs_StopRequested = false;

void SignalHandler(int)
{
	gs_StopRequested.store(true);
}

struct CConfig
{
	NETADDR m_UdpBind = NETADDR_ZEROED;
	std::string m_SharedToken;
	std::string m_JsonPath;
};

struct CIdentity
{
	CUuid m_InstanceId = UUID_ZEROED;
	int m_ClientId = -1;
};

struct CPresenceEntry
{
	CIdentity m_Identity;
	std::string m_ServerAddress;
	std::string m_PlayerName;
	NETADDR m_RemoteAddr = NETADDR_ZEROED;
	int64_t m_LastSeen = 0;
};

struct CPeerPacketBuffer
{
	std::vector<uint8_t> m_vData;
};

std::string FormatUuidString(CUuid Uuid)
{
	char aUuid[UUID_MAXSTRSIZE];
	FormatUuid(Uuid, aUuid, sizeof(aUuid));
	return aUuid;
}

std::string MakeIdentityKey(CUuid InstanceId, int ClientId)
{
	char aKey[UUID_MAXSTRSIZE + 32];
	str_format(aKey, sizeof(aKey), "%s/%d", FormatUuidString(InstanceId).c_str(), ClientId);
	return aKey;
}

std::string MakeNonceKey(CUuid InstanceId, CUuid Nonce)
{
	char aKey[(UUID_MAXSTRSIZE * 2) + 4];
	str_format(aKey, sizeof(aKey), "%s/%s", FormatUuidString(InstanceId).c_str(), FormatUuidString(Nonce).c_str());
	return aKey;
}

std::string NetAddrToString(const NETADDR &Addr)
{
	char aBuffer[NETADDR_MAXSTRSIZE];
	net_addr_str(&Addr, aBuffer, sizeof(aBuffer), true);
	return aBuffer;
}

bool WriteFileAtomically(const char *pPath, const std::string &Contents)
{
	if(fs_makedir_rec_for(pPath) != 0)
	{
		log_error(LOG_SCOPE, "failed to create directory for '%s'", pPath);
		return false;
	}

	char aTmpPath[IO_MAX_PATH_LENGTH];
	str_format(aTmpPath, sizeof(aTmpPath), "%s.tmp", pPath);
	IOHANDLE File = io_open(aTmpPath, IOFLAG_WRITE);
	if(!File)
	{
		log_error(LOG_SCOPE, "failed to open '%s' for writing", aTmpPath);
		return false;
	}

	const unsigned BytesWritten = io_write(File, Contents.data(), Contents.size());
	const bool FlushOk = io_flush(File) == 0;
	io_close(File);

	if(BytesWritten != Contents.size() || !FlushOk)
	{
		log_error(LOG_SCOPE, "failed to write snapshot to '%s'", aTmpPath);
		return false;
	}

	if(fs_rename(aTmpPath, pPath) != 0)
	{
		log_error(LOG_SCOPE, "failed to rename '%s' to '%s'", aTmpPath, pPath);
		return false;
	}

	return true;
}

std::string BuildJsonSnapshot(const std::unordered_map<std::string, CPresenceEntry> &Entries)
{
	struct CSortedPlayer
	{
		std::string m_Name;
		int m_ClientId = -1;
		CUuid m_InstanceId = UUID_ZEROED;
		int64_t m_LastSeen = 0;
	};

	struct CSortedServer
	{
		std::string m_Address;
		std::vector<CSortedPlayer> m_vPlayers;
	};

	std::unordered_map<std::string, std::vector<CSortedPlayer>> PlayersByServer;
	for(const auto &[Key, Entry] : Entries)
	{
		(void)Key;
		auto &vPlayers = PlayersByServer[Entry.m_ServerAddress];
		vPlayers.push_back({Entry.m_PlayerName, Entry.m_Identity.m_ClientId, Entry.m_Identity.m_InstanceId, Entry.m_LastSeen});
	}

	std::vector<CSortedServer> vServers;
	vServers.reserve(PlayersByServer.size());
	for(auto &[Address, vPlayers] : PlayersByServer)
	{
		std::sort(vPlayers.begin(), vPlayers.end(), [](const CSortedPlayer &Left, const CSortedPlayer &Right) {
			if(Left.m_Name != Right.m_Name)
				return Left.m_Name < Right.m_Name;
			if(Left.m_ClientId != Right.m_ClientId)
				return Left.m_ClientId < Right.m_ClientId;
			return Left.m_LastSeen < Right.m_LastSeen;
		});
		vServers.push_back({std::move(Address), std::move(vPlayers)});
	}

	std::sort(vServers.begin(), vServers.end(), [](const CSortedServer &Left, const CSortedServer &Right) {
		return Left.m_Address < Right.m_Address;
	});

	CJsonStringWriter Json;
	Json.BeginArray();
	for(const auto &Server : vServers)
	{
		Json.BeginObject();
		Json.WriteAttribute("server_address");
		Json.WriteStrValue(Server.m_Address.c_str());
		Json.WriteAttribute("players");
		Json.BeginArray();
		for(const auto &Player : Server.m_vPlayers)
		{
			char aInstanceId[UUID_MAXSTRSIZE];
			FormatUuid(Player.m_InstanceId, aInstanceId, sizeof(aInstanceId));

			Json.BeginObject();
			Json.WriteAttribute("name");
			Json.WriteStrValue(Player.m_Name.c_str());
			Json.WriteAttribute("client_id");
			Json.WriteIntValue(Player.m_ClientId);
			Json.WriteAttribute("instance_id");
			Json.WriteStrValue(aInstanceId);
			Json.WriteAttribute("last_seen");
			Json.WriteIntValue((int)Player.m_LastSeen);
			Json.EndObject();
		}
		Json.EndArray();
		Json.EndObject();
	}
	Json.EndArray();
	return Json.GetOutputString();
}

bool ParseCli(int argc, const char **argv, CConfig &Config)
{
	Config.m_UdpBind.type = NETTYPE_ALL;
	Config.m_UdpBind.port = BestClientIndicator::DEFAULT_PORT;

	for(int i = 1; i < argc; ++i)
	{
		const char *pArg = argv[i];
		const auto RequireValue = [&](const char *pName) -> const char * {
			if(i + 1 >= argc)
			{
				log_error(LOG_SCOPE, "missing value for %s", pName);
				return nullptr;
			}
			return argv[++i];
		};

		if(str_comp(pArg, "--udp-bind") == 0)
		{
			const char *pValue = RequireValue("--udp-bind");
			if(!pValue || !BestClientIndicator::ParseAddress(pValue, BestClientIndicator::DEFAULT_PORT, Config.m_UdpBind))
				return false;
		}
		else if(str_comp(pArg, "--shared-token") == 0)
		{
			const char *pValue = RequireValue("--shared-token");
			if(!pValue)
				return false;
			Config.m_SharedToken = pValue;
		}
		else if(str_comp(pArg, "--json-path") == 0)
		{
			const char *pValue = RequireValue("--json-path");
			if(!pValue)
				return false;
			Config.m_JsonPath = pValue;
		}
		else if(str_comp(pArg, "--help") == 0 || str_comp(pArg, "-h") == 0)
		{
			log_info(LOG_SCOPE, "usage: bestclient-indicator-server --shared-token <token> --json-path <path> [--udp-bind <addr:port>]");
			return false;
		}
		else
		{
			log_error(LOG_SCOPE, "unknown argument '%s'", pArg);
			return false;
		}
	}

	if(Config.m_SharedToken.empty() || Config.m_JsonPath.empty())
	{
		log_error(LOG_SCOPE, "--shared-token and --json-path are required");
		return false;
	}

	return true;
}

class CIndicatorServer
{
	CConfig m_Config;
	NETSOCKET m_UdpSocket = nullptr;
	std::unordered_map<std::string, CPresenceEntry> m_Entries;
	std::unordered_map<std::string, int64_t> m_RecentNonces;
	std::unordered_map<std::string, int64_t> m_LastCompatTimestampLogByAddr;
	bool m_SnapshotDirty = true;
	int64_t m_LastSnapshotWrite = 0;
	int64_t m_LastCleanupTick = 0;

public:
	explicit CIndicatorServer(CConfig Config) :
		m_Config(std::move(Config))
	{
	}

	~CIndicatorServer()
	{
		Shutdown();
	}

	bool Init()
	{
		m_UdpSocket = net_udp_create(m_Config.m_UdpBind);
		if(!m_UdpSocket)
		{
			log_error(LOG_SCOPE, "failed to bind UDP socket to %s", NetAddrToString(m_Config.m_UdpBind).c_str());
			return false;
		}
		net_set_non_blocking(m_UdpSocket);

		RefreshSnapshot(true);
		log_info(LOG_SCOPE, "udp bind: %s", NetAddrToString(m_Config.m_UdpBind).c_str());
		log_info(LOG_SCOPE, "json path: %s", m_Config.m_JsonPath.c_str());
		return true;
	}

	int Run()
	{
		while(!gs_StopRequested.load())
		{
			ProcessUdpPackets();
			RunCleanupTick();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		return 0;
	}

	void Shutdown()
	{
		if(m_UdpSocket)
		{
			net_udp_close(m_UdpSocket);
			m_UdpSocket = nullptr;
		}
	}

private:
	BestClientIndicatorCompat::CTimestampCheckResult CheckTimestampCompatibility(int64_t Timestamp) const
	{
		const int64_t Now = time_timestamp();
		return BestClientIndicatorCompat::CheckTimestamp(Now, Timestamp);
	}

	void LogTimestampCompatibility(const NETADDR &From, const BestClientIndicatorCompat::CTimestampCheckResult &TimestampCheck)
	{
		if(!TimestampCheck.UsesCompatibilityPath())
			return;

		const std::string AddrString = NetAddrToString(From);
		auto It = m_LastCompatTimestampLogByAddr.find(AddrString);
		if(It != m_LastCompatTimestampLogByAddr.end() &&
			It->second + BestClientIndicatorCompat::TIMESTAMP_COMPAT_LOG_INTERVAL_SECONDS > TimestampCheck.m_Now)
		{
			return;
		}
		m_LastCompatTimestampLogByAddr[AddrString] = TimestampCheck.m_Now;

		// Temporary compatibility path: some already-deployed clients still send stale timestamps.
		// Keep proof + nonce checks strict for now and remove this once the improved timestamp format ships.
		log_info(LOG_SCOPE, "timestamp outside window from %s (skew=%" PRId64 "s), accepting via compatibility path", AddrString.c_str(), TimestampCheck.m_SkewSeconds);
	}

	bool RememberNonce(CUuid InstanceId, CUuid Nonce)
	{
		const int64_t Now = time_timestamp();
		const std::string Key = MakeNonceKey(InstanceId, Nonce);
		auto It = m_RecentNonces.find(Key);
		if(It != m_RecentNonces.end() && It->second + NONCE_RETENTION_SECONDS >= Now)
			return false;
		m_RecentNonces[Key] = Now;
		return true;
	}

	void CleanupNonces(int64_t Now)
	{
		for(auto It = m_RecentNonces.begin(); It != m_RecentNonces.end();)
		{
			if(It->second + NONCE_RETENTION_SECONDS < Now)
				It = m_RecentNonces.erase(It);
			else
				++It;
		}
	}

	void ProcessUdpPackets()
	{
		if(!m_UdpSocket)
			return;

		for(int i = 0; i < BestClientIndicator::MAX_RECEIVE_PACKETS_PER_TICK; ++i)
		{
			NETADDR From = NETADDR_ZEROED;
			unsigned char *pData = nullptr;
			const int DataSize = net_udp_recv(m_UdpSocket, &From, &pData);
			if(DataSize <= 0 || !pData)
			{
				if(DataSize < 0 && !net_would_block())
					log_error(LOG_SCOPE, "udp recv failed");
				break;
			}
			if(DataSize > MAX_UDP_PACKET_SIZE)
				continue;
			HandleUdpPacket(From, pData, DataSize);
		}
	}

	void HandleUdpPacket(const NETADDR &From, const unsigned char *pData, int DataSize)
	{
		BestClientIndicator::CClientPresencePacket Packet;
		if(!BestClientIndicator::ReadClientPresencePacket(pData, DataSize, Packet))
			return;

		if(!BestClientIndicator::ValidateProof(m_Config.m_SharedToken.c_str(), pData, DataSize))
		{
			log_info(LOG_SCOPE, "invalid proof from %s", NetAddrToString(From).c_str());
			return;
		}

		// Temporary compatibility mode: do not reject stale timestamps from already-deployed clients.
		// Proof validation and nonce replay protection remain the hard admission checks.
		const auto TimestampCheck = CheckTimestampCompatibility((int64_t)Packet.m_Timestamp);
		LogTimestampCompatibility(From, TimestampCheck);

		if(!RememberNonce(Packet.m_ClientInstanceId, Packet.m_Nonce))
		{
			log_info(LOG_SCOPE, "replay rejected for %s", NetAddrToString(From).c_str());
			return;
		}

		if(Packet.m_ClientId < 0 || Packet.m_ClientId > 255 || Packet.m_ServerAddress.empty())
			return;

		switch(Packet.m_Type)
		{
		case BestClientIndicator::PACKET_JOIN:
			HandleJoinOrHeartbeat(Packet, From, true);
			break;
		case BestClientIndicator::PACKET_HEARTBEAT:
			HandleJoinOrHeartbeat(Packet, From, false);
			break;
		case BestClientIndicator::PACKET_LEAVE:
			HandleLeave(Packet);
			break;
		default:
			break;
		}
	}

	void HandleJoinOrHeartbeat(const BestClientIndicator::CClientPresencePacket &Packet, const NETADDR &From, bool IsJoin)
	{
		const int64_t Now = time_timestamp();
		const std::string IdentityKey = MakeIdentityKey(Packet.m_ClientInstanceId, Packet.m_ClientId);
		auto It = m_Entries.find(IdentityKey);
		const bool Exists = It != m_Entries.end();
		const bool TreatAsJoin = IsJoin || !Exists;

		CPresenceEntry OldEntry;
		if(Exists)
			OldEntry = It->second;

		CPresenceEntry Entry;
		if(Exists)
			Entry = It->second;
		Entry.m_Identity = {Packet.m_ClientInstanceId, Packet.m_ClientId};
		Entry.m_ServerAddress = Packet.m_ServerAddress;
		Entry.m_PlayerName = Packet.m_PlayerName;
		Entry.m_RemoteAddr = From;
		Entry.m_LastSeen = Now;

		const bool ServerChanged = Exists && OldEntry.m_ServerAddress != Entry.m_ServerAddress;
		const bool NameChanged = Exists && OldEntry.m_PlayerName != Entry.m_PlayerName;
		const bool NewEntry = !Exists;
		m_Entries[IdentityKey] = Entry;

		if(ServerChanged)
			BroadcastPeerPacket(OldEntry, BestClientIndicator::PACKET_PEER_REMOVE, nullptr, IdentityKey);

		if(TreatAsJoin || ServerChanged)
			SendPeerList(Entry, IdentityKey);

		if(TreatAsJoin || ServerChanged || NameChanged)
			BroadcastPeerPacket(Entry, BestClientIndicator::PACKET_PEER_STATE, &Entry.m_RemoteAddr, IdentityKey);

		if(NewEntry)
			log_info(LOG_SCOPE, "join %s on %s from %s", Entry.m_PlayerName.c_str(), Entry.m_ServerAddress.c_str(), NetAddrToString(From).c_str());

		if(NewEntry || ServerChanged || NameChanged)
			m_SnapshotDirty = true;
	}

	void HandleLeave(const BestClientIndicator::CClientPresencePacket &Packet)
	{
		const std::string IdentityKey = MakeIdentityKey(Packet.m_ClientInstanceId, Packet.m_ClientId);
		auto It = m_Entries.find(IdentityKey);
		if(It == m_Entries.end())
			return;

		BroadcastPeerPacket(It->second, BestClientIndicator::PACKET_PEER_REMOVE, nullptr, IdentityKey);
		log_info(LOG_SCOPE, "leave %s on %s", It->second.m_PlayerName.c_str(), It->second.m_ServerAddress.c_str());
		m_Entries.erase(It);
		m_SnapshotDirty = true;
	}

	void BroadcastPeerPacket(const CPresenceEntry &Entry, BestClientIndicator::EPacketType Type, const NETADDR *pExceptAddr, const std::string &SelfKey)
	{
		CPeerPacketBuffer Packet;
		BestClientIndicator::WritePeerStatePacket(Packet.m_vData, Type, Entry.m_ServerAddress.c_str(), Entry.m_PlayerName.c_str(), Entry.m_Identity.m_ClientId);

		for(const auto &[Key, Other] : m_Entries)
		{
			if(Key == SelfKey || Other.m_ServerAddress != Entry.m_ServerAddress)
				continue;
			if(Other.m_Identity.m_InstanceId == Entry.m_Identity.m_InstanceId)
				continue;
			if(pExceptAddr && net_addr_comp(&Other.m_RemoteAddr, pExceptAddr) == 0)
				continue;
			net_udp_send(m_UdpSocket, &Other.m_RemoteAddr, Packet.m_vData.data(), (int)Packet.m_vData.size());
		}
	}

	void SendPeerList(const CPresenceEntry &Recipient, const std::string &RecipientKey)
	{
		std::vector<int> vClientIds;
		for(const auto &[Key, Entry] : m_Entries)
		{
			if(Key == RecipientKey || Entry.m_ServerAddress != Recipient.m_ServerAddress)
				continue;
			if(Entry.m_Identity.m_InstanceId == Recipient.m_Identity.m_InstanceId)
				continue;
			vClientIds.push_back(Entry.m_Identity.m_ClientId);
		}
		std::sort(vClientIds.begin(), vClientIds.end());

		std::vector<uint8_t> vPacket;
		BestClientIndicator::WritePeerListPacket(vPacket, Recipient.m_ServerAddress.c_str(), vClientIds);
		net_udp_send(m_UdpSocket, &Recipient.m_RemoteAddr, vPacket.data(), (int)vPacket.size());
	}

	void RunCleanupTick()
	{
		const int64_t Now = time_timestamp();
		if(m_LastCleanupTick != 0 && Now == m_LastCleanupTick)
			return;
		m_LastCleanupTick = Now;

		bool RemovedAny = false;
		for(auto It = m_Entries.begin(); It != m_Entries.end();)
		{
			if(It->second.m_LastSeen + HEARTBEAT_TIMEOUT_SECONDS < Now)
			{
				log_info(LOG_SCOPE, "timeout %s on %s", It->second.m_PlayerName.c_str(), It->second.m_ServerAddress.c_str());
				BroadcastPeerPacket(It->second, BestClientIndicator::PACKET_PEER_REMOVE, nullptr, It->first);
				It = m_Entries.erase(It);
				RemovedAny = true;
			}
			else
			{
				++It;
			}
		}

		if(RemovedAny)
			m_SnapshotDirty = true;

		CleanupNonces(Now);
		for(auto It = m_LastCompatTimestampLogByAddr.begin(); It != m_LastCompatTimestampLogByAddr.end();)
		{
			if(It->second + BestClientIndicatorCompat::TIMESTAMP_COMPAT_LOG_INTERVAL_SECONDS < Now)
				It = m_LastCompatTimestampLogByAddr.erase(It);
			else
				++It;
		}
		if(m_SnapshotDirty || (!m_Entries.empty() && m_LastSnapshotWrite != Now))
			RefreshSnapshot(true);
	}

	void RefreshSnapshot(bool WriteToDisk)
	{
		const std::string Snapshot = BuildJsonSnapshot(m_Entries);
		if(WriteToDisk)
			WriteFileAtomically(m_Config.m_JsonPath.c_str(), Snapshot);
		m_SnapshotDirty = false;
		m_LastSnapshotWrite = time_timestamp();
	}
};
}

int main(int argc, const char **argv)
{
	CCmdlineFix CmdlineFix(&argc, &argv);
	log_set_global_logger_default();
	net_init();

	signal(SIGINT, SignalHandler);
	signal(SIGTERM, SignalHandler);

	CConfig Config;
	if(!ParseCli(argc, argv, Config))
		return 1;

	CIndicatorServer Server(std::move(Config));
	if(!Server.Init())
		return 1;

	return Server.Run();
}
