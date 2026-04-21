/* Copyright © 2026 BestProject Team */
#include "cef_runtime.h"

#include <base/log.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/storage.h>

#if defined(CONF_FAMILY_WINDOWS) && defined(CONF_BESTCLIENT_CEF)

#include <base/system.h>
#include <base/windows.h>

#include <include/cef_app.h>
#include <include/cef_browser.h>
#include <include/cef_client.h>
#include <include/cef_command_line.h>
#include <include/cef_life_span_handler.h>
#include <include/cef_sandbox_win.h>

#include <filesystem>
#include <memory>
#include <string>

namespace
{
class CBestClientCefApp final : public CefApp
{
	IMPLEMENT_REFCOUNTING(CBestClientCefApp);
};

std::filesystem::path GetModulePath()
{
	wchar_t aPath[MAX_PATH] = {};
	const DWORD Length = GetModuleFileNameW(nullptr, aPath, sizeof(aPath) / sizeof(aPath[0]));
	if(Length == 0 || Length >= sizeof(aPath) / sizeof(aPath[0]))
		return {};
	return std::filesystem::path(aPath);
}

std::filesystem::path GetModuleDirectory()
{
	const std::filesystem::path ModulePath = GetModulePath();
	return ModulePath.empty() ? std::filesystem::path() : ModulePath.parent_path();
}
}

class CBestClientBrowserImpl;

class CBestClientBrowserHandler final : public CefClient, public CefLifeSpanHandler
{
public:
	explicit CBestClientBrowserHandler(CBestClientBrowserImpl *pOwner) :
		m_pOwner(pOwner)
	{
	}

	CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
	void OnAfterCreated(CefRefPtr<CefBrowser> pBrowser) override;
	void OnBeforeClose(CefRefPtr<CefBrowser> pBrowser) override;

private:
	CBestClientBrowserImpl *m_pOwner;

	IMPLEMENT_REFCOUNTING(CBestClientBrowserHandler);
};

class CBestClientBrowserImpl
{
public:
	CBestClientBrowserImpl(IClient *pClient, IGraphics *pGraphics, IStorage *pStorage) :
		m_pClient(pClient),
		m_pGraphics(pGraphics),
		m_pStorage(pStorage)
	{
		str_copy(m_aStatus, "CEF not initialized", sizeof(m_aStatus));
		str_copy(m_aCurrentUrl, "https://www.google.com/", sizeof(m_aCurrentUrl));
	}

	void OnRender()
	{
		if(m_Initialized)
			CefDoMessageLoopWork();

		if(m_Visible && m_hBrowser != nullptr)
			ApplyBounds();
	}

	void OnWindowResize()
	{
		if(m_Visible && m_hBrowser != nullptr)
			ApplyBounds();
	}

	void Shutdown()
	{
		Hide();
		if(m_pBrowser)
		{
			m_pBrowser->GetHost()->CloseBrowser(true);
			m_pBrowser = nullptr;
			m_hBrowser = nullptr;
		}

		m_pHandler = nullptr;

		if(m_Initialized)
		{
			CefDoMessageLoopWork();
			CefShutdown();
			m_Initialized = false;
		}
	}

	void Show(int X, int Y, int Width, int Height, const char *pUrl)
	{
		if(!EnsureInitialized())
			return;

		m_X = maximum(X, 0);
		m_Y = maximum(Y, 0);
		m_Width = maximum(Width, 1);
		m_Height = maximum(Height, 1);
		m_Visible = true;

		if(pUrl != nullptr && pUrl[0] != '\0' && str_comp(m_aCurrentUrl, pUrl) != 0)
		{
			str_copy(m_aCurrentUrl, pUrl, sizeof(m_aCurrentUrl));
			if(m_pBrowser)
				m_pBrowser->GetMainFrame()->LoadURL(m_aCurrentUrl);
		}

		EnsureBrowser();
		ApplyBounds();
	}

	void Hide()
	{
		m_Visible = false;
		if(m_hBrowser != nullptr)
			ShowWindow(m_hBrowser, SW_HIDE);
	}

	bool IsAvailable() const
	{
		return m_Initialized && m_hBrowser != nullptr;
	}

	const char *Status() const
	{
		return m_aStatus;
	}

