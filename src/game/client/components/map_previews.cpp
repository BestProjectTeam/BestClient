#include "map_previews.h"

#include <base/log.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/engine.h>
#include <engine/gfx/image_loader.h>
#include <engine/map.h>
#include <engine/shared/config.h>
#include <engine/shared/map.h>
#include <engine/storage.h>

#include <game/mapitems.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace
{
enum EPreviewCategory : unsigned char
{
	PREVIEW_EMPTY = 0,
	PREVIEW_OTHER,
	PREVIEW_SOLID,
	PREVIEW_NOHOOK,
	PREVIEW_STOP,
	PREVIEW_ENTITY,
	PREVIEW_TUNE,
	PREVIEW_SPEEDUP,
	PREVIEW_TELE,
	PREVIEW_UNFREEZE,
	PREVIEW_FREEZE,
	PREVIEW_DEATH,
	PREVIEW_START,
	PREVIEW_FINISH,
};

struct SPreviewColor
{
	unsigned char r;
	unsigned char g;
	unsigned char b;
	unsigned char a;
};

static EPreviewCategory TileCategory(int Index)
{
	if(Index == TILE_AIR)
		return PREVIEW_EMPTY;
	if(Index == TILE_SOLID)
		return PREVIEW_SOLID;
	if(Index == TILE_NOHOOK)
		return PREVIEW_NOHOOK;
	if(Index == TILE_DEATH)
		return PREVIEW_DEATH;
	if(Index == TILE_FREEZE || Index == TILE_DFREEZE || Index == TILE_LFREEZE)
		return PREVIEW_FREEZE;
	if(Index == TILE_UNFREEZE || Index == TILE_DUNFREEZE || Index == TILE_LUNFREEZE)
		return PREVIEW_UNFREEZE;
	if(Index == TILE_START)
		return PREVIEW_START;
	if(Index == TILE_FINISH)
		return PREVIEW_FINISH;
	if(Index == TILE_TELEINEVIL || Index == TILE_TELEINWEAPON || Index == TILE_TELEINHOOK || Index == TILE_TELEIN || Index == TILE_TELEOUT || Index == TILE_TELECHECK || Index == TILE_TELECHECKOUT || Index == TILE_TELECHECKIN || Index == TILE_TELECHECKINEVIL)
		return PREVIEW_TELE;
	if(Index == TILE_SPEED_BOOST_OLD || Index == TILE_SPEED_BOOST)
		return PREVIEW_SPEEDUP;
	if(Index == TILE_STOP || Index == TILE_STOPS || Index == TILE_STOPA)
		return PREVIEW_STOP;
	if(Index == TILE_TUNE)
		return PREVIEW_TUNE;
	if(Index >= ENTITY_OFFSET)
		return PREVIEW_ENTITY;
	return PREVIEW_OTHER;
}

static SPreviewColor CategoryColor(EPreviewCategory Category)
{
	switch(Category)
	{
	case PREVIEW_SOLID: return {132, 142, 153, 255};
	case PREVIEW_NOHOOK: return {189, 102, 75, 255};
	case PREVIEW_STOP: return {210, 116, 170, 255};
	case PREVIEW_ENTITY: return {224, 224, 224, 255};
	case PREVIEW_TUNE: return {92, 139, 208, 255};
	case PREVIEW_SPEEDUP: return {230, 164, 67, 255};
	case PREVIEW_TELE: return {160, 111, 199, 255};
	case PREVIEW_UNFREEZE: return {126, 211, 196, 255};
	case PREVIEW_FREEZE: return {91, 174, 218, 255};
	case PREVIEW_DEATH: return {211, 70, 70, 255};
	case PREVIEW_START: return {92, 193, 111, 255};
	case PREVIEW_FINISH: return {238, 196, 82, 255};
	case PREVIEW_OTHER: return {96, 105, 116, 255};
	default: return {25, 27, 32, 255};
	}
}

static void SetCategory(std::vector<unsigned char> &vCategories, int Width, int Height, int x, int y, EPreviewCategory Category)
{
	if(Category == PREVIEW_EMPTY || x < 0 || y < 0 || x >= Width || y >= Height)
		return;
	const size_t Index = (size_t)y * Width + x;
	if(Category > vCategories[Index])
		vCategories[Index] = Category;
}

static void FillRect(CImageInfo &Image, int x0, int y0, int x1, int y1, SPreviewColor Color)
{
	x0 = std::clamp(x0, 0, (int)Image.m_Width);
	y0 = std::clamp(y0, 0, (int)Image.m_Height);
	x1 = std::clamp(x1, 0, (int)Image.m_Width);
	y1 = std::clamp(y1, 0, (int)Image.m_Height);
	for(int y = y0; y < y1; ++y)
	{
		for(int x = x0; x < x1; ++x)
		{
			unsigned char *pPixel = Image.m_pData + ((size_t)y * Image.m_Width + x) * 4;
			pPixel[0] = Color.r;
			pPixel[1] = Color.g;
			pPixel[2] = Color.b;
			pPixel[3] = Color.a;
		}
	}
}
}

