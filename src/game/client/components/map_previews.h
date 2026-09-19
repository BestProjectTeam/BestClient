#ifndef GAME_CLIENT_COMPONENTS_MAP_PREVIEWS_H
#define GAME_CLIENT_COMPONENTS_MAP_PREVIEWS_H

#include <engine/graphics.h>
#include <engine/serverbrowser.h>
#include <engine/shared/http.h>
#include <engine/shared/jobs.h>

#include <game/client/component.h>
#include <game/client/ui_rect.h>

#include <deque>
#include <memory>
#include <string>
#include <unordered_map>

class CMapPreview
{
	friend class CMapPreviews;

	IGraphics::CTextureHandle m_Texture;
	int m_Width = 0;
	int m_Height = 0;
};

class CMapPreviews : public CComponentInterfaces
{
public:
	void Load();
	void Update();
	void Shutdown();

	void Prioritize(const char *pMapName, int MapCrc, int MapSize, const SHA256_DIGEST *pMapSha256);
	void ClearPriority();
	const CMapPreview *Find(const char *pMapName, int MapCrc, const SHA256_DIGEST *pMapSha256) const;
	const char *StatusText(const char *pMapName, int MapCrc, const SHA256_DIGEST *pMapSha256) const;
	void Render(const CMapPreview *pPreview, CUIRect Rect);

private:
	enum class EState
	{
		QUEUED_DOWNLOAD,
		DOWNLOADING,
		QUEUED_GENERATE,
		GENERATING,
		LOADING_CACHE,
		READY,
		FAILED,
	};

	struct CEntry
	{
		std::string m_Key;
		std::string m_MapName;
		std::string m_MapUrl;
		std::string m_MapPath;
		std::string m_PreviewPath;
		int m_MapCrc = 0;
		int m_MapSize = 0;
		bool m_HasMapSha256 = false;
		SHA256_DIGEST m_MapSha256{};
		int m_MapStorageType = 0;
		bool m_MapPathIsDownloadCache = false;
		EState m_State = EState::QUEUED_DOWNLOAD;
		CMapPreview m_Preview;
	};

	class CPreviewLoadJob : public IJob
	{
		CMapPreviews *m_pMapPreviews;
		std::string m_Key;
		std::string m_Path;
		CImageInfo m_ImageInfo;
		bool m_Success = false;

	protected:
		void Run() override;

	public:
		CPreviewLoadJob(CMapPreviews *pMapPreviews, std::string Key, std::string Path);
		~CPreviewLoadJob() override;

		const std::string &Key() const { return m_Key; }
		bool Success() const { return m_Success; }
		CImageInfo &ImageInfo() { return m_ImageInfo; }
	};

	class CGenerateJob : public IJob
	{
		CMapPreviews *m_pMapPreviews;
		std::string m_Key;
		std::string m_MapPath;
		std::string m_PreviewPath;
		int m_MapStorageType;
		int m_MapCrc;
		int m_MapSize;
		CImageInfo m_ImageInfo;
		bool m_Success = false;

	protected:
		void Run() override;

	public:
		CGenerateJob(CMapPreviews *pMapPreviews, std::string Key, std::string MapPath, std::string PreviewPath, int MapStorageType, int MapCrc, int MapSize);
		~CGenerateJob() override;

		const std::string &Key() const { return m_Key; }
		bool Success() const { return m_Success; }
		CImageInfo &ImageInfo() { return m_ImageInfo; }
	};

	class CDownloadJob : public CHttpRequest
	{
		std::string m_Key;
		std::string m_Path;

	public:
		CDownloadJob(CMapPreviews *pMapPreviews, std::string Key, std::string Path, const std::string &Url, int MapSize, const SHA256_DIGEST *pMapSha256);

		const std::string &Key() const { return m_Key; }
		const std::string &Path() const { return m_Path; }
	};

	std::unordered_map<std::string, CEntry> m_Entries;
	std::deque<std::string> m_PendingDownloads;
	std::deque<std::string> m_PendingGenerations;
	std::deque<std::shared_ptr<CPreviewLoadJob>> m_PreviewLoadJobs;
	std::deque<std::shared_ptr<CGenerateJob>> m_GenerateJobs;
	std::deque<std::shared_ptr<CDownloadJob>> m_DownloadJobs;
	std::string m_PriorityKey;

	static std::string MapKey(const char *pMapName, int MapCrc, const SHA256_DIGEST *pMapSha256);
	static std::string CacheStem(const char *pMapName, int MapCrc, const SHA256_DIGEST *pMapSha256);
	static std::string DownloadedMapPath(const char *pMapName, int MapCrc, const SHA256_DIGEST *pMapSha256);
	static std::string PreviewPath(const char *pMapName, int MapCrc, const SHA256_DIGEST *pMapSha256);
	static std::string MapUrl(const char *pMapName, int MapCrc, const SHA256_DIGEST *pMapSha256);

	void QueueMap(const CServerInfo &Info);
	void QueueMap(const char *pMapName, int MapCrc, int MapSize, const SHA256_DIGEST *pMapSha256, const char *pMapUrl = nullptr);
	void PrepareEntry(CEntry &Entry);
	void QueueGeneration(CEntry &Entry, const std::string &MapPath, int StorageType, bool DownloadCache);
	void StartPreviewLoad(CEntry &Entry);
	void StartGeneration(CEntry &Entry);
	void StartDownload(CEntry &Entry);
	void RemovePending(std::deque<std::string> &Queue, const std::string &Key);
	bool LoadPreviewFile(const char *pPath, CImageInfo &ImageInfo);
	bool GeneratePreview(const char *pMapPath, int StorageType, int WantedCrc, int WantedSize, const char *pPreviewPath, CImageInfo &ImageInfo);

	static constexpr size_t MAX_BACKGROUND_DOWNLOADS = 5;
	static constexpr size_t MAX_PARALLEL_DOWNLOADS = 6;
	static constexpr size_t MAX_BACKGROUND_GENERATIONS = 2;
	static constexpr size_t MAX_PARALLEL_GENERATIONS = 3;
};

#endif
