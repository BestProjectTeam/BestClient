/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CONFIG_PRESETS_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CONFIG_PRESETS_H

#include <game/client/component.h>

#include <ctime>
#include <vector>

class CBcConfigPreset
{
public:
	char m_aName[64];
	time_t m_Time;
	int m_Changes;
};

class CBcConfigPresets : public CComponent
{
	bool WriteFile(const char *pName);

public:
	static constexpr int MAX_NAME_LENGTH = 64;

	std::vector<CBcConfigPreset> m_vPresets;

	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;

	static bool NameOk(const char *pName);
	void Refresh();
	bool SaveNew(const char *pName);
	bool Load(const char *pName);
	bool Delete(const char *pName);
	bool Override(const char *pName);
	bool Exists(const char *pName) const;
	int CountChanged() const;
	void OpenFolder() const;
};

#endif
