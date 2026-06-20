/* Copyright © 2026 BestClient Team */
#include "lua_system.h"
#include "lua_api.h"

#include <base/fs.h>
#include <base/system.h>
#include <base/time.h>
#include <engine/client.h>
#include <engine/external/json-parser/json.h>
#include <engine/graphics.h>
#include <engine/shared/jsonwriter.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <generated/protocol.h>
#include <sol/sol.hpp>

#include <string>
#include <unordered_map>

struct SScriptData
{
	sol::state m_Lua;
	SLuaUiState m_UiState;
	std::unordered_map<std::string, std::string> m_Storage;
	std::string m_StoragePath;
};

CScriptSystem::CScript::CScript() :
	m_pData(std::make_unique<SScriptData>()) {}

CScriptSystem::CScript::~CScript() = default;

CScriptSystem::CScript::CScript(CScript &&) noexcept = default;
CScriptSystem::CScript &CScriptSystem::CScript::operator=(CScript &&) noexcept = default;

void CScriptSystem::OnInit()
{
	char *pContent = Storage()->ReadFileStr("lua_scripts.json", IStorage::TYPE_ALL);
	if(!pContent)
		return;
	json_settings JsonSettings = {};
	char aError[256];
	json_value *pRoot = json_parse_ex(&JsonSettings, pContent, strlen(pContent), aError);
	free(pContent);
	if(!pRoot)
		return;
	if(pRoot->type == json_array)
	{
		for(unsigned i = 0; i < pRoot->u.array.length; i++)
		{
			const json_value *pEntry = pRoot->u.array.values[i];
			if(pEntry->type != json_object)
				continue;
			const char *pPath = nullptr;
			bool Enabled = true;
			for(unsigned j = 0; j < pEntry->u.object.length; j++)
			{
				const char *pKey = pEntry->u.object.values[j].name;
				const json_value *pVal = pEntry->u.object.values[j].value;
				if(strcmp(pKey, "path") == 0 && pVal->type == json_string)
					pPath = pVal->u.string.ptr;
				else if(strcmp(pKey, "enabled") == 0 && pVal->type == json_boolean)
					Enabled = pVal->u.boolean != 0;
			}
			if(pPath)
			{
				LoadScript(pPath, false);
				if(!m_Scripts.empty())
					m_Scripts.back().m_Enabled = Enabled;
			}
		}
	}
	json_value_free(pRoot);
}

void CScriptSystem::OnConsoleInit() {}

void CScriptSystem::OnNewSnapshot()
{
	DispatchTick();
}

void CScriptSystem::OnRender()
{
	DispatchRender();

	static int64_t s_LastCheck = 0;
	const int64_t Now = time_get();
	if(Now - s_LastCheck < time_freq() * 2)
		return;
	s_LastCheck = Now;

	for(int i = 0; i < (int)m_Scripts.size(); i++)
	{
		if(m_Scripts[i].m_LastModified == 0)
			continue;
		time_t Created, Modified;
		if(fs_file_time(m_Scripts[i].m_Path.c_str(), &Created, &Modified) == 0 && Modified != m_Scripts[i].m_LastModified)
		{
			ReloadScript(i);
			break;
		}
	}
}

