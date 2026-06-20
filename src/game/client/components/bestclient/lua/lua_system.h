/* Copyright © 2026 BestClient Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_LUA_LUA_SYSTEM_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_LUA_LUA_SYSTEM_H

#include "lua_ui_state.h"

#include <game/client/component.h>

#include <ctime>
#include <memory>
#include <string>
#include <vector>

// Opaque — full definition only in lua_system.cpp (keeps sol.hpp out of gameclient.h chain)
struct SScriptData;

class CScriptSystem : public CComponent
{
public:
	struct CScript
	{
		std::string m_Path;
		std::string m_Name;
		std::string m_Description;
		std::string m_Version;
		std::string m_Author;
		bool m_Enabled = false;
		bool m_SettingsOpen = false;
		float m_LastSettingsH = 0.0f;
		std::string m_LastError;
		time_t m_LastModified = 0;
		std::unique_ptr<SScriptData> m_pData;

		CScript();
		~CScript();
		CScript(CScript &&) noexcept;
		CScript &operator=(CScript &&) noexcept;
		CScript(const CScript &) = delete;
		CScript &operator=(const CScript &) = delete;
	};

	std::vector<CScript> m_Scripts;

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnRender() override;
	void OnNewSnapshot() override;
	void OnConsoleInit() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnMessage(int MsgType, void *pRawMsg) override;

	void LoadScript(const char *pPath, bool bSave = true);
	void UnloadScript(int Index, bool bSave = true);
	void ReloadScript(int Index);
	void SaveScriptList();

	void DispatchTick();
	void DispatchRender();
	void DispatchMessage(const char *pMsg, int AuthorId, const char *pAuthorName);
	void DispatchConnect();
	void DispatchDisconnect();
	// Returns height consumed by widgets drawn during on_settings
	float DispatchSettings(int Index, float X, float Y, float W);
};

#endif
