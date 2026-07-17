#ifndef ENGINE_UPDATER_H
#define ENGINE_UPDATER_H

#include "kernel.h"

class IUpdater : public IInterface
{
	MACRO_INTERFACE("updater")
public:
	enum EUpdaterState
	{
		CLEAN,
		GETTING_MANIFEST,
		VERSION_AVAILABLE,
		DOWNLOADING,
		NEED_RESTART,
		FAIL,
	};

	virtual void Update() = 0;
	virtual void CheckForUpdate() = 0;
	virtual void InitiateUpdate() = 0;
	virtual void ApplyUpdateAndRestart() = 0;

	virtual EUpdaterState GetCurrentState() = 0;
	virtual void GetCurrentFile(char *pBuf, int BufSize) = 0;
	virtual int GetCurrentPercent() = 0;
	virtual const char *GetLatestVersionString() = 0;
};

#endif