void CScriptSystem::LoadScript(const char *pPath, bool bSave)
{
	CScript Script;
	Script.m_Path = pPath;

	auto &Lua = Script.m_pData->m_Lua;
	Lua.open_libraries(
		sol::lib::base,
		sol::lib::string,
		sol::lib::math,
		sol::lib::table);

	Lua.create_named_table("script");

	// Build storage path (.json next to the .lua file)
	{
		std::string StoragePath = pPath;
		auto DotPos = StoragePath.rfind('.');
		Script.m_pData->m_StoragePath = (DotPos != std::string::npos) ? StoragePath.substr(0, DotPos) + ".json" : StoragePath + ".json";

		// Load existing storage from file
		IOHANDLE F = io_open(Script.m_pData->m_StoragePath.c_str(), IOFLAG_READ);
		if(F)
		{
			char *pContent = io_read_all_str(F);
			io_close(F);
			if(pContent)
			{
				json_settings JsonSettings = {};
				char aError[256];
				json_value *pRoot = json_parse_ex(&JsonSettings, pContent, strlen(pContent), aError);
				free(pContent);
				if(pRoot && pRoot->type == json_object)
				{
					for(unsigned j = 0; j < pRoot->u.object.length; j++)
					{
						const char *pKey = pRoot->u.object.values[j].name;
						const json_value *pVal = pRoot->u.object.values[j].value;
						if(pVal->type == json_string)
							Script.m_pData->m_Storage[pKey] = pVal->u.string.ptr;
					}
				}
				if(pRoot)
					json_value_free(pRoot);
			}
		}
	}

	SLuaApiCtx Ctx{GameClient(), Console(), Client(), Graphics(), TextRender(), Ui(), &Script.m_pData->m_UiState, &Script.m_pData->m_Storage, &Script.m_pData->m_StoragePath};
	RegisterLuaApi(Lua, Ctx);

	auto Result = Lua.safe_script_file(pPath, sol::script_pass_on_error);
	if(!Result.valid())
	{
		sol::error Err = Result;
		Script.m_LastError = Err.what();
		Script.m_Enabled = false;
	}
	else
	{
		sol::table Meta = Lua["script"];
		if(Meta.valid())
		{
			Script.m_Name = Meta.get_or<std::string>("name", "");
			Script.m_Description = Meta.get_or<std::string>("description", "");
			Script.m_Version = Meta.get_or<std::string>("version", "");
			Script.m_Author = Meta.get_or<std::string>("author", "");
		}
		if(Script.m_Name.empty())
		{
			const char *pSlash = strrchr(pPath, '/');
			const char *pBackslash = strrchr(pPath, '\\');
			const char *pName = (pSlash > pBackslash) ? pSlash + 1 : (pBackslash ? pBackslash + 1 : pPath);
			Script.m_Name = pName;
		}
		Script.m_Enabled = true;
	}

	{
		time_t Created, Modified;
		if(fs_file_time(pPath, &Created, &Modified) == 0)
			Script.m_LastModified = Modified;
	}

	m_Scripts.push_back(std::move(Script));
	if(bSave)
		SaveScriptList();
}

void CScriptSystem::UnloadScript(int Index, bool bSave)
{
	if(Index < 0 || Index >= (int)m_Scripts.size())
		return;
	m_Scripts.erase(m_Scripts.begin() + Index);
	if(bSave)
		SaveScriptList();
}

void CScriptSystem::ReloadScript(int Index)
{
	if(Index < 0 || Index >= (int)m_Scripts.size())
		return;
	bool WasEnabled = m_Scripts[Index].m_Enabled;
	std::string Path = m_Scripts[Index].m_Path;
	UnloadScript(Index, false);
	LoadScript(Path.c_str(), false);
	if(!m_Scripts.empty())
		m_Scripts.back().m_Enabled = WasEnabled;
	SaveScriptList();
}

void CScriptSystem::SaveScriptList()
{
	IOHANDLE F = Storage()->OpenFile("lua_scripts.json", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!F)
		return;
	CJsonFileWriter Writer(F);
	Writer.BeginArray();
	for(const auto &Script : m_Scripts)
	{
		Writer.BeginObject();
		Writer.WriteAttribute("path");
		Writer.WriteStrValue(Script.m_Path.c_str());
		Writer.WriteAttribute("enabled");
		Writer.WriteBoolValue(Script.m_Enabled);
		Writer.EndObject();
	}
	Writer.EndArray();
}