	void OnAfterCreated(CefRefPtr<CefBrowser> pBrowser)
	{
		m_pBrowser = pBrowser;
		m_hBrowser = pBrowser->GetHost()->GetWindowHandle();
		ApplyBounds();
		str_copy(m_aStatus, "CEF browser is ready", sizeof(m_aStatus));
		log_info("bestclient-cef", "browser created");
	}

	void OnBeforeClose(CefRefPtr<CefBrowser> pBrowser)
	{
		if(m_pBrowser && pBrowser && m_pBrowser->IsSame(pBrowser))
		{
			m_pBrowser = nullptr;
			m_hBrowser = nullptr;
			str_copy(m_aStatus, "CEF browser closed", sizeof(m_aStatus));
		}
	}

private:
	bool EnsureInitialized()
	{
		if(m_Initialized)
			return true;
		if(m_InitializationAttempted)
			return false;

		m_InitializationAttempted = true;
		m_hParent = static_cast<HWND>(m_pGraphics->NativeWindowHandle());
		if(m_hParent == nullptr)
		{
			str_copy(m_aStatus, "CEF init failed: missing native window handle", sizeof(m_aStatus));
			log_error("bestclient-cef", "%s", m_aStatus);
			return false;
		}

		m_pStorage->CreateFolder("BestClient", IStorage::TYPE_SAVE);
		m_pStorage->CreateFolder("BestClient/cef_cache", IStorage::TYPE_SAVE);

		char aCachePath[IO_MAX_PATH_LENGTH];
		char aLogPath[IO_MAX_PATH_LENGTH];
		m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "BestClient/cef_cache", aCachePath, sizeof(aCachePath));
		m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "BestClient/cef.log", aLogPath, sizeof(aLogPath));

		const std::filesystem::path ModulePath = GetModulePath();
		const std::filesystem::path ModuleDir = GetModuleDirectory();
		if(ModulePath.empty() || ModuleDir.empty())
		{
			str_copy(m_aStatus, "CEF init failed: unable to resolve module path", sizeof(m_aStatus));
			log_error("bestclient-cef", "%s", m_aStatus);
			return false;
		}

		CefMainArgs MainArgs(GetModuleHandleW(nullptr));
		CefSettings Settings;
		Settings.no_sandbox = true;
		Settings.multi_threaded_message_loop = false;
		Settings.external_message_pump = false;
		Settings.command_line_args_disabled = false;

		CefString(&Settings.browser_subprocess_path) = ModulePath.native();
		CefString(&Settings.resources_dir_path) = ModuleDir.native();
		CefString(&Settings.locales_dir_path) = (ModuleDir / L"locales").native();
		CefString(&Settings.cache_path) = windows_utf8_to_wide(aCachePath);
		CefString(&Settings.log_file) = windows_utf8_to_wide(aLogPath);

		CefRefPtr<CBestClientCefApp> pApp(new CBestClientCefApp);
		if(!CefInitialize(MainArgs, Settings, pApp.get(), nullptr))
		{
			str_copy(m_aStatus, "CEF init failed: CefInitialize returned false", sizeof(m_aStatus));
			log_error("bestclient-cef", "%s", m_aStatus);
			return false;
		}

		m_Initialized = true;
		str_copy(m_aStatus, "CEF initialized", sizeof(m_aStatus));
		log_info("bestclient-cef", "initialized");
		return true;
	}

	void EnsureBrowser()
	{
		if(m_pBrowser || m_hParent == nullptr)
			return;

		CefWindowInfo WindowInfo;
		WindowInfo.SetAsChild(m_hParent, CefRect(m_X, m_Y, m_Width, m_Height));

		CefBrowserSettings BrowserSettings;
		m_pHandler = new CBestClientBrowserHandler(this);
		m_pBrowser = CefBrowserHost::CreateBrowserSync(WindowInfo, m_pHandler, m_aCurrentUrl, BrowserSettings, nullptr, nullptr);
		if(!m_pBrowser)
		{
			str_copy(m_aStatus, "CEF browser creation failed", sizeof(m_aStatus));
			log_error("bestclient-cef", "%s", m_aStatus);
		}
	}

	void ApplyBounds()
	{
		if(m_hBrowser == nullptr)
			return;

		SetWindowPos(m_hBrowser, HWND_TOP, m_X, m_Y, m_Width, m_Height, SWP_NOACTIVATE);
		ShowWindow(m_hBrowser, m_Visible ? SW_SHOW : SW_HIDE);
		if(m_pBrowser)
		{
			m_pBrowser->GetHost()->NotifyMoveOrResizeStarted();
			m_pBrowser->GetHost()->WasResized();
		}
	}

	IClient *m_pClient;
	IGraphics *m_pGraphics;
	IStorage *m_pStorage;
	bool m_InitializationAttempted = false;
	bool m_Initialized = false;
	bool m_Visible = false;
	int m_X = 0;
	int m_Y = 0;
	int m_Width = 1;
	int m_Height = 1;
	HWND m_hParent = nullptr;
	HWND m_hBrowser = nullptr;
	CefRefPtr<CefBrowser> m_pBrowser;
	CefRefPtr<CBestClientBrowserHandler> m_pHandler;
	char m_aCurrentUrl[256];
	char m_aStatus[256];
};