namespace
{
struct SPreviewRgba
{
	float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
	float a = 0.0f;
};

static SPreviewRgba SampleImage(const CImageInfo &Image, float u, float v)
{
	if(Image.m_pData == nullptr || Image.m_Width == 0 || Image.m_Height == 0)
		return {1.0f, 1.0f, 1.0f, 1.0f};
	u = std::clamp(u, 0.0f, 0.999999f);
	v = std::clamp(v, 0.0f, 0.999999f);
	const size_t x = minimum((size_t)(u * Image.m_Width), Image.m_Width - 1);
	const size_t y = minimum((size_t)(v * Image.m_Height), Image.m_Height - 1);
	const uint8_t *p = Image.m_pData + (y * Image.m_Width + x) * Image.PixelSize();
	switch(Image.m_Format)
	{
	case CImageInfo::FORMAT_RGBA: return {p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, p[3] / 255.0f};
	case CImageInfo::FORMAT_RGB: return {p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, 1.0f};
	case CImageInfo::FORMAT_RA: return {p[0] / 255.0f, p[0] / 255.0f, p[0] / 255.0f, p[1] / 255.0f};
	case CImageInfo::FORMAT_R: return {p[0] / 255.0f, p[0] / 255.0f, p[0] / 255.0f, 1.0f};
	default: return {1.0f, 1.0f, 1.0f, 1.0f};
	}
}

static void BlendPreviewPixel(CImageInfo &Dst, int x, int y, SPreviewRgba Src)
{
	if(x < 0 || y < 0 || x >= (int)Dst.m_Width || y >= (int)Dst.m_Height || Src.a <= 0.0f)
		return;
	Src.r = std::clamp(Src.r, 0.0f, 1.0f);
	Src.g = std::clamp(Src.g, 0.0f, 1.0f);
	Src.b = std::clamp(Src.b, 0.0f, 1.0f);
	Src.a = std::clamp(Src.a, 0.0f, 1.0f);
	uint8_t *p = Dst.m_pData + ((size_t)y * Dst.m_Width + x) * 4;
	const float Inv = 1.0f - Src.a;
	p[0] = (uint8_t)std::clamp<int>((int)std::round((Src.r * Src.a + (p[0] / 255.0f) * Inv) * 255.0f), 0, 255);
	p[1] = (uint8_t)std::clamp<int>((int)std::round((Src.g * Src.a + (p[1] / 255.0f) * Inv) * 255.0f), 0, 255);
	p[2] = (uint8_t)std::clamp<int>((int)std::round((Src.b * Src.a + (p[2] / 255.0f) * Inv) * 255.0f), 0, 255);
	p[3] = 255;
}

static bool LoadPreviewMapImage(IStorage *pStorage, IMap *pMap, int ImageStart, int ImageIndex, CImageInfo &Out)
{
	if(ImageIndex < 0)
		return false;
	const CMapItemImage_v2 *pImg = static_cast<const CMapItemImage_v2 *>(pMap->GetItem(ImageStart + ImageIndex));
	if(pImg == nullptr || pImg->m_Width <= 0 || pImg->m_Height <= 0)
		return false;
	const char *pName = pMap->GetDataString(pImg->m_ImageName);
	bool Success = false;
	if(pImg->m_External)
	{
		if(pName != nullptr && pName[0] != '\0')
		{
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "mapres/%s.png", pName);
			IOHANDLE File = pStorage->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_ALL);
			int PngliteIncompatible = 0;
			Success = CImageLoader::LoadPng(File, aPath, Out, PngliteIncompatible);
		}
	}
	else
	{
		void *pData = pMap->GetData(pImg->m_ImageData);
		const size_t Need = (size_t)pImg->m_Width * pImg->m_Height * 4;
		const int DataSize = pMap->GetDataSize(pImg->m_ImageData);
		if(pData != nullptr && DataSize >= 0 && (size_t)DataSize >= Need)
		{
			Out.m_Width = pImg->m_Width;
			Out.m_Height = pImg->m_Height;
			Out.m_Format = CImageInfo::FORMAT_RGBA;
			Out.m_pData = static_cast<uint8_t *>(malloc(Need));
			if(Out.m_pData != nullptr)
			{
				mem_copy(Out.m_pData, pData, Need);
				Success = true;
			}
		}
		pMap->UnloadData(pImg->m_ImageData);
	}
	if(pImg->m_ImageName >= 0)
		pMap->UnloadData(pImg->m_ImageName);
	return Success;
}

static void TileUvCorners(unsigned char Flags, std::array<float, 4> &X, std::array<float, 4> &Y)
{
	X = {0.0f, 1.0f, 1.0f, 0.0f};
	Y = {0.0f, 0.0f, 1.0f, 1.0f};
	if(Flags & TILEFLAG_XFLIP)
		std::rotate(X.begin(), X.begin() + 2, X.end());
	if(Flags & TILEFLAG_YFLIP)
		std::rotate(Y.begin(), Y.begin() + 2, Y.end());
	if(Flags & TILEFLAG_ROTATE)
	{
		std::rotate(X.begin(), X.begin() + 3, X.end());
		std::rotate(Y.begin(), Y.begin() + 3, Y.end());
	}
}

static void DrawPreviewTile(CImageInfo &Dst, const CImageInfo &Texture, unsigned char TileIndex, unsigned char Flags,
	float x0, float y0, float x1, float y1, const CColor &LayerColor)
{
	if(TileIndex == 0 || Texture.m_pData == nullptr || Texture.m_Width < 16 || Texture.m_Height < 16)
		return;
	const int ix0 = std::max(0, (int)std::floor(x0));
	const int iy0 = std::max(0, (int)std::floor(y0));
	const int ix1 = std::min((int)Dst.m_Width, maximum(ix0 + 1, (int)std::ceil(x1)));
	const int iy1 = std::min((int)Dst.m_Height, maximum(iy0 + 1, (int)std::ceil(y1)));
	if(ix0 >= ix1 || iy0 >= iy1 || x1 <= x0 || y1 <= y0)
		return;
	std::array<float, 4> Uc, Vc;
	TileUvCorners(Flags, Uc, Vc);
	const float BaseU = (TileIndex % 16) / 16.0f;
	const float BaseV = (TileIndex / 16) / 16.0f;
	for(float &u : Uc) u = BaseU + u / 16.0f;
	for(float &v : Vc) v = BaseV + v / 16.0f;
	const float Cr = LayerColor.r / 255.0f;
	const float Cg = LayerColor.g / 255.0f;
	const float Cb = LayerColor.b / 255.0f;
	const float Ca = LayerColor.a / 255.0f;
	for(int py = iy0; py < iy1; ++py)
	{
		const float t = std::clamp(((py + 0.5f) - y0) / (y1 - y0), 0.0f, 1.0f);
		for(int px = ix0; px < ix1; ++px)
		{
			const float q = std::clamp(((px + 0.5f) - x0) / (x1 - x0), 0.0f, 1.0f);
			const float u = (1-q)*(1-t)*Uc[0] + q*(1-t)*Uc[1] + q*t*Uc[2] + (1-q)*t*Uc[3];
			const float v = (1-q)*(1-t)*Vc[0] + q*(1-t)*Vc[1] + q*t*Vc[2] + (1-q)*t*Vc[3];
			SPreviewRgba Color = SampleImage(Texture, u, v);
			Color.r *= Cr; Color.g *= Cg; Color.b *= Cb; Color.a *= Ca;
			BlendPreviewPixel(Dst, px, py, Color);
		}
	}
}

struct SPreviewVertex
{
	float x, y, u, v, r, g, b, a;
};

