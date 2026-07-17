// Copyright © 2026 BestProject Team
// Recursive filesystem helpers for the Linux updater (POSIX opendir/stat, no shelling out).
#ifndef TOOLS_BESTCLIENT_UPDATER_LINUX_FS_H
#define TOOLS_BESTCLIENT_UPDATER_LINUX_FS_H

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

namespace BcFs
{

inline bool PathExists(const char *pPath)
{
	struct stat St;
	return stat(pPath, &St) == 0;
}

inline bool IsDirectory(const char *pPath)
{
	struct stat St;
	if(stat(pPath, &St) != 0)
		return false;
	return S_ISDIR(St.st_mode);
}

inline bool MakeDirRecursive(const std::string &Path)
{
	if(Path.empty() || IsDirectory(Path.c_str()))
		return true;

	const size_t Slash = Path.find_last_of('/');
	if(Slash != std::string::npos && Slash > 0)
	{
		if(!MakeDirRecursive(Path.substr(0, Slash)))
			return false;
	}

	if(mkdir(Path.c_str(), 0755) == 0)
		return true;
	return IsDirectory(Path.c_str());
}

inline void DeleteTree(const char *pPath)
{
	struct stat St;
	if(stat(pPath, &St) != 0)
		return;

	if(!S_ISDIR(St.st_mode))
	{
		unlink(pPath);
		return;
	}

	DIR *pDir = opendir(pPath);
	if(!pDir)
		return;

	struct dirent *pEntry;
	while((pEntry = readdir(pDir)) != nullptr)
	{
		if(strcmp(pEntry->d_name, ".") == 0 || strcmp(pEntry->d_name, "..") == 0)
			continue;
		const std::string Child = std::string(pPath) + "/" + pEntry->d_name;
		DeleteTree(Child.c_str());
	}
	closedir(pDir);
	rmdir(pPath);
}

// Recursively counts regular files under pDir. Clamped to at least 1 so
// callers can safely use the result as a progress-percentage denominator.
inline int CountFiles(const char *pDir)
{
	DIR *pDirHandle = opendir(pDir);
	if(!pDirHandle)
		return 1;

	int Count = 0;
	struct dirent *pEntry;
	while((pEntry = readdir(pDirHandle)) != nullptr)
	{
		if(strcmp(pEntry->d_name, ".") == 0 || strcmp(pEntry->d_name, "..") == 0)
			continue;
		const std::string Child = std::string(pDir) + "/" + pEntry->d_name;
		struct stat St;
		if(stat(Child.c_str(), &St) != 0)
			continue;
		if(S_ISDIR(St.st_mode))
			Count += CountFiles(Child.c_str());
		else
			++Count;
	}
	closedir(pDirHandle);
	return Count > 0 ? Count : 1;
}

// Recursively copies pSrc into pDst (pDst is created if missing), preserving
// the source's permission bits (needed so the client binary keeps its
// executable bit after being copied into the install directory).
inline void CopyTree(const char *pSrc, const char *pDst, const std::function<void()> &PerFile = nullptr)
{
	struct stat SrcSt;
	if(stat(pSrc, &SrcSt) != 0)
		return;

	if(!S_ISDIR(SrcSt.st_mode))
	{
		FILE *pIn = fopen(pSrc, "rb");
		if(!pIn)
			return;
		FILE *pOut = fopen(pDst, "wb");
		if(!pOut)
		{
			fclose(pIn);
			return;
		}
		char aBuf[65536];
		size_t Read;
		while((Read = fread(aBuf, 1, sizeof(aBuf), pIn)) > 0)
			fwrite(aBuf, 1, Read, pOut);
		fclose(pIn);
		fclose(pOut);
		chmod(pDst, SrcSt.st_mode & 0777);
		if(PerFile)
			PerFile();
		return;
	}

	MakeDirRecursive(pDst);
	DIR *pDir = opendir(pSrc);
	if(!pDir)
		return;

	struct dirent *pEntry;
	while((pEntry = readdir(pDir)) != nullptr)
	{
		if(strcmp(pEntry->d_name, ".") == 0 || strcmp(pEntry->d_name, "..") == 0)
			continue;
		const std::string SrcChild = std::string(pSrc) + "/" + pEntry->d_name;
		const std::string DstChild = std::string(pDst) + "/" + pEntry->d_name;
		CopyTree(SrcChild.c_str(), DstChild.c_str(), PerFile);
	}
	closedir(pDir);
}

} // namespace BcFs

#endif