void CBestClientBrowserHandler::OnAfterCreated(CefRefPtr<CefBrowser> pBrowser)
{
	m_pOwner->OnAfterCreated(pBrowser);
}

void CBestClientBrowserHandler::OnBeforeClose(CefRefPtr<CefBrowser> pBrowser)
{
	m_pOwner->OnBeforeClose(pBrowser);
}

CBestClientBrowser::CBestClientBrowser(IClient *pClient, IGraphics *pGraphics, IStorage *pStorage) :
	m_pImpl(new CBestClientBrowserImpl(pClient, pGraphics, pStorage))
{
}

CBestClientBrowser::~CBestClientBrowser()
{
	delete m_pImpl;
}

void CBestClientBrowser::OnRender()
{
	m_pImpl->OnRender();
}

void CBestClientBrowser::OnWindowResize()
{
	m_pImpl->OnWindowResize();
}

void CBestClientBrowser::Shutdown()
{
	m_pImpl->Shutdown();
}

void CBestClientBrowser::Show(int X, int Y, int Width, int Height, const char *pUrl)
{
	m_pImpl->Show(X, Y, Width, Height, pUrl);
}

void CBestClientBrowser::Hide()
{
	m_pImpl->Hide();
}

bool CBestClientBrowser::IsAvailable() const
{
	return m_pImpl->IsAvailable();
}

const char *CBestClientBrowser::Status() const
{
	return m_pImpl->Status();
}

int BestClientCefExecuteSubprocess()
{
	CefMainArgs MainArgs(GetModuleHandleW(nullptr));
	CefRefPtr<CBestClientCefApp> pApp(new CBestClientCefApp);
	return CefExecuteProcess(MainArgs, pApp.get(), nullptr);
}

#else

class CBestClientBrowserImpl
{
public:
	CBestClientBrowserImpl(IClient *pClient, IGraphics *pGraphics, IStorage *pStorage)
	{
		(void)pClient;
		(void)pGraphics;
		(void)pStorage;
	}

	void OnRender() {}
	void OnWindowResize() {}
	void Shutdown() {}
	void Show(int X, int Y, int Width, int Height, const char *pUrl)
	{
		(void)X;
		(void)Y;
		(void)Width;
		(void)Height;
		(void)pUrl;
	}
	void Hide() {}
	bool IsAvailable() const { return false; }
	const char *Status() const { return "CEF is only available in Windows builds with CONF_BESTCLIENT_CEF enabled"; }
};

CBestClientBrowser::CBestClientBrowser(IClient *pClient, IGraphics *pGraphics, IStorage *pStorage) :
	m_pImpl(new CBestClientBrowserImpl(pClient, pGraphics, pStorage))
{
}

CBestClientBrowser::~CBestClientBrowser()
{
	delete m_pImpl;
}

void CBestClientBrowser::OnRender()
{
	m_pImpl->OnRender();
}

void CBestClientBrowser::OnWindowResize()
{
	m_pImpl->OnWindowResize();
}

void CBestClientBrowser::Shutdown()
{
	m_pImpl->Shutdown();
}

void CBestClientBrowser::Show(int X, int Y, int Width, int Height, const char *pUrl)
{
	m_pImpl->Show(X, Y, Width, Height, pUrl);
}

void CBestClientBrowser::Hide()
{
	m_pImpl->Hide();
}

bool CBestClientBrowser::IsAvailable() const
{
	return m_pImpl->IsAvailable();
}

const char *CBestClientBrowser::Status() const
{
	return m_pImpl->Status();
}

int BestClientCefExecuteSubprocess()
{
	return -1;
}

#endif