static float Edge(float ax, float ay, float bx, float by, float px, float py)
{
	return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void DrawPreviewTriangle(CImageInfo &Dst, const CImageInfo *pTexture, const SPreviewVertex &A, const SPreviewVertex &B, const SPreviewVertex &C)
{
	const float Area = Edge(A.x, A.y, B.x, B.y, C.x, C.y);
	if(std::abs(Area) < 0.0001f)
		return;
	const int x0 = std::max(0, (int)std::floor(minimum(A.x, minimum(B.x, C.x))));
	const int y0 = std::max(0, (int)std::floor(minimum(A.y, minimum(B.y, C.y))));
	const int x1 = std::min((int)Dst.m_Width, (int)std::ceil(maximum(A.x, maximum(B.x, C.x))));
	const int y1 = std::min((int)Dst.m_Height, (int)std::ceil(maximum(A.y, maximum(B.y, C.y))));
	for(int y = y0; y < y1; ++y)
	{
		for(int x = x0; x < x1; ++x)
		{
			const float px = x + 0.5f, py = y + 0.5f;
			const float w0 = Edge(B.x, B.y, C.x, C.y, px, py) / Area;
			const float w1 = Edge(C.x, C.y, A.x, A.y, px, py) / Area;
			const float w2 = 1.0f - w0 - w1;
			if(w0 < -0.0001f || w1 < -0.0001f || w2 < -0.0001f)
				continue;
			SPreviewRgba Color;
			if(pTexture != nullptr && pTexture->m_pData != nullptr)
				Color = SampleImage(*pTexture, A.u*w0 + B.u*w1 + C.u*w2, A.v*w0 + B.v*w1 + C.v*w2);
			else
				Color = {1.0f, 1.0f, 1.0f, 1.0f};
			Color.r *= A.r*w0 + B.r*w1 + C.r*w2;
			Color.g *= A.g*w0 + B.g*w1 + C.g*w2;
			Color.b *= A.b*w0 + B.b*w1 + C.b*w2;
			Color.a *= A.a*w0 + B.a*w1 + C.a*w2;
			BlendPreviewPixel(Dst, x, y, Color);
		}
	}
}
}

CMapPreviews::CPreviewLoadJob::CPreviewLoadJob(CMapPreviews *pMapPreviews, std::string Key, std::string Path) :
	m_pMapPreviews(pMapPreviews),
	m_Key(std::move(Key)),
	m_Path(std::move(Path))
{
	Abortable(true);
}

CMapPreviews::CPreviewLoadJob::~CPreviewLoadJob()
{
	m_ImageInfo.Free();
}

void CMapPreviews::CPreviewLoadJob::Run()
{
	m_Success = m_pMapPreviews->LoadPreviewFile(m_Path.c_str(), m_ImageInfo);
}

CMapPreviews::CGenerateJob::CGenerateJob(CMapPreviews *pMapPreviews, std::string Key, std::string MapPath, std::string PreviewPath, int MapStorageType, int MapCrc, int MapSize) :
	m_pMapPreviews(pMapPreviews),
	m_Key(std::move(Key)),
	m_MapPath(std::move(MapPath)),
	m_PreviewPath(std::move(PreviewPath)),
	m_MapStorageType(MapStorageType),
	m_MapCrc(MapCrc),
	m_MapSize(MapSize)
{
	Abortable(true);
}

CMapPreviews::CGenerateJob::~CGenerateJob()
{
	m_ImageInfo.Free();
}

void CMapPreviews::CGenerateJob::Run()
{
	m_Success = m_pMapPreviews->GeneratePreview(m_MapPath.c_str(), m_MapStorageType, m_MapCrc, m_MapSize, m_PreviewPath.c_str(), m_ImageInfo);
}

CMapPreviews::CDownloadJob::CDownloadJob(CMapPreviews *pMapPreviews, std::string Key, std::string Path, const std::string &Url, int MapSize, const SHA256_DIGEST *pMapSha256) :
	CHttpRequest(Url.c_str()),
	m_Key(std::move(Key)),
	m_Path(std::move(Path))
{
	WriteToFile(pMapPreviews->Storage(), m_Path.c_str(), IStorage::TYPE_SAVE);
	Timeout(CTimeout{5000, 0, 1000, 10});
	MaxResponseSize(MapSize > 0 ? MapSize : 64 * 1024 * 1024);
	if(pMapSha256 != nullptr)
		ExpectSha256(*pMapSha256);
	LogProgress(HTTPLOG::NONE);
}

std::string CMapPreviews::MapKey(const char *pMapName, int MapCrc, const SHA256_DIGEST *pMapSha256)
{
	std::string Result = pMapName ? pMapName : "";
	Result.push_back('\n');
	if(pMapSha256 != nullptr)
	{
		char aSha256[SHA256_MAXSTRSIZE];
		sha256_str(*pMapSha256, aSha256, sizeof(aSha256));
		Result.append(aSha256);
	}
	else
	{
		char aCrc[16];
		str_format(aCrc, sizeof(aCrc), "%08x", (unsigned)MapCrc);
		Result.append(aCrc);
	}
	return Result;
}

std::string CMapPreviews::CacheStem(const char *pMapName, int MapCrc, const SHA256_DIGEST *pMapSha256)
{
	std::string Result;
	if(pMapName != nullptr)
	{
		for(const unsigned char *pChar = reinterpret_cast<const unsigned char *>(pMapName); *pChar != '\0'; ++pChar)
		{
			const unsigned char c = *pChar;
			const bool Safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
			Result.push_back(Safe ? static_cast<char>(c) : '_');
		}
	}
	Result.push_back('_');
	if(pMapSha256 != nullptr)
	{
		char aSha256[SHA256_MAXSTRSIZE];
		sha256_str(*pMapSha256, aSha256, sizeof(aSha256));
		Result.append(aSha256);
	}
	else
	{
		char aCrc[16];
		str_format(aCrc, sizeof(aCrc), "%08x", (unsigned)MapCrc);
		Result.append(aCrc);
	}
	return Result;
}

std::string CMapPreviews::DownloadedMapPath(const char *pMapName, int MapCrc, const SHA256_DIGEST *pMapSha256)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(pMapSha256 != nullptr)
	{
		char aSha256[SHA256_MAXSTRSIZE];
		sha256_str(*pMapSha256, aSha256, sizeof(aSha256));
		str_format(aPath, sizeof(aPath), "downloadedmaps/%s_%s.map", pMapName, aSha256);
	}
	else
		str_format(aPath, sizeof(aPath), "downloadedmaps/%s_%08x.map", pMapName, (unsigned)MapCrc);
	return aPath;
}

