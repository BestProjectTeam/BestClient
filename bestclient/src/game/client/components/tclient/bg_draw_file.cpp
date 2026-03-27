#include "bg_draw_file.h"

#include <cstring>

#define MAX_LINE_LENGTH 256
#define MAX_PARTS_PER_ITEM 4096

bool BgDrawFile::Write(const std::function<bool(const char *)> &WriteLine, const CBgDrawItemData &Data)
{
	char aBuf[MAX_LINE_LENGTH];
	for(const CBgDrawItemDataPoint &Point : Data)
	{
		std::snprintf(aBuf, sizeof(aBuf), "%f,%f,%f,%f,%f,%f,%f",
			Point.x, Point.y, Point.w, Point.r, Point.g, Point.b, Point.a);
		if(!WriteLine(aBuf))
			return false;
	}
	if(!WriteLine(","))
		return false;
	return true;
}
bool BgDrawFile::Read(const std::function<bool(char *pBuf, int Length)> &ReadLine, CBgDrawItemData &Data)
{
	char aBuf[MAX_LINE_LENGTH];
	Data.clear();
	for(int Count = 0; Count < MAX_PARTS_PER_ITEM; ++Count)
	{
		if(!ReadLine(aBuf, sizeof(aBuf)))
			return false;
		if(aBuf[0] == '\0' || aBuf[0] == EOF || aBuf[0] == ',')
			return !Data.empty();
		if(aBuf[0] == '#')
			continue;
		float x, y;
		float w = 5.0f;
		float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
		int Ret = std::sscanf(aBuf, "%f,%f,%f,%f,%f,%f,%f", &x, &y, &w, &r, &g, &b, &a);
		if(Ret < 2)
			continue; // Need x and y
		Data.emplace_back(x, y, w, r, g, b, a);
	}
	return true;
}
bool BgDrawFile::Write(FILE *pFile, const CBgDrawItemData &Data)
{
	auto WriteLine = [pFile](const char *pLine) -> bool {
		if(fputs(pLine, pFile) == EOF)
			return false;
		if(fputc('\n', pFile) == EOF)
			return false;
		return true;
	};
	return Write(WriteLine, Data);
}
bool BgDrawFile::Read(FILE *pFile, CBgDrawItemData &Data)
{
	auto ReadLine = [pFile](char *pBuf, int Length) -> bool {
		if(!std::fgets(pBuf, Length, pFile))
			return false;
		size_t Len = std::strlen(pBuf);
		while(Len > 0 && (pBuf[Len - 1] == '\n' || pBuf[Len - 1] == '\r'))
			pBuf[--Len] = '\0';
		return true;
	};
	return Read(ReadLine, Data);
}
