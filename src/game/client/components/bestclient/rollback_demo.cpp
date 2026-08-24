/* Copyright © 2026 BestProject Team */
#include "rollback_demo.h"

#include <base/system.h>
#include <base/types.h>

#include <engine/client.h>
#include <engine/demo.h>
#include <engine/map.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/broadcast.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>

void CRollbackDemo::ConSaveRollback(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CRollbackDemo *>(pUserData)->SaveRollback();
}

void CRollbackDemo::SaveRollback()
{
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		GameClient()->m_Broadcast.DoBroadcast(Localize("Rollback is only available while online"));
		return;
	}

	if(!g_Config.m_ClReplays)
	{
		GameClient()->m_Broadcast.DoBroadcast(Localize("Enable rollback demo recording first"));
		return;
	}

	IDemoRecorder *pReplayRecorder = DemoRecorder(RECORDER_REPLAYS);
	if(!pReplayRecorder->IsRecording())
	{
		GameClient()->m_Broadcast.DoBroadcast(Localize("Rollback recorder is not ready yet"));
		return;
	}

	if(pReplayRecorder->Length() < 1)
	{
		GameClient()->m_Broadcast.DoBroadcast(Localize("Wait at least 1 second before rollback"));
		return;
	}

	Storage()->CreateFolder("demos/rollback", IStorage::TYPE_SAVE);

	const int Length = std::clamp(g_Config.m_ClReplayLength, 10, 60);
	char aTimestamp[20];
	str_timestamp(aTimestamp, sizeof(aTimestamp));

	char aFilename[IO_MAX_PATH_LENGTH];
	str_format(aFilename, sizeof(aFilename), "rollback/%s_%s_(rollback)", GameClient()->Map()->BaseName(), aTimestamp);

	char aCommand[IO_MAX_PATH_LENGTH + 64];
	str_format(aCommand, sizeof(aCommand), "save_replay %d \"%s\"", Length, aFilename);
	Console()->ExecuteLine(aCommand, IConsole::CLIENT_ID_UNSPECIFIED);
}

void CRollbackDemo::OnConsoleInit()
{
	Console()->Register("BC_save_rollback", "", CFGFLAG_CLIENT, ConSaveRollback, this, "Save the last configured seconds as a rollback demo");
}