std::string CMapPreviews::PreviewPath(const char *pMapName, int MapCrc, const SHA256_DIGEST *pMapSha256)
{
	return "BestClient/map_previews/visual_v3_" + CacheStem(pMapName, MapCrc, pMapSha256) + ".png";
}

std::string CMapPreviews::MapUrl(const char *pMapName, int MapCrc, const SHA256_DIGEST *pMapSha256)
{
	char aFilename[IO_MAX_PATH_LENGTH];
	if(pMapSha256 != nullptr)
	{
		char aSha256[SHA256_MAXSTRSIZE];
		sha256_str(*pMapSha256, aSha256, sizeof(aSha256));
		str_format(aFilename, sizeof(aFilename), "%s_%s.map", pMapName, aSha256);
	}
	else
		str_format(aFilename, sizeof(aFilename), "%s_%08x.map", pMapName, (unsigned)MapCrc);
	char aEscaped[IO_MAX_PATH_LENGTH * 2];
	EscapeUrl(aEscaped, sizeof(aEscaped), aFilename);
	std::string Base = g_Config.m_ClMapDownloadUrl;
	while(!Base.empty() && Base.back() == '/')
		Base.pop_back();
	return Base + "/" + aEscaped;
}

bool CMapPreviews::LoadPreviewFile(const char *pPath, CImageInfo &ImageInfo)
{
	if(!Graphics()->LoadPng(ImageInfo, pPath, IStorage::TYPE_SAVE))
		return false;
	if(ImageInfo.m_Format != CImageInfo::FORMAT_RGBA)
	{
		ImageInfo.Free();
		return false;
	}
	return true;
}

