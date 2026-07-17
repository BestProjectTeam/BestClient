/* Copyright © 2026 BestProject Team */
#include "cherry_gifs_cache.h"

#include <base/system.h>

#include <engine/storage.h>

namespace
{
constexpr const char *CACHE_DIR = "BestClient/cherrygifs_cache";
constexpr int64_t MAX_CACHE_FILE_BYTES = 8 * 1024 * 1024; // matches the network download cap

bool BuildCachePath(const char *pGifId, char *pOut, size_t OutSize)
{
	// Gif ids are short alnum tokens straight from the API's own "id" field; still refuse
	// anything that could escape the cache directory, just in case.
	if(pGifId[0] == '\0' || pGifId[0] == '.')
		return false;
	for(const char *p = pGifId; *p; ++p)
	{
		if(*p == '/' || *p == '\\' || *p == ':')
			return false;
	}
	str_format(pOut, OutSize, "%s/%s.gif", CACHE_DIR, pGifId);
	return true;
}
}

bool CherryGifsCache::Load(IStorage *pStorage, const char *pGifId, std::vector<unsigned char> &vOutData)
{
	char aPath[256];
	if(!BuildCachePath(pGifId, aPath, sizeof(aPath)))
		return false;

	void *pRawData = nullptr;
	unsigned RawLen = 0;
	if(!pStorage->ReadFile(aPath, IStorage::TYPE_SAVE, &pRawData, &RawLen))
		return false;

	if(RawLen == 0 || RawLen > MAX_CACHE_FILE_BYTES)
	{
		free(pRawData);
		return false;
	}

	vOutData.assign(static_cast<unsigned char *>(pRawData), static_cast<unsigned char *>(pRawData) + RawLen);
	free(pRawData);
	return true;
}

void CherryGifsCache::Save(IStorage *pStorage, const char *pGifId, const unsigned char *pData, size_t DataSize)
{
	if(!pData || DataSize == 0 || DataSize > (size_t)MAX_CACHE_FILE_BYTES)
		return;

	char aPath[256];
	if(!BuildCachePath(pGifId, aPath, sizeof(aPath)))
		return;

	pStorage->CreateFolder("BestClient", IStorage::TYPE_SAVE);
	pStorage->CreateFolder(CACHE_DIR, IStorage::TYPE_SAVE);

	IOHANDLE File = pStorage->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;
	io_write(File, pData, DataSize);
	io_close(File);
}