void CScriptSystem::OnMessage(int MsgType, void *pRawMsg)
{
	if(MsgType != NETMSGTYPE_SV_CHAT)
		return;
	auto *pMsg = static_cast<CNetMsg_Sv_Chat *>(pRawMsg);
	const char *pAuthorName = "";
	if(pMsg->m_ClientId >= 0 && pMsg->m_ClientId < MAX_CLIENTS)
		pAuthorName = GameClient()->m_aClients[pMsg->m_ClientId].m_aName;
	DispatchMessage(pMsg->m_pMessage, pMsg->m_ClientId, pAuthorName);
}

void CScriptSystem::DispatchTick()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;
	for(auto &Script : m_Scripts)
	{
		if(!Script.m_Enabled)
			continue;
		sol::protected_function Fn = Script.m_pData->m_Lua["script"]["on_tick"];
		if(!Fn.valid())
			continue;
		auto Result = Fn();
		if(!Result.valid())
		{
			sol::error Err = Result;
			Script.m_LastError = Err.what();
		}
	}
}

void CScriptSystem::DispatchRender()
{
	Ui()->MapScreen();
	Graphics()->BlendNormal();

	for(auto &Script : m_Scripts)
	{
		if(!Script.m_Enabled)
			continue;
		sol::protected_function Fn = Script.m_pData->m_Lua["script"]["on_render"];
		if(!Fn.valid())
			continue;
		auto Result = Fn();
		if(!Result.valid())
		{
			sol::error Err = Result;
			Script.m_LastError = Err.what();
		}
	}
}

void CScriptSystem::DispatchMessage(const char *pMsg, int AuthorId, const char *pAuthorName)
{
	for(auto &Script : m_Scripts)
	{
		if(!Script.m_Enabled)
			continue;
		sol::protected_function Fn = Script.m_pData->m_Lua["script"]["on_message"];
		if(!Fn.valid())
			continue;
		auto Result = Fn(std::string(pMsg), AuthorId, std::string(pAuthorName));
		if(!Result.valid())
		{
			sol::error Err = Result;
			Script.m_LastError = Err.what();
		}
	}
}

void CScriptSystem::OnStateChange(int NewState, int OldState)
{
	if(NewState == IClient::STATE_ONLINE)
		DispatchConnect();
	else if(OldState == IClient::STATE_ONLINE)
		DispatchDisconnect();
}

void CScriptSystem::DispatchConnect()
{
	for(auto &Script : m_Scripts)
	{
		if(!Script.m_Enabled)
			continue;
		sol::protected_function Fn = Script.m_pData->m_Lua["script"]["on_connect"];
		if(!Fn.valid())
			continue;
		auto Result = Fn();
		if(!Result.valid())
		{
			sol::error Err = Result;
			Script.m_LastError = Err.what();
		}
	}
}

void CScriptSystem::DispatchDisconnect()
{
	for(auto &Script : m_Scripts)
	{
		if(!Script.m_Enabled)
			continue;
		sol::protected_function Fn = Script.m_pData->m_Lua["script"]["on_disconnect"];
		if(!Fn.valid())
			continue;
		auto Result = Fn();
		if(!Result.valid())
		{
			sol::error Err = Result;
			Script.m_LastError = Err.what();
		}
	}
}

float CScriptSystem::DispatchSettings(int Index, float X, float Y, float W)
{
	if(Index < 0 || Index >= (int)m_Scripts.size())
		return 0.0f;
	auto &Script = m_Scripts[Index];
	if(!Script.m_Enabled)
		return 0.0f;

	auto &State = Script.m_pData->m_UiState;
	State.m_X = X;
	State.m_Y = Y;
	State.m_W = W;
	State.m_CurY = Y;
	State.m_WidgetIdx = 0;
	State.m_Active = true;

	sol::protected_function Fn = Script.m_pData->m_Lua["script"]["on_settings"];
	if(Fn.valid())
	{
		auto Result = Fn();
		if(!Result.valid())
		{
			sol::error Err = Result;
			Script.m_LastError = Err.what();
		}
	}

	State.m_Active = false;
	return State.m_CurY - Y;
}