bool CMapPreviews::GeneratePreview(const char *pMapPath, int StorageType, int WantedCrc, int WantedSize, const char *pPreviewPath, CImageInfo &ImageInfo)
{
	std::unique_ptr<IMap> pMap = CreateMap();
	if(!pMap->Load(Storage(), pMapPath, StorageType))
		return false;
	if(WantedCrc != 0 && (int)pMap->Crc() != WantedCrc)
		return false;
	if(WantedSize > 0 && pMap->Size() != WantedSize)
		return false;

	int GroupsStart = 0, GroupsNum = 0, LayersStart = 0, LayersNum = 0;
	pMap->GetType(MAPITEMTYPE_GROUP, &GroupsStart, &GroupsNum);
	pMap->GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);
	if(GroupsNum <= 0 || LayersNum <= 0)
		return false;

	const CMapItemGroup *pGameGroup = nullptr;
	const CMapItemLayerTilemap *pGameLayer = nullptr;
	for(int g = 0; g < GroupsNum && pGameLayer == nullptr; ++g)
	{
		const CMapItemGroup *pGroup = static_cast<const CMapItemGroup *>(pMap->GetItem(GroupsStart + g));
		if(pGroup == nullptr)
			continue;
		for(int l = 0; l < pGroup->m_NumLayers; ++l)
		{
			const int LayerIndex = pGroup->m_StartLayer + l;
			if(LayerIndex < 0 || LayerIndex >= LayersNum)
				continue;
			const CMapItemLayer *pLayer = static_cast<const CMapItemLayer *>(pMap->GetItem(LayersStart + LayerIndex));
			if(pLayer != nullptr && pLayer->m_Type == LAYERTYPE_TILES)
			{
				const CMapItemLayerTilemap *pTile = reinterpret_cast<const CMapItemLayerTilemap *>(pLayer);
				if(pTile->m_Flags & TILESLAYERFLAG_GAME)
				{
					pGameGroup = pGroup;
					pGameLayer = pTile;
					break;
				}
			}
		}
	}
	if(pGameLayer == nullptr || pGameLayer->m_Width <= 0 || pGameLayer->m_Height <= 0)
		return false;

	const int GameW = pGameLayer->m_Width;
	const int GameH = pGameLayer->m_Height;
	int MinX = GameW, MinY = GameH, MaxX = -1, MaxY = -1;
	if(pGameLayer->m_Data >= 0)
	{
		const CTile *pTiles = static_cast<const CTile *>(pMap->GetData(pGameLayer->m_Data));
		const int DataSize = pMap->GetDataSize(pGameLayer->m_Data);
		if(pTiles != nullptr && DataSize > 0)
		{
			for(int y = 0; y < GameH; ++y)
			{
				for(int x = 0; x < GameW; ++x)
				{
					const CTile &Tile = pTiles[(size_t)y * GameW + x];
					if(Tile.m_Index != 0)
					{
						MinX = minimum(MinX, x); MinY = minimum(MinY, y);
						MaxX = maximum(MaxX, x); MaxY = maximum(MaxY, y);
					}
					x += Tile.m_Skip;
				}
			}
		}
		pMap->UnloadData(pGameLayer->m_Data);
	}
	if(MaxX < MinX || MaxY < MinY)
	{
		MinX = 0; MinY = 0; MaxX = GameW - 1; MaxY = GameH - 1;
	}

	const float Padding = 3.0f * 32.0f;
	float CropLeft = maximum(0.0f, MinX * 32.0f - Padding);
	float CropTop = maximum(0.0f, MinY * 32.0f - Padding);
	float CropRight = minimum(GameW * 32.0f, (MaxX + 1) * 32.0f + Padding);
	float CropBottom = minimum(GameH * 32.0f, (MaxY + 1) * 32.0f + Padding);
	float CropW = maximum(32.0f, CropRight - CropLeft);
	float CropH = maximum(32.0f, CropBottom - CropTop);
	const float TargetAspect = 512.0f / 288.0f;
	if(CropW / CropH > TargetAspect)
	{
		const float NewH = CropW / TargetAspect;
		const float Grow = (NewH - CropH) * 0.5f;
		CropTop -= Grow; CropBottom += Grow; CropH = NewH;
	}
	else
	{
		const float NewW = CropH * TargetAspect;
		const float Grow = (NewW - CropW) * 0.5f;
		CropLeft -= Grow; CropRight += Grow; CropW = NewW;
	}
	const float CropCenterX = (CropLeft + CropRight) * 0.5f;
	const float CropCenterY = (CropTop + CropBottom) * 0.5f;
	const float GameParallaxX = pGameGroup != nullptr ? pGameGroup->m_ParallaxX / 100.0f : 1.0f;
	const float GameParallaxY = pGameGroup != nullptr ? pGameGroup->m_ParallaxY / 100.0f : 1.0f;
	const float GameOffsetX = pGameGroup != nullptr ? (float)pGameGroup->m_OffsetX : 0.0f;
	const float GameOffsetY = pGameGroup != nullptr ? (float)pGameGroup->m_OffsetY : 0.0f;
	const float CameraX = std::abs(GameParallaxX) > 0.001f ? (CropCenterX - GameOffsetX) / GameParallaxX : CropCenterX;
	const float CameraY = std::abs(GameParallaxY) > 0.001f ? (CropCenterY - GameOffsetY) / GameParallaxY : CropCenterY;

	// Match IGraphics::CalcScreenParams + MapScreenToWorld so editor/game
	// parallax zoom is preserved in the generated preview.
	float BaseViewW = std::sqrt(1150.0f * 1000.0f * TargetAspect);
	float BaseViewH = BaseViewW / TargetAspect;
	if(BaseViewW > 1500.0f)
	{
		BaseViewW = 1500.0f;
		BaseViewH = BaseViewW / TargetAspect;
	}
	if(BaseViewH > 1050.0f)
	{
		BaseViewH = 1050.0f;
		BaseViewW = BaseViewH * TargetAspect;
	}
	const float GameParallaxZoom = pGameGroup != nullptr ? (float)std::clamp(maximum(pGameGroup->m_ParallaxX, pGameGroup->m_ParallaxY), 0, 100) : 100.0f;
	float PreviewZoom = CropW / BaseViewW;
	if(GameParallaxZoom > 0.001f)
		PreviewZoom = 1.0f + (CropW / BaseViewW - 1.0f) * 100.0f / GameParallaxZoom;
	PreviewZoom = maximum(PreviewZoom, 0.01f);

	int ImageStart = 0, ImageNum = 0;
	pMap->GetType(MAPITEMTYPE_IMAGE, &ImageStart, &ImageNum);
	ImageNum = std::clamp(ImageNum, 0, (int)MAX_MAPIMAGES);
	std::vector<CImageInfo> vImages((size_t)ImageNum);
	std::vector<bool> vImageTried((size_t)ImageNum, false);
	auto GetImage = [&](int Index) -> const CImageInfo * {
		if(Index < 0 || Index >= ImageNum)
			return nullptr;
		if(!vImageTried[Index])
		{
			vImageTried[Index] = true;
			LoadPreviewMapImage(Storage(), pMap.get(), ImageStart, Index, vImages[Index]);
		}
		return vImages[Index].m_pData != nullptr ? &vImages[Index] : nullptr;
	};

	ImageInfo.m_Width = 512;
	ImageInfo.m_Height = 288;
	ImageInfo.m_Format = CImageInfo::FORMAT_RGBA;
	ImageInfo.m_pData = static_cast<uint8_t *>(malloc(ImageInfo.DataSize()));
	if(ImageInfo.m_pData == nullptr)
		return false;
	FillRect(ImageInfo, 0, 0, 512, 288, {20, 22, 26, 255});

	for(int g = 0; g < GroupsNum; ++g)
	{
		const CMapItemGroup *pGroup = static_cast<const CMapItemGroup *>(pMap->GetItem(GroupsStart + g));
		if(pGroup == nullptr)
			continue;
		const float ParallaxZoom = (float)std::clamp(maximum(pGroup->m_ParallaxX, pGroup->m_ParallaxY), 0, 100);
		const float GroupZoomFactor = (ParallaxZoom * (PreviewZoom - 1.0f) + 100.0f) / 100.0f;
		const float GroupViewW = BaseViewW * GroupZoomFactor;
		const float GroupViewH = BaseViewH * GroupZoomFactor;
		const float GroupLeft = pGroup->m_OffsetX + CameraX * (pGroup->m_ParallaxX / 100.0f) - GroupViewW * 0.5f;
		const float GroupTop = pGroup->m_OffsetY + CameraY * (pGroup->m_ParallaxY / 100.0f) - GroupViewH * 0.5f;
		auto ToScreenX = [&](float x) { return (x - GroupLeft) / GroupViewW * 512.0f; };
		auto ToScreenY = [&](float y) { return (y - GroupTop) / GroupViewH * 288.0f; };

		for(int l = 0; l < pGroup->m_NumLayers; ++l)
		{
			const int LayerIndex = pGroup->m_StartLayer + l;
			if(LayerIndex < 0 || LayerIndex >= LayersNum)
				continue;
			const CMapItemLayer *pLayer = static_cast<const CMapItemLayer *>(pMap->GetItem(LayersStart + LayerIndex));
			if(pLayer == nullptr)
				continue;
			if(pLayer->m_Type == LAYERTYPE_TILES)
			{
				const CMapItemLayerTilemap *pTilemap = reinterpret_cast<const CMapItemLayerTilemap *>(pLayer);
				if(pTilemap->m_Flags & (TILESLAYERFLAG_GAME | TILESLAYERFLAG_FRONT | TILESLAYERFLAG_TELE | TILESLAYERFLAG_SPEEDUP | TILESLAYERFLAG_SWITCH | TILESLAYERFLAG_TUNE))
					continue;
				const CImageInfo *pTexture = GetImage(pTilemap->m_Image);
				if(pTexture == nullptr || pTilemap->m_Data < 0 || pTilemap->m_Width <= 0 || pTilemap->m_Height <= 0)
					continue;
				const CTile *pTiles = static_cast<const CTile *>(pMap->GetData(pTilemap->m_Data));
				if(pTiles == nullptr)
					continue;
				const int StartX = std::clamp((int)std::floor(GroupLeft / 32.0f) - 1, 0, pTilemap->m_Width);
				const int EndX = std::clamp((int)std::ceil((GroupLeft + GroupViewW) / 32.0f) + 1, 0, pTilemap->m_Width);
				const int StartY = std::clamp((int)std::floor(GroupTop / 32.0f) - 1, 0, pTilemap->m_Height);
				const int EndY = std::clamp((int)std::ceil((GroupTop + GroupViewH) / 32.0f) + 1, 0, pTilemap->m_Height);
				for(int y = StartY; y < EndY; ++y)
				{
					for(int x = StartX; x < EndX; ++x)
					{
						const CTile &Tile = pTiles[(size_t)y * pTilemap->m_Width + x];
						if(Tile.m_Index != 0)
							DrawPreviewTile(ImageInfo, *pTexture, Tile.m_Index, Tile.m_Flags, ToScreenX(x * 32.0f), ToScreenY(y * 32.0f), ToScreenX((x + 1) * 32.0f), ToScreenY((y + 1) * 32.0f), pTilemap->m_Color);
						x += Tile.m_Skip;
					}
				}
				pMap->UnloadData(pTilemap->m_Data);
			}
			else if(pLayer->m_Type == LAYERTYPE_QUADS)
			{
				const CMapItemLayerQuads *pQuadsLayer = reinterpret_cast<const CMapItemLayerQuads *>(pLayer);
				if(pQuadsLayer->m_NumQuads <= 0 || pQuadsLayer->m_Data < 0)
					continue;
				const CImageInfo *pTexture = GetImage(pQuadsLayer->m_Image);
				const CQuad *pQuads = static_cast<const CQuad *>(pMap->GetDataSwapped(pQuadsLayer->m_Data));
				if(pQuads == nullptr)
					continue;
				for(int q = 0; q < pQuadsLayer->m_NumQuads; ++q)
				{
					const CQuad &Quad = pQuads[q];
					SPreviewVertex V[4];
					for(int v = 0; v < 4; ++v)
					{
						V[v].x = ToScreenX(fx2f(Quad.m_aPoints[v].x));
						V[v].y = ToScreenY(fx2f(Quad.m_aPoints[v].y));
						V[v].u = fx2f(Quad.m_aTexcoords[v].x);
						V[v].v = fx2f(Quad.m_aTexcoords[v].y);
						V[v].r = Quad.m_aColors[v].r / 255.0f;
						V[v].g = Quad.m_aColors[v].g / 255.0f;
						V[v].b = Quad.m_aColors[v].b / 255.0f;
						V[v].a = Quad.m_aColors[v].a / 255.0f;
					}
					DrawPreviewTriangle(ImageInfo, pTexture, V[0], V[1], V[3]);
					DrawPreviewTriangle(ImageInfo, pTexture, V[0], V[3], V[2]);
				}
				pMap->UnloadData(pQuadsLayer->m_Data);
			}
		}
	}

	for(CImageInfo &Img : vImages)
		Img.Free();
	IOHANDLE File = Storage()->OpenFile(pPreviewPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(File != nullptr && !CImageLoader::SavePng(File, pPreviewPath, ImageInfo))
		log_debug("map_previews", "Failed to save visual preview '%s'", pPreviewPath);
	return true;
}

