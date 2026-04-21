/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_BROWSER_CEF_RUNTIME_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_BROWSER_CEF_RUNTIME_H

class IClient;
class IGraphics;
class IStorage;
class CBestClientBrowserImpl;

class CBestClientBrowser
{
public:
	CBestClientBrowser(IClient *pClient, IGraphics *pGraphics, IStorage *pStorage);
	~CBestClientBrowser();

	void OnRender();
	void OnWindowResize();
	void Shutdown();

	void Show(int X, int Y, int Width, int Height, const char *pUrl);
	void Hide();

	bool IsAvailable() const;
	const char *Status() const;

private:
	CBestClientBrowserImpl *m_pImpl;
};

int BestClientCefExecuteSubprocess();

#endif
