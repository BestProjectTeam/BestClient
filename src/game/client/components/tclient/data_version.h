#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_DATA_VERSION_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_DATA_VERSION_H
#else
#error data_version.h included multiple times
#endif

// Check validity of data/data_version.txt
// This is extracted to this file for ease of editing
// TODO: this is a stub

#include <base/system.h>

#define DATA_VERSION_PATH "data_version.txt"

inline void CheckDataVersion(char *pError, int Length, IOHANDLE File)
{
	(void)pError;
	(void)Length;
	if(File)
		io_close(File);
}