void CMapPreviews::RemovePending(std::deque<std::string> &Queue, const std::string &Key)
{
	Queue.erase(std::remove(Queue.begin(), Queue.end(), Key), Queue.end());
}

void CMapPreviews::StartPreviewLoad(CEntry &Entry)
{
	Entry.m_State = EState::LOADING_CACHE;
	auto pJob = std::make_shared<CPreviewLoadJob>(this, Entry.m_Key, Entry.m_PreviewPath);
	Engine()->AddJob(pJob);
	m_PreviewLoadJobs.push_back(std::move(pJob));
}

void CMapPreviews::QueueGeneration(CEntry &Entry, const std::string &MapPath, int StorageType, bool DownloadCache)
{
	Entry.m_MapPath = MapPath;
	Entry.m_MapStorageType = StorageType;
	Entry.m_MapPathIsDownloadCache = DownloadCache;
	Entry.m_State = EState::QUEUED_GENERATE;
	RemovePending(m_PendingGenerations, Entry.m_Key);
	m_PendingGenerations.push_back(Entry.m_Key);
}

void CMapPreviews::PrepareEntry(CEntry &Entry)
{
	if(Storage()->FileExists(Entry.m_PreviewPath.c_str(), IStorage::TYPE_SAVE))
	{
		StartPreviewLoad(Entry);
		return;
	}

	char aNormalPath[IO_MAX_PATH_LENGTH];
	str_format(aNormalPath, sizeof(aNormalPath), "maps/%s.map", Entry.m_MapName.c_str());
	if(Storage()->FileExists(aNormalPath, IStorage::TYPE_ALL))
	{
		QueueGeneration(Entry, aNormalPath, IStorage::TYPE_ALL, false);
		return;
	}

	const std::string DownloadPath = DownloadedMapPath(Entry.m_MapName.c_str(), Entry.m_MapCrc, Entry.m_HasMapSha256 ? &Entry.m_MapSha256 : nullptr);
	if(Storage()->FileExists(DownloadPath.c_str(), IStorage::TYPE_SAVE))
	{
		QueueGeneration(Entry, DownloadPath, IStorage::TYPE_SAVE, true);
		return;
	}

	Entry.m_MapPath = DownloadPath;
	Entry.m_MapStorageType = IStorage::TYPE_SAVE;
	Entry.m_MapPathIsDownloadCache = true;
	Entry.m_State = EState::QUEUED_DOWNLOAD;
	RemovePending(m_PendingDownloads, Entry.m_Key);
	m_PendingDownloads.push_back(Entry.m_Key);
}

void CMapPreviews::QueueMap(const CServerInfo &Info)
{
	QueueMap(Info.m_aMap, Info.m_MapCrc, Info.m_MapSize, Info.m_HasMapSha256 ? &Info.m_MapSha256 : nullptr, Info.m_aMapUrl);
}

void CMapPreviews::QueueMap(const char *pMapName, int MapCrc, int MapSize, const SHA256_DIGEST *pMapSha256, const char *pMapUrl)
{
	if(pMapName == nullptr || pMapName[0] == '\0')
		return;
	const std::string Key = MapKey(pMapName, MapCrc, pMapSha256);
	auto [Iterator, Inserted] = m_Entries.try_emplace(Key);
	CEntry &Entry = Iterator->second;
	if(!Inserted)
	{
		if(Entry.m_MapSize <= 0 && MapSize > 0)
			Entry.m_MapSize = MapSize;
		if(Entry.m_MapUrl.empty() && pMapUrl != nullptr && pMapUrl[0] != '\0')
			Entry.m_MapUrl = pMapUrl;
		return;
	}
	Entry.m_Key = Key;
	Entry.m_MapName = pMapName;
	if(pMapUrl != nullptr && pMapUrl[0] != '\0')
		Entry.m_MapUrl = pMapUrl;
	Entry.m_MapCrc = MapCrc;
	Entry.m_MapSize = MapSize;
	Entry.m_HasMapSha256 = pMapSha256 != nullptr;
	if(pMapSha256 != nullptr)
		Entry.m_MapSha256 = *pMapSha256;
	Entry.m_PreviewPath = PreviewPath(pMapName, MapCrc, pMapSha256);
	PrepareEntry(Entry);
}

