#include <engine/discord.h>

class CDiscordStub : public IDiscord
{
	void Update(bool Enabled) override {}
	void ClearGameInfo() override {}
	// bestclient
	void SetGameInfo(const CServerInfo &ServerInfo, const char *pMapName, const char *pPlayerName, const char *pSkinName, bool ShowMap, bool Registered) override {}
	void UpdateServerInfo(const CServerInfo &ServerInfo, const char *pMapName, const char *pPlayerName, const char *pSkinName) override {}
	// bestclient
	void UpdatePlayerCount(int Count) override {}
};

IDiscord *CreateDiscord()
{
	// bestclient
	IDiscord *pDiscord = CreateBestClientDiscord();
	// bestclient
	if(pDiscord)
	{
		return pDiscord;
	}
	return new CDiscordStub();
}