void CMapPreviews::StartGeneration(CEntry &Entry)
{
	Entry.m_State = EState::GENERATING;
	auto pJob = std::make_shared<CGenerateJob>(this, Entry.m_Key, Entry.m_MapPath, Entry.m_PreviewPath, Entry.m_MapStorageType, Entry.m_MapCrc, Entry.m_MapSize);
	Engine()->AddJob(pJob);
	m_GenerateJobs.push_back(std::move(pJob));
}

void CMapPreviews::StartDownload(CEntry &Entry)
{
	if(!Entry.m_HasMapSha256 && Entry.m_MapCrc == 0)
	{
		Entry.m_State = EState::FAILED;
		return;
	}
	const SHA256_DIGEST *pMapSha256 = Entry.m_HasMapSha256 ? &Entry.m_MapSha256 : nullptr;
	const std::string Url = !Entry.m_MapUrl.empty() ? Entry.m_MapUrl : MapUrl(Entry.m_MapName.c_str(), Entry.m_MapCrc, pMapSha256);
	auto pJob = std::make_shared<CDownloadJob>(this, Entry.m_Key, Entry.m_MapPath, Url, Entry.m_MapSize, pMapSha256);
	Entry.m_State = EState::DOWNLOADING;
	Http()->Run(pJob);
	m_DownloadJobs.push_back(std::move(pJob));
}

void CMapPreviews::Load()
{
	Storage()->CreateFolder("BestClient", IStorage::TYPE_SAVE);
	Storage()->CreateFolder("BestClient/map_previews", IStorage::TYPE_SAVE);
	Storage()->CreateFolder("downloadedmaps", IStorage::TYPE_SAVE);
}

void CMapPreviews::Prioritize(const char *pMapName, int MapCrc, int MapSize, const SHA256_DIGEST *pMapSha256)
{
	QueueMap(pMapName, MapCrc, MapSize, pMapSha256);
	m_PriorityKey = MapKey(pMapName, MapCrc, pMapSha256);
	auto Iterator = m_Entries.find(m_PriorityKey);
	if(Iterator == m_Entries.end())
		return;
	if(Iterator->second.m_State == EState::QUEUED_DOWNLOAD)
	{
		RemovePending(m_PendingDownloads, m_PriorityKey);
		m_PendingDownloads.push_front(m_PriorityKey);
	}
	else if(Iterator->second.m_State == EState::QUEUED_GENERATE)
	{
		RemovePending(m_PendingGenerations, m_PriorityKey);
		m_PendingGenerations.push_front(m_PriorityKey);
	}
}

void CMapPreviews::ClearPriority()
{
	m_PriorityKey.clear();
}

void CMapPreviews::Update()
{
	for(int i = 0; i < ServerBrowser()->NumServers(); ++i)
	{
		const CServerInfo *pInfo = ServerBrowser()->Get(i);
		if(pInfo != nullptr && pInfo->m_aMap[0] != '\0')
			QueueMap(*pInfo);
	}

	for(auto Iterator = m_PreviewLoadJobs.begin(); Iterator != m_PreviewLoadJobs.end();)
	{
		const std::shared_ptr<CPreviewLoadJob> pJob = *Iterator;
		if(!pJob->Done())
		{
			++Iterator;
			continue;
		}
		auto EntryIterator = m_Entries.find(pJob->Key());
		if(EntryIterator != m_Entries.end())
		{
			CEntry &Entry = EntryIterator->second;
			if(pJob->Success())
			{
				CImageInfo &ImageInfo = pJob->ImageInfo();
				Entry.m_Preview.m_Width = (int)ImageInfo.m_Width;
				Entry.m_Preview.m_Height = (int)ImageInfo.m_Height;
				Entry.m_Preview.m_Texture = Graphics()->LoadTextureRawMove(ImageInfo, 0, Entry.m_MapName.c_str());
				Entry.m_State = Entry.m_Preview.m_Texture.IsValid() ? EState::READY : EState::FAILED;
			}
			else
			{
				Storage()->RemoveFile(Entry.m_PreviewPath.c_str(), IStorage::TYPE_SAVE);
				PrepareEntry(Entry);
			}
		}
		Iterator = m_PreviewLoadJobs.erase(Iterator);
	}

	for(auto Iterator = m_GenerateJobs.begin(); Iterator != m_GenerateJobs.end();)
	{
		const std::shared_ptr<CGenerateJob> pJob = *Iterator;
		if(!pJob->Done())
		{
			++Iterator;
			continue;
		}
		auto EntryIterator = m_Entries.find(pJob->Key());
		if(EntryIterator != m_Entries.end())
		{
			CEntry &Entry = EntryIterator->second;
			if(pJob->Success())
			{
				CImageInfo &ImageInfo = pJob->ImageInfo();
				Entry.m_Preview.m_Width = (int)ImageInfo.m_Width;
				Entry.m_Preview.m_Height = (int)ImageInfo.m_Height;
				Entry.m_Preview.m_Texture = Graphics()->LoadTextureRawMove(ImageInfo, 0, Entry.m_MapName.c_str());
				Entry.m_State = Entry.m_Preview.m_Texture.IsValid() ? EState::READY : EState::FAILED;
			}
			else if(!Entry.m_MapPathIsDownloadCache)
			{
				Entry.m_MapPath = DownloadedMapPath(Entry.m_MapName.c_str(), Entry.m_MapCrc, Entry.m_HasMapSha256 ? &Entry.m_MapSha256 : nullptr);
				Entry.m_MapStorageType = IStorage::TYPE_SAVE;
				Entry.m_MapPathIsDownloadCache = true;
				Entry.m_State = EState::QUEUED_DOWNLOAD;
				RemovePending(m_PendingDownloads, Entry.m_Key);
				m_PendingDownloads.push_back(Entry.m_Key);
			}
			else
			{
				// The downloaded file is hash-verified. If generation still fails,
				// retrying the same download forever only wastes bandwidth.
				Entry.m_State = EState::FAILED;
			}
		}
		Iterator = m_GenerateJobs.erase(Iterator);
	}

	for(auto Iterator = m_DownloadJobs.begin(); Iterator != m_DownloadJobs.end();)
	{
		const std::shared_ptr<CDownloadJob> pJob = *Iterator;
		if(!pJob->Done())
		{
			++Iterator;
			continue;
		}
		auto EntryIterator = m_Entries.find(pJob->Key());
		if(EntryIterator != m_Entries.end())
		{
			CEntry &Entry = EntryIterator->second;
			if(pJob->State() == EHttpState::DONE && Storage()->FileExists(Entry.m_MapPath.c_str(), IStorage::TYPE_SAVE))
				QueueGeneration(Entry, Entry.m_MapPath, IStorage::TYPE_SAVE, true);
			else
			{
				Storage()->RemoveFile(Entry.m_MapPath.c_str(), IStorage::TYPE_SAVE);
				Entry.m_State = EState::FAILED;
			}
		}
		Iterator = m_DownloadJobs.erase(Iterator);
	}

	if(!m_PriorityKey.empty() && m_GenerateJobs.size() < MAX_PARALLEL_GENERATIONS)
	{
		auto PriorityIterator = m_Entries.find(m_PriorityKey);
		if(PriorityIterator != m_Entries.end() && PriorityIterator->second.m_State == EState::QUEUED_GENERATE)
		{
			RemovePending(m_PendingGenerations, m_PriorityKey);
			StartGeneration(PriorityIterator->second);
		}
	}
	while(m_GenerateJobs.size() < MAX_BACKGROUND_GENERATIONS && !m_PendingGenerations.empty())
	{
		std::string Key = std::move(m_PendingGenerations.front());
		m_PendingGenerations.pop_front();
		auto EntryIterator = m_Entries.find(Key);
		if(EntryIterator == m_Entries.end() || EntryIterator->second.m_State != EState::QUEUED_GENERATE)
			continue;
		StartGeneration(EntryIterator->second);
	}

	if(!m_PriorityKey.empty() && m_DownloadJobs.size() < MAX_PARALLEL_DOWNLOADS)
	{
		auto PriorityIterator = m_Entries.find(m_PriorityKey);
		if(PriorityIterator != m_Entries.end() && PriorityIterator->second.m_State == EState::QUEUED_DOWNLOAD)
		{
			RemovePending(m_PendingDownloads, m_PriorityKey);
			StartDownload(PriorityIterator->second);
		}
	}
	while(m_DownloadJobs.size() < MAX_BACKGROUND_DOWNLOADS && !m_PendingDownloads.empty())
	{
		std::string Key = std::move(m_PendingDownloads.front());
		m_PendingDownloads.pop_front();
		auto EntryIterator = m_Entries.find(Key);
		if(EntryIterator == m_Entries.end() || EntryIterator->second.m_State != EState::QUEUED_DOWNLOAD)
			continue;
		StartDownload(EntryIterator->second);
	}
}

const CMapPreview *CMapPreviews::Find(const char *pMapName, int MapCrc, const SHA256_DIGEST *pMapSha256) const
{
	const auto Iterator = m_Entries.find(MapKey(pMapName, MapCrc, pMapSha256));
	if(Iterator == m_Entries.end() || Iterator->second.m_State != EState::READY || !Iterator->second.m_Preview.m_Texture.IsValid())
		return nullptr;
	return &Iterator->second.m_Preview;
}

const char *CMapPreviews::StatusText(const char *pMapName, int MapCrc, const SHA256_DIGEST *pMapSha256) const
{
	const auto Iterator = m_Entries.find(MapKey(pMapName, MapCrc, pMapSha256));
	if(Iterator == m_Entries.end())
		return "Waiting for map...";
	switch(Iterator->second.m_State)
	{
	case EState::QUEUED_DOWNLOAD: return "Waiting for map download...";
	case EState::DOWNLOADING: return "Downloading map...";
	case EState::QUEUED_GENERATE: return "Waiting to generate preview...";
	case EState::GENERATING: return "Generating preview...";
	case EState::LOADING_CACHE: return "Loading cached preview...";
	case EState::FAILED: return (!Iterator->second.m_HasMapSha256 && Iterator->second.m_MapCrc == 0) ? "Map download metadata unavailable" : "Preview unavailable";
	case EState::READY: return "";
	}
	return "";
}

void CMapPreviews::Render(const CMapPreview *pPreview, CUIRect Rect)
{
	if(pPreview == nullptr || !pPreview->m_Texture.IsValid() || pPreview->m_Width <= 0 || pPreview->m_Height <= 0)
		return;
	const float ImageAspect = static_cast<float>(pPreview->m_Width) / static_cast<float>(pPreview->m_Height);
	const float RectAspect = Rect.w / Rect.h;
	if(RectAspect > ImageAspect)
	{
		const float NewWidth = Rect.h * ImageAspect;
		Rect.x += (Rect.w - NewWidth) * 0.5f;
		Rect.w = NewWidth;
	}
	else
	{
		const float NewHeight = Rect.w / ImageAspect;
		Rect.y += (Rect.h - NewHeight) * 0.5f;
		Rect.h = NewHeight;
	}
	Graphics()->WrapClamp();
	Graphics()->TextureSet(pPreview->m_Texture);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	const IGraphics::CQuadItem Quad(Rect.x, Rect.y, Rect.w, Rect.h);
	Graphics()->QuadsDrawTL(&Quad, 1);
	Graphics()->QuadsEnd();
	Graphics()->WrapNormal();
}

void CMapPreviews::Shutdown()
{
	m_PendingDownloads.clear();
	m_PendingGenerations.clear();
	m_PreviewLoadJobs.clear();
	m_GenerateJobs.clear();
	m_DownloadJobs.clear();
	m_PriorityKey.clear();
	for(auto &[Key, Entry] : m_Entries)
	{
		(void)Key;
		if(Entry.m_Preview.m_Texture.IsValid())
			Graphics()->UnloadTexture(&Entry.m_Preview.m_Texture);
	}
	m_Entries.clear();
}
