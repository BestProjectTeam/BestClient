/* Copyright 2026 BestProject Team */
#include "irc.h"

#include <base/color.h>
#include <base/net.h>
#include <base/log.h>
#include <base/math.h>
#include <base/system.h>

#include <engine/client.h>
#include <engine/font_icons.h>
#include <engine/shared/config.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <engine/textrender.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/components/menus.h>
#include <game/client/components/skins.h>
#include <game/localization.h>

#if defined(CONF_OPENSSL)
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#elif defined(CONF_FAMILY_WINDOWS)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>
#include <wincrypt.h>
#ifdef SendMessage
#undef SendMessage
#endif
#endif

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <sstream>

using namespace std::chrono_literals;

namespace
{
constexpr const char *CHANNELS[] = {"international", "english", "russian", "chinese", "french"};
constexpr const char *CHANNEL_LABELS[] = {"International", "English", "Russian", "Chinese", "French"};
constexpr int IRC_MEDIA_MAX_RESPONSE_SIZE = 16 * 1024 * 1024;
constexpr int IRC_MEDIA_MAX_ANIMATION_MS = 10000;
constexpr float BESTGRAM_ROUNDING = 6.0f;

ColorRGBA IrcBg() { return ColorRGBA(0.055f, 0.057f, 0.064f, 1.0f); }
ColorRGBA IrcPanel() { return ColorRGBA(0.120f, 0.124f, 0.140f, 1.0f); }
ColorRGBA IrcPanelDark() { return ColorRGBA(0.075f, 0.078f, 0.088f, 1.0f); }
ColorRGBA IrcPanel2() { return ColorRGBA(0.155f, 0.160f, 0.180f, 1.0f); }
ColorRGBA IrcHover() { return ColorRGBA(0.205f, 0.210f, 0.235f, 1.0f); }
ColorRGBA IrcActive() { return ColorRGBA(0.250f, 0.258f, 0.290f, 1.0f); }
ColorRGBA IrcTextMuted() { return ColorRGBA(0.62f, 0.64f, 0.69f, 1.0f); }
ColorRGBA IrcAccent() { return ColorRGBA(0.345f, 0.395f, 0.965f, 1.0f); }

const char *JsonString(const json_value *pValue)
{
	return pValue && pValue->type == json_string ? pValue->u.string.ptr : "";
}

int64_t JsonInt64(const json_value *pValue)
{
	if(!pValue)
		return 0;
	if(pValue->type == json_integer)
		return pValue->u.integer;
	if(pValue->type == json_string)
		return str_toint(pValue->u.string.ptr);
	return 0;
}

bool JsonBoolValue(const json_value *pValue)
{
	return pValue && pValue->type == json_boolean && pValue->u.boolean != 0;
}

const json_value *Obj(const json_value *pObject, const char *pName)
{
	return pObject && pObject->type == json_object ? json_object_get(pObject, pName) : nullptr;
}

std::string MakeRequestId(const char *pPrefix)
{
	static std::atomic<int> s_Counter{0};
	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "%s-%d", pPrefix, s_Counter.fetch_add(1));
	return aBuf;
}

ColorRGBA HexColor(const std::string &Text, ColorRGBA Fallback)
{
	if(Text.size() != 7 || Text[0] != '#')
		return Fallback;
	char *pEnd = nullptr;
	const long Value = std::strtol(Text.c_str() + 1, &pEnd, 16);
	if(!pEnd || *pEnd != '\0')
		return Fallback;
	return ColorRGBA(((Value >> 16) & 0xff) / 255.0f, ((Value >> 8) & 0xff) / 255.0f, (Value & 0xff) / 255.0f, 1.0f);
}

void Label(ITextRender *pTextRender, CUi *pUi, const CUIRect &Rect, const char *pText, float Size, int Align, ColorRGBA Color)
{
	pTextRender->TextColor(Color);
	pUi->DoLabel(&Rect, pText, Size, Align);
	pTextRender->TextColor(pTextRender->DefaultTextColor());
}

bool LooksLikeHtml(const unsigned char *pData, size_t DataSize)
{
	if(pData == nullptr || DataSize == 0)
		return false;
	const size_t ScanSize = std::min(DataSize, (size_t)2048);
	std::string Prefix((const char *)pData, ScanSize);
	std::transform(Prefix.begin(), Prefix.end(), Prefix.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return Prefix.find("<html") != std::string::npos || Prefix.find("<!doctype") != std::string::npos || Prefix.find("<meta") != std::string::npos;
}

bool HasGifSignature(const unsigned char *pData, size_t DataSize)
{
	return DataSize >= 6 && (mem_comp(pData, "GIF87a", 6) == 0 || mem_comp(pData, "GIF89a", 6) == 0);
}

#if !defined(CONF_OPENSSL) && defined(CONF_FAMILY_WINDOWS)
void NetAddrToSockaddrIn(const NETADDR &Addr, sockaddr_in &SockAddr)
{
	mem_zero(&SockAddr, sizeof(SockAddr));
	SockAddr.sin_family = AF_INET;
	SockAddr.sin_port = htons(Addr.port);
	mem_copy(&SockAddr.sin_addr.s_addr, Addr.ip, sizeof(unsigned char) * 4);
}

void NetAddrToSockaddrIn6(const NETADDR &Addr, sockaddr_in6 &SockAddr)
{
	mem_zero(&SockAddr, sizeof(SockAddr));
	SockAddr.sin6_family = AF_INET6;
	SockAddr.sin6_port = htons(Addr.port);
	mem_copy(&SockAddr.sin6_addr.s6_addr, Addr.ip, sizeof(Addr.ip));
}

class CSchannelSocket
{
public:
	~CSchannelSocket()
	{
		Close();
	}

	bool Connect(const char *pHost, int Port, std::string &Error, std::string &Fingerprint)
	{
		NETADDR Addr;
		if(net_host_lookup(pHost, &Addr, NETTYPE_ALL) != 0)
		{
			Error = "Could not resolve BestGram host.";
			return false;
		}
		Addr.port = Port;

		m_Socket = socket((Addr.type & NETTYPE_IPV6) ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if(m_Socket == INVALID_SOCKET)
		{
			Error = "Could not create TLS socket.";
			return false;
		}

		if(Addr.type & NETTYPE_IPV6)
		{
			sockaddr_in6 SockAddr;
			NetAddrToSockaddrIn6(Addr, SockAddr);
			if(connect(m_Socket, (sockaddr *)&SockAddr, sizeof(SockAddr)) != 0)
			{
				Error = "TLS connection failed.";
				return false;
			}
		}
		else
		{
			sockaddr_in SockAddr;
			NetAddrToSockaddrIn(Addr, SockAddr);
			if(connect(m_Socket, (sockaddr *)&SockAddr, sizeof(SockAddr)) != 0)
			{
				Error = "TLS connection failed.";
				return false;
			}
		}

		SCHANNEL_CRED Cred{};
		Cred.dwVersion = SCHANNEL_CRED_VERSION;
		Cred.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_NO_SERVERNAME_CHECK;

		TimeStamp Expiry;
		SECURITY_STATUS Status = AcquireCredentialsHandleA(nullptr, const_cast<char *>(UNISP_NAME_A), SECPKG_CRED_OUTBOUND, nullptr, &Cred, nullptr, nullptr, &m_CredHandle, &Expiry);
		if(Status != SEC_E_OK)
		{
			Error = "Could not acquire TLS credentials.";
			return false;
		}
		m_HasCredHandle = true;

		DWORD Flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
		DWORD OutFlags = 0;
		SecBufferDesc OutBufferDesc;
		SecBuffer OutBuffers[1];
		OutBufferDesc.ulVersion = SECBUFFER_VERSION;
		OutBufferDesc.cBuffers = 1;
		OutBufferDesc.pBuffers = OutBuffers;
		OutBuffers[0].BufferType = SECBUFFER_TOKEN;
		OutBuffers[0].pvBuffer = nullptr;
		OutBuffers[0].cbBuffer = 0;

		Status = InitializeSecurityContextA(&m_CredHandle, nullptr, const_cast<char *>(pHost), Flags, 0, SECURITY_NATIVE_DREP, nullptr, 0, &m_CtxHandle, &OutBufferDesc, &OutFlags, &Expiry);
		if(Status != SEC_I_CONTINUE_NEEDED)
		{
			Error = "TLS handshake failed.";
			return false;
		}
		m_HasCtxHandle = true;
		if(!SendToken(OutBuffers[0]))
		{
			if(OutBuffers[0].pvBuffer)
				FreeContextBuffer(OutBuffers[0].pvBuffer);
			Error = "TLS handshake failed.";
			return false;
		}
		if(OutBuffers[0].pvBuffer)
			FreeContextBuffer(OutBuffers[0].pvBuffer);

		std::vector<unsigned char> vHandshakeData;
		unsigned char aBuffer[8192];
		while(true)
		{
			const int Received = recv(m_Socket, (char *)aBuffer, sizeof(aBuffer), 0);
			if(Received <= 0)
			{
				Error = "TLS handshake failed.";
				return false;
			}
			vHandshakeData.insert(vHandshakeData.end(), aBuffer, aBuffer + Received);

			while(true)
			{
				SecBuffer InBuffers[2];
				SecBufferDesc InBufferDesc;
				InBufferDesc.ulVersion = SECBUFFER_VERSION;
				InBufferDesc.cBuffers = 2;
				InBufferDesc.pBuffers = InBuffers;
				InBuffers[0].BufferType = SECBUFFER_TOKEN;
				InBuffers[0].pvBuffer = vHandshakeData.data();
				InBuffers[0].cbBuffer = (unsigned long)vHandshakeData.size();
				InBuffers[1].BufferType = SECBUFFER_EMPTY;
				InBuffers[1].pvBuffer = nullptr;
				InBuffers[1].cbBuffer = 0;

				OutBuffers[0].BufferType = SECBUFFER_TOKEN;
				OutBuffers[0].pvBuffer = nullptr;
				OutBuffers[0].cbBuffer = 0;
				Status = InitializeSecurityContextA(&m_CredHandle, &m_CtxHandle, const_cast<char *>(pHost), Flags, 0, SECURITY_NATIVE_DREP, &InBufferDesc, 0, nullptr, &OutBufferDesc, &OutFlags, &Expiry);
				if(OutBuffers[0].pvBuffer)
				{
					if(!SendToken(OutBuffers[0]))
					{
						FreeContextBuffer(OutBuffers[0].pvBuffer);
						Error = "TLS handshake failed.";
						return false;
					}
					FreeContextBuffer(OutBuffers[0].pvBuffer);
				}

				if(Status == SEC_E_INCOMPLETE_MESSAGE)
					break;

				if(Status == SEC_E_OK)
				{
					if(InBuffers[1].BufferType == SECBUFFER_EXTRA)
					{
						const size_t ExtraOffset = vHandshakeData.size() - InBuffers[1].cbBuffer;
						m_vEncryptedRead.assign(vHandshakeData.begin() + ExtraOffset, vHandshakeData.end());
					}
					else
					{
						m_vEncryptedRead.clear();
					}

					if(QueryContextAttributes(&m_CtxHandle, SECPKG_ATTR_STREAM_SIZES, &m_StreamSizes) != SEC_E_OK)
					{
						Error = "Could not query TLS stream sizes.";
						return false;
					}

					PCCERT_CONTEXT pCertContext = nullptr;
					if(QueryContextAttributes(&m_CtxHandle, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &pCertContext) != SEC_E_OK || !pCertContext)
					{
						Error = "Server did not present a certificate.";
						return false;
					}
					BYTE aHash[32];
					DWORD HashSize = sizeof(aHash);
					if(!CertGetCertificateContextProperty(pCertContext, CERT_SHA256_HASH_PROP_ID, aHash, &HashSize))
					{
						CertFreeCertificateContext(pCertContext);
						Error = "Could not read server certificate fingerprint.";
						return false;
					}
					CertFreeCertificateContext(pCertContext);
					char aHex[65];
					for(DWORD i = 0; i < HashSize; ++i)
						str_format(aHex + i * 2, sizeof(aHex) - i * 2, "%02x", aHash[i]);
					Fingerprint = aHex;
					return true;
				}

				if(Status != SEC_I_CONTINUE_NEEDED)
				{
					Error = "TLS handshake failed.";
					return false;
				}

				if(InBuffers[1].BufferType == SECBUFFER_EXTRA)
				{
					const size_t ExtraOffset = vHandshakeData.size() - InBuffers[1].cbBuffer;
					std::vector<unsigned char> vExtra(vHandshakeData.begin() + ExtraOffset, vHandshakeData.end());
					vHandshakeData.swap(vExtra);
				}
				else
				{
					vHandshakeData.clear();
					break;
				}
			}
		}
	}

	bool SendPlain(const char *pData, int Size)
	{
		if(!m_HasCtxHandle)
			return false;

		std::vector<unsigned char> vPacket(m_StreamSizes.cbHeader + Size + m_StreamSizes.cbTrailer);
		mem_copy(vPacket.data() + m_StreamSizes.cbHeader, pData, Size);

		SecBuffer Buffers[4];
		SecBufferDesc BufferDesc;
		BufferDesc.ulVersion = SECBUFFER_VERSION;
		BufferDesc.cBuffers = 4;
		BufferDesc.pBuffers = Buffers;
		Buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
		Buffers[0].pvBuffer = vPacket.data();
		Buffers[0].cbBuffer = m_StreamSizes.cbHeader;
		Buffers[1].BufferType = SECBUFFER_DATA;
		Buffers[1].pvBuffer = vPacket.data() + m_StreamSizes.cbHeader;
		Buffers[1].cbBuffer = Size;
		Buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
		Buffers[2].pvBuffer = vPacket.data() + m_StreamSizes.cbHeader + Size;
		Buffers[2].cbBuffer = m_StreamSizes.cbTrailer;
		Buffers[3].BufferType = SECBUFFER_EMPTY;
		Buffers[3].pvBuffer = nullptr;
		Buffers[3].cbBuffer = 0;

		if(EncryptMessage(&m_CtxHandle, 0, &BufferDesc, 0) != SEC_E_OK)
			return false;

		const int Total = (int)(Buffers[0].cbBuffer + Buffers[1].cbBuffer + Buffers[2].cbBuffer);
		int Sent = 0;
		while(Sent < Total)
		{
			const int Result = send(m_Socket, (const char *)vPacket.data() + Sent, Total - Sent, 0);
			if(Result <= 0)
				return false;
			Sent += Result;
		}
		return true;
	}

	bool PollRead(std::string &Out)
	{
		fd_set ReadSet;
		FD_ZERO(&ReadSet);
		FD_SET(m_Socket, &ReadSet);
		timeval Timeout{};
		if(select(0, &ReadSet, nullptr, nullptr, &Timeout) <= 0)
			return true;

		unsigned char aBuffer[8192];
		const int Received = recv(m_Socket, (char *)aBuffer, sizeof(aBuffer), 0);
		if(Received == 0)
			return false;
		if(Received < 0)
			return true;
		m_vEncryptedRead.insert(m_vEncryptedRead.end(), aBuffer, aBuffer + Received);

		while(!m_vEncryptedRead.empty())
		{
			SecBuffer Buffers[4];
			SecBufferDesc BufferDesc;
			BufferDesc.ulVersion = SECBUFFER_VERSION;
			BufferDesc.cBuffers = 4;
			BufferDesc.pBuffers = Buffers;
			Buffers[0].BufferType = SECBUFFER_DATA;
			Buffers[0].pvBuffer = m_vEncryptedRead.data();
			Buffers[0].cbBuffer = (unsigned long)m_vEncryptedRead.size();
			Buffers[1].BufferType = SECBUFFER_EMPTY;
			Buffers[1].pvBuffer = nullptr;
			Buffers[1].cbBuffer = 0;
			Buffers[2].BufferType = SECBUFFER_EMPTY;
			Buffers[2].pvBuffer = nullptr;
			Buffers[2].cbBuffer = 0;
			Buffers[3].BufferType = SECBUFFER_EMPTY;
			Buffers[3].pvBuffer = nullptr;
			Buffers[3].cbBuffer = 0;

			const SECURITY_STATUS Status = DecryptMessage(&m_CtxHandle, &BufferDesc, 0, nullptr);
			if(Status == SEC_E_INCOMPLETE_MESSAGE)
				break;
			if(Status == SEC_I_CONTEXT_EXPIRED)
				return false;
			if(Status != SEC_E_OK && Status != SEC_I_RENEGOTIATE)
				return false;

			for(const SecBuffer &Buffer : Buffers)
			{
				if(Buffer.BufferType == SECBUFFER_DATA && Buffer.cbBuffer > 0)
					Out.append((const char *)Buffer.pvBuffer, Buffer.cbBuffer);
			}

			bool HasExtra = false;
			for(const SecBuffer &Buffer : Buffers)
			{
				if(Buffer.BufferType == SECBUFFER_EXTRA)
				{
					const size_t ExtraOffset = m_vEncryptedRead.size() - Buffer.cbBuffer;
					std::vector<unsigned char> vExtra(m_vEncryptedRead.begin() + ExtraOffset, m_vEncryptedRead.end());
					m_vEncryptedRead.swap(vExtra);
					HasExtra = true;
					break;
				}
			}
			if(!HasExtra)
				m_vEncryptedRead.clear();
			if(Status == SEC_I_RENEGOTIATE)
				return false;
		}
		return true;
	}

	void Close()
	{
		if(m_HasCtxHandle)
		{
			DeleteSecurityContext(&m_CtxHandle);
			m_HasCtxHandle = false;
		}
		if(m_HasCredHandle)
		{
			FreeCredentialsHandle(&m_CredHandle);
			m_HasCredHandle = false;
		}
		if(m_Socket != INVALID_SOCKET)
		{
			closesocket(m_Socket);
			m_Socket = INVALID_SOCKET;
		}
		m_vEncryptedRead.clear();
	}

private:
	bool SendToken(const SecBuffer &Buffer)
	{
		const char *pData = (const char *)Buffer.pvBuffer;
		int Left = (int)Buffer.cbBuffer;
		while(Left > 0)
		{
			const int Result = send(m_Socket, pData, Left, 0);
			if(Result <= 0)
				return false;
			pData += Result;
			Left -= Result;
		}
		return true;
	}

	SOCKET m_Socket = INVALID_SOCKET;
	CredHandle m_CredHandle{};
	CtxtHandle m_CtxHandle{};
	bool m_HasCredHandle = false;
	bool m_HasCtxHandle = false;
	SecPkgContext_StreamSizes m_StreamSizes{};
	std::vector<unsigned char> m_vEncryptedRead;
};
#endif
}

void CIrcChat::OnInit()
{
	m_PasswordInput.SetHidden(true);
	m_PasswordRepeatInput.SetHidden(true);
	m_NewPasswordInput.SetHidden(true);
	m_LoginInput.SetEmptyText("login");
	m_PasswordInput.SetEmptyText("password");
	m_PasswordRepeatInput.SetEmptyText("repeat password");
	m_MessageInput.SetEmptyText("Message");
	m_SearchInput.SetEmptyText("Search online users");
	m_DescriptionInput.SetEmptyText("About me");
	m_BannerColorInput.SetEmptyText("#5865f2");
	m_NewPasswordInput.SetEmptyText("new password");
	m_BannerColorInput.Set("#5865f2");
	if(g_Config.m_BcIrcAutoconnect)
		StartConnection();
}

void CIrcChat::OnShutdown()
{
	StopConnection();
	ResetMediaPreviews();
}

void CIrcChat::OnRender()
{
	DrainEvents();
	UpdateMediaPreviews();
}

bool CIrcChat::OnInput(const IInput::CEvent &Event)
{
	if((Event.m_Flags & IInput::FLAG_PRESS) && (Event.m_Key == KEY_RETURN || Event.m_Key == KEY_KP_ENTER) && m_MessageInput.IsActive() && m_LoggedIn)
	{
		SendMessage();
		return true;
	}
	if(m_LoggedIn && m_View == EView::CHANNEL && (Event.m_Flags & IInput::FLAG_PRESS) && (Event.m_Key == KEY_MOUSE_WHEEL_UP || Event.m_Key == KEY_MOUSE_WHEEL_DOWN) && Ui()->MouseInside(&m_LastChatListRect))
	{
		if(Event.m_Key == KEY_MOUSE_WHEEL_UP)
		{
			m_ChatScrollOffset += 3;
			if(!m_LoadingOlderHistory && !m_HistoryExhausted && m_ChatScrollOffset > maximum(0, (int)CurrentRoomMessages().size() - 18))
				SendHistoryRequest(true);
		}
		else
			m_ChatScrollOffset = maximum(0, m_ChatScrollOffset - 3);
		return true;
	}
	return false;
}

bool CIrcChat::IsOpenInMenus() const
{
	return false;
}

void CIrcChat::RenderPage(CUIRect MainView)
{
	DrainEvents();
	UpdateMediaPreviews();
	MainView.Draw(IrcBg(), IGraphics::CORNER_ALL, BESTGRAM_ROUNDING);

	if(m_State == EConnectionState::DISCONNECTED || m_State == EConnectionState::ERROR)
	{
		CUIRect Top, Button;
		MainView.Margin(8.0f, &MainView);
		MainView.HSplitTop(28.0f, &Top, &MainView);
		Label(TextRender(), Ui(), Top, ConnectionStatusText(), 14.0f, TEXTALIGN_ML, IrcTextMuted());
		Top.VSplitRight(120.0f, nullptr, &Button);
		if(DoIrcButton(&m_ConnectButton, "Connect", Button, false, nullptr, 12.0f))
			StartConnection();
		MainView.HSplitTop(8.0f, nullptr, &MainView);
	}

	if(!m_LoggedIn)
		RenderAuth(MainView);
	else
		RenderShell(MainView);
}

void CIrcChat::StartConnection()
{
	if(m_State == EConnectionState::CONNECTING || m_State == EConnectionState::CONNECTED || m_State == EConnectionState::AUTHENTICATED)
		return;
	StopConnection();
	m_StopThread = false;
	m_State = EConnectionState::CONNECTING;
	m_StatusText = "Connecting...";
	m_ErrorText.clear();
	m_AutoLoginTried = false;
	m_NetworkThread = std::thread(&CIrcChat::NetworkMain, this);
}

void CIrcChat::StopConnection()
{
	m_StopThread = true;
	m_NetCv.notify_all();
	if(m_NetworkThread.joinable())
		m_NetworkThread.join();
	std::lock_guard Lock(m_NetMutex);
	m_vOutgoing.clear();
}

void CIrcChat::NetworkMain()
{
#if defined(CONF_OPENSSL)
	SSL_library_init();
	SSL_load_error_strings();
	const SSL_METHOD *pMethod = TLS_client_method();
	SSL_CTX *pCtx = SSL_CTX_new(pMethod);
	if(!pCtx)
	{
		QueueEvent("{\"type\":\"client.error\",\"message\":\"Could not create TLS context.\"}");
		return;
	}
	SSL_CTX_set_verify(pCtx, SSL_VERIFY_NONE, nullptr);

	char aTarget[320];
	str_format(aTarget, sizeof(aTarget), "%s:%d", g_Config.m_BcIrcHost, g_Config.m_BcIrcPort);
	BIO *pBio = BIO_new_ssl_connect(pCtx);
	if(!pBio)
	{
		SSL_CTX_free(pCtx);
		QueueEvent("{\"type\":\"client.error\",\"message\":\"Could not create TLS BIO.\"}");
		return;
	}
	BIO_set_conn_hostname(pBio, aTarget);
	BIO_set_nbio(pBio, 1);

	while(!m_StopThread && BIO_do_connect(pBio) <= 0)
	{
		if(!BIO_should_retry(pBio))
		{
			BIO_free_all(pBio);
			SSL_CTX_free(pCtx);
			QueueEvent("{\"type\":\"client.error\",\"message\":\"TLS connection failed.\"}");
			return;
		}
		std::this_thread::sleep_for(20ms);
	}

	SSL *pSsl = nullptr;
	BIO_get_ssl(pBio, &pSsl);
	if(!pSsl)
	{
		BIO_free_all(pBio);
		SSL_CTX_free(pCtx);
		QueueEvent("{\"type\":\"client.error\",\"message\":\"TLS handshake failed.\"}");
		return;
	}
	X509 *pCert = SSL_get_peer_certificate(pSsl);
	if(!pCert)
	{
		BIO_free_all(pBio);
		SSL_CTX_free(pCtx);
		QueueEvent("{\"type\":\"client.error\",\"message\":\"Server did not present a certificate.\"}");
		return;
	}
	unsigned char aDigest[SHA256_DIGEST_LENGTH];
	unsigned int DigestSize = 0;
	X509_digest(pCert, EVP_sha256(), aDigest, &DigestSize);
	X509_free(pCert);
	char aFingerprint[SHA256_DIGEST_LENGTH * 2 + 1];
	for(unsigned int i = 0; i < DigestSize; ++i)
		str_format(aFingerprint + i * 2, sizeof(aFingerprint) - i * 2, "%02x", aDigest[i]);
	QueueEvent(std::string("{\"type\":\"client.tls\",\"fingerprint\":\"") + aFingerprint + "\"}");

	std::string ReadBuffer;
	char aBuf[4096];
	while(!m_StopThread)
	{
		std::vector<std::string> vOutgoing;
		{
			std::lock_guard Lock(m_NetMutex);
			vOutgoing.swap(m_vOutgoing);
		}
		for(const std::string &Line : vOutgoing)
		{
			std::string WithNewline = Line + "\n";
			BIO_write(pBio, WithNewline.data(), (int)WithNewline.size());
		}

		for(;;)
		{
			const int Read = BIO_read(pBio, aBuf, sizeof(aBuf));
			if(Read > 0)
			{
				ReadBuffer.append(aBuf, Read);
				size_t Pos = 0;
				while((Pos = ReadBuffer.find('\n')) != std::string::npos)
				{
					std::string Line = ReadBuffer.substr(0, Pos);
					ReadBuffer.erase(0, Pos + 1);
					if(!Line.empty() && Line.back() == '\r')
						Line.pop_back();
					QueueEvent(Line);
				}
				continue;
			}
			if(Read <= 0 && !BIO_should_retry(pBio))
			{
				BIO_free_all(pBio);
				SSL_CTX_free(pCtx);
				QueueEvent("{\"type\":\"client.error\",\"message\":\"BestGram connection closed.\"}");
				return;
			}
			break;
		}
		std::unique_lock Lock(m_NetMutex);
		m_NetCv.wait_for(Lock, 20ms, [&]() { return m_StopThread.load() || !m_vOutgoing.empty(); });
	}
	BIO_free_all(pBio);
	SSL_CTX_free(pCtx);
#elif defined(CONF_FAMILY_WINDOWS)
	CSchannelSocket Socket;
	std::string Error;
	std::string Fingerprint;
	if(!Socket.Connect(g_Config.m_BcIrcHost, g_Config.m_BcIrcPort, Error, Fingerprint))
	{
		QueueEvent(std::string("{\"type\":\"client.error\",\"message\":\"") + JsonEscape(Error.c_str()) + "\"}");
		return;
	}
	QueueEvent(std::string("{\"type\":\"client.tls\",\"fingerprint\":\"") + Fingerprint + "\"}");

	std::string ReadBuffer;
	while(!m_StopThread)
	{
		std::vector<std::string> vOutgoing;
		{
			std::lock_guard Lock(m_NetMutex);
			vOutgoing.swap(m_vOutgoing);
		}
		for(const std::string &Line : vOutgoing)
		{
			std::string WithNewline = Line + "\n";
			if(!Socket.SendPlain(WithNewline.data(), (int)WithNewline.size()))
			{
				QueueEvent("{\"type\":\"client.error\",\"message\":\"Failed to send BestGram data.\"}");
				return;
			}
		}

		if(!Socket.PollRead(ReadBuffer))
		{
			QueueEvent("{\"type\":\"client.error\",\"message\":\"BestGram connection closed.\"}");
			return;
		}

		size_t Pos = 0;
		while((Pos = ReadBuffer.find('\n')) != std::string::npos)
		{
			std::string Line = ReadBuffer.substr(0, Pos);
			ReadBuffer.erase(0, Pos + 1);
			if(!Line.empty() && Line.back() == '\r')
				Line.pop_back();
			QueueEvent(Line);
		}

		std::unique_lock Lock(m_NetMutex);
		m_NetCv.wait_for(Lock, 20ms, [&]() { return m_StopThread.load() || !m_vOutgoing.empty(); });
	}
#else
	QueueEvent("{\"type\":\"client.error\",\"message\":\"BestGram server requires TLS, but this build has no TLS backend on this platform.\"}");
	return;
#endif
}

void CIrcChat::QueueJson(const std::string &Json)
{
	std::lock_guard Lock(m_NetMutex);
	m_vOutgoing.push_back(Json);
	m_NetCv.notify_all();
}

void CIrcChat::QueueEvent(const std::string &Line)
{
	std::lock_guard Lock(m_NetMutex);
	m_vEvents.push_back({Line});
}

void CIrcChat::DrainEvents()
{
	std::vector<SNetEvent> vEvents;
	{
		std::lock_guard Lock(m_NetMutex);
		vEvents.swap(m_vEvents);
	}
	for(const SNetEvent &Event : vEvents)
		HandleServerLine(Event.m_Line.c_str());
}

void CIrcChat::HandleServerLine(const char *pLine)
{
	json_settings Settings{};
	char aError[256];
	json_value *pJson = json_parse_ex(&Settings, (json_char *)pLine, str_length(pLine), aError);
	if(!pJson || pJson->type != json_object)
	{
		if(pJson)
			json_value_free(pJson);
		return;
	}
	const std::string Type = JsonString(Obj(pJson, "type"));
	if(Type == "client.error")
	{
		m_State = EConnectionState::ERROR;
		m_ErrorText = JsonString(Obj(pJson, "message"));
		m_StatusText = m_ErrorText;
		m_LoggedIn = false;
	}
	else if(Type == "client.tls")
	{
		const char *pFingerprint = JsonString(Obj(pJson, "fingerprint"));
		if(g_Config.m_BcIrcTlsFingerprint[0] == '\0')
		{
			str_copy(g_Config.m_BcIrcTlsFingerprint, pFingerprint, sizeof(g_Config.m_BcIrcTlsFingerprint));
		}
		else if(str_comp(g_Config.m_BcIrcTlsFingerprint, pFingerprint) != 0)
		{
			m_State = EConnectionState::ERROR;
			m_ErrorText = "TLS fingerprint changed. Reset it in BestGram settings if this is expected.";
			m_StatusText = m_ErrorText;
			StopConnection();
		}
	}
	else if(Type == "hello")
	{
		m_State = EConnectionState::CONNECTED;
		m_StatusText = "Connected";
		if(g_Config.m_BcIrcAutologin && g_Config.m_BcIrcSessionToken[0] != '\0' && !m_AutoLoginTried)
			SendResume();
	}
	else if(Type == "auth.ok")
	{
		m_State = EConnectionState::AUTHENTICATED;
		m_LoggedIn = true;
		const char *pToken = JsonString(Obj(pJson, "session_token"));
		if(pToken[0] != '\0')
			str_copy(g_Config.m_BcIrcSessionToken, pToken, sizeof(g_Config.m_BcIrcSessionToken));
		const json_value *pUser = Obj(pJson, "user");
		if(pUser)
		{
			SUser &User = EnsureUser(JsonInt64(Obj(pUser, "id")));
			User.m_Login = JsonString(Obj(pUser, "login"));
			User.m_Display = JsonString(Obj(pUser, "display"));
			User.m_Status = JsonString(Obj(pUser, "status"));
			User.m_Description = JsonString(Obj(pUser, "description"));
			User.m_BannerColor = JsonString(Obj(pUser, "banner_color"));
			User.m_Skin = JsonString(Obj(pUser, "skin"));
			User.m_Online = JsonBoolValue(Obj(pUser, "online"));
			m_LocalUserId = User.m_Id;
			m_LoginInput.Set(User.m_Login.c_str());
			m_DescriptionInput.Set(User.m_Description.c_str());
			m_BannerColorInput.Set(User.m_BannerColor.c_str());
			m_PasswordInput.Clear();
			m_PasswordRepeatInput.Clear();
		}
		m_StatusText = "Logged in";
		m_ChatScrollOffset = 0;
		m_HistoryExhausted = false;
		SendJoinChannel(m_CurrentChannel.c_str());
		SendFriendList();
		SendProfileUpdate();
	}
	else if(Type == "error")
	{
		m_ErrorText = JsonString(Obj(pJson, "message"));
		m_StatusText = m_ErrorText;
	}
	else if(Type == "auth.login.change.ok")
	{
		const json_value *pUser = Obj(pJson, "user");
		const int64_t Id = JsonInt64(Obj(pUser, "id"));
		if(Id > 0)
		{
			SUser &User = EnsureUser(Id);
			User.m_Login = JsonString(Obj(pUser, "login"));
			User.m_Display = JsonString(Obj(pUser, "display"));
			User.m_Status = JsonString(Obj(pUser, "status"));
			User.m_Description = JsonString(Obj(pUser, "description"));
			User.m_BannerColor = JsonString(Obj(pUser, "banner_color"));
			User.m_Skin = JsonString(Obj(pUser, "skin"));
			User.m_Online = JsonBoolValue(Obj(pUser, "online"));
			m_LoginInput.Set(User.m_Login.c_str());
		}
		m_PasswordInput.Clear();
		m_StatusText = "Login updated";
	}
	else if(Type == "auth.password.change.ok")
	{
		m_PasswordInput.Clear();
		m_NewPasswordInput.Clear();
		m_StatusText = "Password updated";
	}
	else if(Type == "friend.request.new" || Type == "friend.update")
	{
		const json_value *pUser = Obj(pJson, "user");
		const int64_t Id = JsonInt64(Obj(pUser, "id"));
		if(Id > 0)
		{
			SUser &User = EnsureUser(Id);
			User.m_Login = JsonString(Obj(pUser, "login"));
			User.m_Display = JsonString(Obj(pUser, "display"));
			User.m_Status = JsonString(Obj(pUser, "status"));
			User.m_Online = JsonBoolValue(Obj(pUser, "online"));
		}
		SendFriendList();
	}
	else if(Type == "history")
	{
		const bool WasLoadingOlder = m_LoadingOlderHistory;
		const std::string RoomType = JsonString(Obj(pJson, "room_type"));
		const std::string RoomKey = JsonString(Obj(pJson, "room_key"));
		std::vector<SMessage> vIncoming;
		const json_value *pMessages = Obj(pJson, "messages");
		if(pMessages && pMessages->type == json_array)
		{
			for(unsigned i = 0; i < pMessages->u.array.length; ++i)
			{
				const json_value *pMsg = pMessages->u.array.values[i];
				if(!pMsg || pMsg->type != json_object)
					continue;
				SMessage Msg;
				Msg.m_Id = JsonInt64(Obj(pMsg, "id"));
				Msg.m_RoomType = RoomType;
				Msg.m_RoomKey = RoomKey;
				Msg.m_Text = JsonString(Obj(pMsg, "text"));
				Msg.m_CreatedAt = JsonInt64(Obj(pMsg, "created_at"));
				const json_value *pSender = Obj(pMsg, "sender");
				Msg.m_SenderId = JsonInt64(Obj(pSender, "id"));
				Msg.m_SenderName = JsonString(Obj(pSender, "display"));
				if(Msg.m_SenderName.empty())
					Msg.m_SenderName = JsonString(Obj(pSender, "login"));
				vIncoming.push_back(Msg);
			}
		}
		const int64_t OldestExisting = [&]() {
			int64_t Oldest = 0;
			for(const SMessage &Msg : m_Messages)
			{
				if(Msg.m_RoomType == RoomType && Msg.m_RoomKey == RoomKey && Msg.m_Id > 0 && (Oldest == 0 || Msg.m_Id < Oldest))
					Oldest = Msg.m_Id;
			}
			return Oldest;
		}();
		const bool OlderPage = OldestExisting > 0 && !vIncoming.empty() && vIncoming.back().m_Id < OldestExisting;
		if(WasLoadingOlder && vIncoming.empty())
		{
			m_HistoryExhausted = true;
		}
		else if(OlderPage)
		{
			for(const SMessage &Incoming : vIncoming)
			{
				const bool Exists = std::any_of(m_Messages.begin(), m_Messages.end(), [&](const SMessage &Msg) { return Msg.m_Id == Incoming.m_Id; });
				if(!Exists)
					m_Messages.push_back(Incoming);
			}
			std::sort(m_Messages.begin(), m_Messages.end(), [](const SMessage &a, const SMessage &b) {
				if(a.m_RoomType != b.m_RoomType)
					return a.m_RoomType < b.m_RoomType;
				if(a.m_RoomKey != b.m_RoomKey)
					return a.m_RoomKey < b.m_RoomKey;
				return a.m_Id < b.m_Id;
			});
		}
		else
		{
			m_Messages.erase(std::remove_if(m_Messages.begin(), m_Messages.end(), [&](const SMessage &Msg) {
				return Msg.m_RoomType == RoomType && Msg.m_RoomKey == RoomKey;
			}), m_Messages.end());
			for(const SMessage &Incoming : vIncoming)
				m_Messages.push_back(Incoming);
			m_HistoryExhausted = vIncoming.empty();
		}
		m_LoadingOlderHistory = false;
	}
	else if(Type == "message.new")
	{
		SMessage Msg;
		Msg.m_Id = JsonInt64(Obj(pJson, "id"));
		Msg.m_RoomType = JsonString(Obj(pJson, "room_type"));
		Msg.m_RoomKey = JsonString(Obj(pJson, "room_key"));
		Msg.m_Text = JsonString(Obj(pJson, "text"));
		Msg.m_CreatedAt = JsonInt64(Obj(pJson, "created_at"));
		const json_value *pSender = Obj(pJson, "sender");
		Msg.m_SenderId = JsonInt64(Obj(pSender, "id"));
		Msg.m_SenderName = JsonString(Obj(pSender, "display"));
		if(Msg.m_SenderName.empty())
			Msg.m_SenderName = JsonString(Obj(pSender, "login"));
		auto Exists = std::any_of(m_Messages.begin(), m_Messages.end(), [&](const SMessage &Other) { return Other.m_Id == Msg.m_Id; });
		if(!Exists)
		{
			m_Messages.push_back(Msg);
			if(m_ChatScrollOffset > 0 && Msg.m_RoomType == CurrentRoomType() && Msg.m_RoomKey == CurrentRoomKey())
				m_ChatScrollOffset++;
		}
	}
	else if(Type == "presence.snapshot")
	{
		m_PresenceOrder.clear();
		const json_value *pUsers = Obj(pJson, "users");
		if(pUsers && pUsers->type == json_array)
		{
			for(unsigned i = 0; i < pUsers->u.array.length; ++i)
			{
				const json_value *pUser = pUsers->u.array.values[i];
				const int64_t Id = JsonInt64(Obj(pUser, "id"));
				if(Id <= 0)
					continue;
				SUser &User = EnsureUser(Id);
				User.m_Login = JsonString(Obj(pUser, "login"));
				User.m_Display = JsonString(Obj(pUser, "display"));
				User.m_Status = JsonString(Obj(pUser, "status"));
				User.m_Online = JsonBoolValue(Obj(pUser, "online"));
				User.m_Description = JsonString(Obj(pUser, "description"));
				User.m_BannerColor = JsonString(Obj(pUser, "banner_color"));
				User.m_Skin = JsonString(Obj(pUser, "skin"));
				m_PresenceOrder.push_back(Id);
			}
		}
	}
	else if(Type == "presence.update")
	{
		const json_value *pUser = Obj(pJson, "user");
		const int64_t Id = JsonInt64(Obj(pUser, "id"));
		if(Id > 0)
		{
			SUser &User = EnsureUser(Id);
			User.m_Login = JsonString(Obj(pUser, "login"));
			User.m_Display = JsonString(Obj(pUser, "display"));
			User.m_Status = JsonString(Obj(pUser, "status"));
			User.m_Online = JsonBoolValue(Obj(pUser, "online"));
			User.m_Description = JsonString(Obj(pUser, "description"));
			User.m_BannerColor = JsonString(Obj(pUser, "banner_color"));
			User.m_Skin = JsonString(Obj(pUser, "skin"));
			if(std::find(m_PresenceOrder.begin(), m_PresenceOrder.end(), Id) == m_PresenceOrder.end())
				m_PresenceOrder.push_back(Id);
		}
	}
	else if(Type == "friend.list")
	{
		auto FillIds = [&](std::vector<int64_t> &vOut, const char *pName) {
			vOut.clear();
			const json_value *pArray = Obj(pJson, pName);
			if(!pArray || pArray->type != json_array)
				return;
			for(unsigned i = 0; i < pArray->u.array.length; ++i)
			{
				const json_value *pUser = pArray->u.array.values[i];
				const int64_t Id = JsonInt64(Obj(pUser, "id"));
				if(Id <= 0)
					continue;
				SUser &User = EnsureUser(Id);
				User.m_Login = JsonString(Obj(pUser, "login"));
				User.m_Display = JsonString(Obj(pUser, "display"));
				User.m_Status = JsonString(Obj(pUser, "status"));
				User.m_Online = JsonBoolValue(Obj(pUser, "online"));
				vOut.push_back(Id);
			}
		};
		FillIds(m_Friends, "friends");
		FillIds(m_PendingIn, "pending_in");
		FillIds(m_PendingOut, "pending_out");
	}
	else if(Type == "user.search.results")
	{
		m_SearchResults.clear();
		const json_value *pUsers = Obj(pJson, "users");
		if(pUsers && pUsers->type == json_array)
		{
			for(unsigned i = 0; i < pUsers->u.array.length; ++i)
			{
				const json_value *pUser = pUsers->u.array.values[i];
				const int64_t Id = JsonInt64(Obj(pUser, "id"));
				if(Id <= 0)
					continue;
				SUser &User = EnsureUser(Id);
				User.m_Login = JsonString(Obj(pUser, "login"));
				User.m_Display = JsonString(Obj(pUser, "display"));
				User.m_Status = JsonString(Obj(pUser, "status"));
				User.m_Online = JsonBoolValue(Obj(pUser, "online"));
				m_SearchResults.push_back(Id);
			}
		}
		m_View = EView::SEARCH;
	}
	else if(Type == "dm.open.ok")
	{
		m_CurrentDmKey = JsonString(Obj(pJson, "room_key"));
		const json_value *pUser = Obj(pJson, "user");
		m_SelectedDmUserId = JsonInt64(Obj(pUser, "id"));
		if(m_SelectedDmUserId > 0)
		{
			SUser &User = EnsureUser(m_SelectedDmUserId);
			User.m_Login = JsonString(Obj(pUser, "login"));
			User.m_Display = JsonString(Obj(pUser, "display"));
		}
		m_View = EView::CHANNEL;
		m_ChatScrollOffset = 0;
		m_LoadingOlderHistory = false;
		m_HistoryExhausted = false;
	}
	if(pJson)
		json_value_free(pJson);
}

void CIrcChat::SendLogin()
{
	QueueJson("{\"type\":\"auth.login\",\"request_id\":\"" + MakeRequestId("login") + "\",\"login\":\"" + JsonEscape(m_LoginInput.GetString()) + "\",\"password\":\"" + JsonEscape(m_PasswordInput.GetString()) + "\"}");
}

void CIrcChat::SendRegister()
{
	if(str_comp(m_PasswordInput.GetString(), m_PasswordRepeatInput.GetString()) != 0)
	{
		m_ErrorText = "Passwords do not match.";
		return;
	}
	QueueJson("{\"type\":\"auth.register\",\"request_id\":\"" + MakeRequestId("register") + "\",\"login\":\"" + JsonEscape(m_LoginInput.GetString()) + "\",\"password\":\"" + JsonEscape(m_PasswordInput.GetString()) + "\"}");
}

void CIrcChat::SendResume()
{
	m_AutoLoginTried = true;
	QueueJson("{\"type\":\"auth.resume\",\"request_id\":\"" + MakeRequestId("resume") + "\",\"token\":\"" + JsonEscape(g_Config.m_BcIrcSessionToken) + "\"}");
}

void CIrcChat::SendJoinChannel(const char *pChannel)
{
	m_CurrentDmKey.clear();
	m_SelectedDmUserId = 0;
	m_CurrentChannel = pChannel;
	m_View = EView::CHANNEL;
	m_ChatScrollOffset = 0;
	m_LoadingOlderHistory = false;
	m_HistoryExhausted = false;
	QueueJson("{\"type\":\"channel.join\",\"request_id\":\"" + MakeRequestId("join") + "\",\"channel\":\"" + JsonEscape(pChannel) + "\"}");
}

void CIrcChat::SendMessage()
{
	if(m_MessageInput.IsEmpty())
		return;
	const std::string Type = CurrentRoomType();
	const std::string Target = Type == "dm" ? std::to_string(m_SelectedDmUserId) : m_CurrentChannel;
	QueueJson("{\"type\":\"message.send\",\"request_id\":\"" + MakeRequestId("msg") + "\",\"target_type\":\"" + Type + "\",\"target\":\"" + JsonEscape(Target.c_str()) + "\",\"text\":\"" + JsonEscape(m_MessageInput.GetString()) + "\"}");
	m_MessageInput.Clear();
	m_ChatScrollOffset = 0;
}

void CIrcChat::SendStatus(const char *pStatus)
{
	QueueJson("{\"type\":\"user.status.update\",\"request_id\":\"" + MakeRequestId("status") + "\",\"status\":\"" + JsonEscape(pStatus) + "\"}");
	m_StatusMenuOpen = false;
}

void CIrcChat::SendSearch()
{
	QueueJson("{\"type\":\"user.search\",\"request_id\":\"" + MakeRequestId("search") + "\",\"query\":\"" + JsonEscape(m_SearchInput.GetString()) + "\"}");
}

void CIrcChat::SendFriendRequest(int64_t UserId)
{
	QueueJson("{\"type\":\"friend.request\",\"request_id\":\"" + MakeRequestId("friend") + "\",\"user_id\":" + std::to_string(UserId) + "}");
}

void CIrcChat::SendFriendAccept(int64_t UserId)
{
	QueueJson("{\"type\":\"friend.accept\",\"request_id\":\"" + MakeRequestId("friend") + "\",\"user_id\":" + std::to_string(UserId) + "}");
}

void CIrcChat::SendFriendRemove(int64_t UserId)
{
	QueueJson("{\"type\":\"friend.remove\",\"request_id\":\"" + MakeRequestId("friend") + "\",\"user_id\":" + std::to_string(UserId) + "}");
}

void CIrcChat::SendDmOpen(int64_t UserId)
{
	QueueJson("{\"type\":\"dm.open\",\"request_id\":\"" + MakeRequestId("dm") + "\",\"user_id\":" + std::to_string(UserId) + "}");
}

void CIrcChat::SendProfileUpdate()
{
	QueueJson("{\"type\":\"user.profile.update\",\"request_id\":\"" + MakeRequestId("profile") + "\",\"description\":\"" + JsonEscape(m_DescriptionInput.GetString()) + "\",\"banner_color\":\"" + JsonEscape(m_BannerColorInput.GetString()) + "\",\"skin\":\"" + JsonEscape(BuildLocalSkinJson().c_str()) + "\"}");
}

void CIrcChat::SendLoginChange()
{
	QueueJson("{\"type\":\"auth.login.change\",\"request_id\":\"" + MakeRequestId("login-change") + "\",\"new_login\":\"" + JsonEscape(m_LoginInput.GetString()) + "\",\"password\":\"" + JsonEscape(m_PasswordInput.GetString()) + "\"}");
}

void CIrcChat::SendPasswordChange()
{
	QueueJson("{\"type\":\"auth.password.change\",\"request_id\":\"" + MakeRequestId("password-change") + "\",\"password\":\"" + JsonEscape(m_PasswordInput.GetString()) + "\",\"new_password\":\"" + JsonEscape(m_NewPasswordInput.GetString()) + "\"}");
}

void CIrcChat::SendFriendList()
{
	QueueJson("{\"type\":\"friend.list\",\"request_id\":\"" + MakeRequestId("friends") + "\"}");
}

void CIrcChat::SendHistoryRequest(bool OlderPage)
{
	if(!m_LoggedIn)
		return;
	const int64_t OldestMessageId = CurrentRoomOldestMessageId();
	if(OlderPage && OldestMessageId <= 0)
	{
		m_HistoryExhausted = true;
		return;
	}
	if(OlderPage)
		m_LoadingOlderHistory = true;
	const std::string RequestId = MakeRequestId(OlderPage ? "history-old" : "history");
	std::string Json;
	if(CurrentRoomType() == "dm")
	{
		Json = "{\"type\":\"dm.history\",\"request_id\":\"" + RequestId + "\",\"user_id\":" + std::to_string(m_SelectedDmUserId) + ",\"limit\":60";
	}
	else
	{
		Json = "{\"type\":\"channel.history\",\"request_id\":\"" + RequestId + "\",\"channel\":\"" + JsonEscape(m_CurrentChannel.c_str()) + "\",\"limit\":60";
	}
	if(OlderPage)
		Json += ",\"before_id\":" + std::to_string(OldestMessageId);
	Json += "}";
	QueueJson(Json);
}

void CIrcChat::RenderAuth(CUIRect View)
{
	CUIRect Box = View;
	Box.Margin(45.0f, &Box);
	Box.Draw(IrcPanel(), IGraphics::CORNER_ALL, 8.0f);
	Box.Margin(18.0f, &Box);
	CUIRect Row;
	Box.HSplitTop(28.0f, &Row, &Box);
	Label(TextRender(), Ui(), Row, m_RegisterMode ? "Create BestGram account" : "Login to BestGram", 20.0f, TEXTALIGN_ML, ColorRGBA(1, 1, 1, 1));
	Box.HSplitTop(8.0f, nullptr, &Box);
	Box.HSplitTop(28.0f, &Row, &Box);
	Ui()->DoEditBox(&m_LoginInput, &Row, 14.0f);
	Box.HSplitTop(8.0f, nullptr, &Box);
	Box.HSplitTop(28.0f, &Row, &Box);
	Ui()->DoEditBox(&m_PasswordInput, &Row, 14.0f);
	if(m_RegisterMode)
	{
		Box.HSplitTop(8.0f, nullptr, &Box);
		Box.HSplitTop(28.0f, &Row, &Box);
		Ui()->DoEditBox(&m_PasswordRepeatInput, &Row, 14.0f);
	}
	Box.HSplitTop(10.0f, nullptr, &Box);
	Box.HSplitTop(28.0f, &Row, &Box);
	CUIRect Left, Right;
	Row.VSplitMid(&Left, &Right, 8.0f);
	if(DoIrcButton(&m_AuthSubmitButton, m_RegisterMode ? "Register" : "Login", Left, false, nullptr, 12.0f))
		m_RegisterMode ? SendRegister() : SendLogin();
	if(DoIrcButton(&m_AuthModeButton, m_RegisterMode ? "Have account" : "Create account", Right, false, nullptr, 12.0f))
		m_RegisterMode = !m_RegisterMode;
	if(!m_ErrorText.empty())
	{
		Box.HSplitTop(12.0f, nullptr, &Box);
		Box.HSplitTop(22.0f, &Row, &Box);
		Label(TextRender(), Ui(), Row, m_ErrorText.c_str(), 12.0f, TEXTALIGN_ML, ColorRGBA(1.0f, 0.35f, 0.35f, 1.0f));
	}
}

void CIrcChat::RenderShell(CUIRect View)
{
	CUIRect Left, Main, Right;
	View.VSplitLeft(248.0f, &Left, &Main);
	Main.VSplitRight(338.0f, &Main, &Right);
	RenderLeftRail(Left);
	if(m_View == EView::SETTINGS)
	{
		RenderSettings(Main);
		RenderSettingsPanel(Right);
	}
	else
	{
		RenderChat(Main);
		RenderRightPanel(Right);
	}
}

void CIrcChat::RenderLeftRail(CUIRect View)
{
	View.Draw(IrcPanelDark(), IGraphics::CORNER_L, BESTGRAM_ROUNDING);
	View.Margin(10.0f, &View);
	CUIRect Row;
	View.HSplitTop(32.0f, &Row, &View);
	Row.Draw(IrcPanel(), IGraphics::CORNER_ALL, BESTGRAM_ROUNDING);
	Row.Margin(8.0f, &Row);
	Label(TextRender(), Ui(), Row, "BestGram", 13.0f, TEXTALIGN_ML, ColorRGBA(0.86f, 0.87f, 0.91f, 1.0f));
	View.HSplitTop(14.0f, nullptr, &View);
	View.HSplitTop(18.0f, &Row, &View);
	Label(TextRender(), Ui(), Row, "GENERAL", 9.0f, TEXTALIGN_ML, IrcTextMuted());
	for(size_t i = 0; i < std::size(CHANNELS); ++i)
	{
		View.HSplitTop(30.0f, &Row, &View);
		const bool Active = m_View == EView::CHANNEL && m_CurrentDmKey.empty() && m_CurrentChannel == CHANNELS[i];
		if(DoIrcButton(&m_ChannelButtons[i], CHANNEL_LABELS[i], Row, Active, FontIcon::HASHTAG))
			SendJoinChannel(CHANNELS[i]);
	}
	View.HSplitTop(8.0f, nullptr, &View);
	View.HSplitTop(18.0f, &Row, &View);
	Label(TextRender(), Ui(), Row, "SOCIAL", 9.0f, TEXTALIGN_ML, IrcTextMuted());
	View.HSplitTop(30.0f, &Row, &View);
	if(DoIrcButton(&m_FriendsButton, "Friends", Row, m_View == EView::FRIENDS, FontIcon::ICON_USERS))
	{
		m_View = EView::FRIENDS;
		SendFriendList();
	}
	View.HSplitTop(30.0f, &Row, &View);
	if(DoIrcButton(&m_SearchButton, "Search", Row, m_View == EView::SEARCH, FontIcon::MAGNIFYING_GLASS))
	{
		m_View = EView::SEARCH;
		SendSearch();
	}
	View.HSplitBottom(116.0f, &View, &Row);
	CUIRect Support, Settings, Profile, Gap;
	Row.HSplitTop(30.0f, &Support, &Row);
	Row.HSplitTop(6.0f, &Gap, &Row);
	Row.HSplitTop(30.0f, &Settings, &Row);
	Row.HSplitTop(8.0f, &Gap, &Row);
	Row.HSplitTop(42.0f, &Profile, &Row);
	CUIRect StatusPopup = Profile;
	StatusPopup.y -= 112.0f;
	StatusPopup.h = 104.0f;
	const bool BlockBottomButtons = m_StatusMenuOpen && Ui()->MouseInside(&StatusPopup);
	if(BlockBottomButtons)
	{
		CUIRect ButtonLabel = Support;
		ButtonLabel.Margin(7.0f, &ButtonLabel);
		CUIRect Icon;
		ButtonLabel.VSplitLeft(18.0f, &Icon, &ButtonLabel);
		Label(TextRender(), Ui(), Icon, FontIcon::QUESTION, 12.0f, TEXTALIGN_ML, IrcTextMuted());
		Label(TextRender(), Ui(), ButtonLabel, "Support", 12.0f, TEXTALIGN_ML, ColorRGBA(0.78f, 0.80f, 0.86f, 1.0f));
	}
	else if(DoIrcButton(&m_SupportButton, "Support", Support, false, FontIcon::QUESTION))
		Client()->ViewLink("https://discord.gg/bestclient");
	if(BlockBottomButtons)
	{
		CUIRect ButtonLabel = Settings;
		if(m_View == EView::SETTINGS)
			ButtonLabel.Draw(IrcActive(), IGraphics::CORNER_ALL, BESTGRAM_ROUNDING);
		ButtonLabel.Margin(7.0f, &ButtonLabel);
		CUIRect Icon;
		ButtonLabel.VSplitLeft(18.0f, &Icon, &ButtonLabel);
		Label(TextRender(), Ui(), Icon, FontIcon::GEAR, 12.0f, TEXTALIGN_ML, m_View == EView::SETTINGS ? ColorRGBA(1, 1, 1, 1) : IrcTextMuted());
		Label(TextRender(), Ui(), ButtonLabel, "Settings", 12.0f, TEXTALIGN_ML, m_View == EView::SETTINGS ? ColorRGBA(1, 1, 1, 1) : ColorRGBA(0.78f, 0.80f, 0.86f, 1.0f));
	}
	else if(DoIrcButton(&m_SettingsButton, "Settings", Settings, m_View == EView::SETTINGS, FontIcon::GEAR))
		m_View = EView::SETTINGS;
	const SUser *pMe = FindUser(m_LocalUserId);
	Profile.Draw(IrcPanel(), IGraphics::CORNER_ALL, BESTGRAM_ROUNDING);
	CUIRect Avatar, Info;
	Profile.Margin(5.0f, &Profile);
	Profile.VSplitLeft(32.0f, &Avatar, &Info);
	RenderAvatar(Avatar, pMe);
	Info.VMargin(8.0f, &Info);
	CUIRect Name, Status;
	Info.HSplitTop(17.0f, &Name, &Status);
	Label(TextRender(), Ui(), Name, pMe ? pMe->m_Display.c_str() : "Profile", 11.0f, TEXTALIGN_ML, ColorRGBA(0.92f, 0.93f, 0.96f, 1.0f));
	CUIRect StatusIcon, StatusText;
	Status.VSplitLeft(12.0f, &StatusIcon, &StatusText);
	RenderStatusIcon(StatusIcon, pMe ? pMe->m_Status.c_str() : "offline", 8.0f);
	Label(TextRender(), Ui(), StatusText, pMe ? pMe->m_Status.c_str() : ConnectionStatusText(), 9.0f, TEXTALIGN_ML, IrcTextMuted());
	if(Ui()->DoButtonLogic(&m_ProfileButton, 0, &Profile, BUTTONFLAG_LEFT, CUi::EButtonSoundType::BUTTON))
		m_StatusMenuOpen = !m_StatusMenuOpen;
	if(m_StatusMenuOpen)
	{
		CUIRect Popup = StatusPopup;
		Popup.Draw(IrcPanel2(), IGraphics::CORNER_ALL, BESTGRAM_ROUNDING);
		Popup.Margin(6.0f, &Popup);
		const char *apLabels[] = {"Online", "Idle", "Do Not Disturb", "Invisible"};
		const char *apValues[] = {"online", "idle", "dnd", "invisible"};
		for(int i = 0; i < 4; ++i)
		{
			Popup.HSplitTop(22.0f, &Row, &Popup);
			const bool Hover = Ui()->MouseInside(&Row);
			if(Hover)
				Row.Draw(IrcHover(), IGraphics::CORNER_ALL, BESTGRAM_ROUNDING);
			CUIRect Icon, LabelRect;
			Row.Margin(5.0f, &LabelRect);
			LabelRect.VSplitLeft(18.0f, &Icon, &LabelRect);
			RenderStatusIcon(Icon, apValues[i], 9.0f);
			Label(TextRender(), Ui(), LabelRect, apLabels[i], 10.0f, TEXTALIGN_ML, ColorRGBA(0.86f, 0.87f, 0.91f, 1.0f));
			if(Ui()->DoButtonLogic(&m_StatusButtons[i], 0, &Row, BUTTONFLAG_LEFT, CUi::EButtonSoundType::BUTTON))
			{
				SendStatus(apValues[i]);
				m_StatusMenuOpen = false;
			}
			Popup.HSplitTop(3.0f, nullptr, &Popup);
		}
	}
}

void CIrcChat::RenderChat(CUIRect View)
{
	View.Draw(IrcBg(), IGraphics::CORNER_NONE, 0.0f);
	CUIRect Header, Input, List;
	View.HSplitTop(44.0f, &Header, &View);
	View.HSplitBottom(48.0f, &List, &Input);
	Header.Draw(IrcPanelDark(), IGraphics::CORNER_NONE, 0.0f);
	Header.Margin(14.0f, &Header);
	const std::string Title = m_CurrentDmKey.empty() ? ("# " + DisplayChannelName(m_CurrentChannel)) : ("DM with " + (FindUser(m_SelectedDmUserId) ? FindUser(m_SelectedDmUserId)->m_Display : std::string("user")));
	Label(TextRender(), Ui(), Header, Title.c_str(), 16.0f, TEXTALIGN_ML, ColorRGBA(0.95f, 0.96f, 0.98f, 1.0f));

	List.Margin(12.0f, &List);
	m_LastChatListRect = List;
	std::vector<SMessage> vMessages = CurrentRoomMessages();
	if(vMessages.empty())
		m_ChatScrollOffset = 0;
	else
		m_ChatScrollOffset = std::clamp(m_ChatScrollOffset, 0, maximum(0, (int)vMessages.size() - 1));
	const bool ShowHistoryInfo = m_ChatScrollOffset > 0 || m_LoadingOlderHistory || m_HistoryExhausted;
	const float AvailableListHeight = maximum(0.0f, List.h - (ShowHistoryInfo ? 24.0f : 0.0f));
	struct SRenderMessage
	{
		int m_Index = 0;
		int64_t m_MediaKey = 0;
		float m_TextHeight = 0.0f;
		float m_MediaHeight = 0.0f;
		float m_RowHeight = 0.0f;
		std::string m_Url;
	};
	std::vector<SRenderMessage> vVisible;
	const float MessageTextWidth = maximum(40.0f, List.w - 54.0f);
	float TotalHeight = 0.0f;
	const int End = (int)vMessages.size() - m_ChatScrollOffset;
	for(int i = End - 1; i >= 0; --i)
	{
		SRenderMessage Entry;
		Entry.m_Index = i;
		Entry.m_TextHeight = maximum(15.0f, WrappedTextHeight(vMessages[i].m_Text.c_str(), 12.0f, MessageTextWidth));
		if(g_Config.m_BcIrcMediaPreview)
		{
			Entry.m_Url = ExtractFirstUrl(vMessages[i].m_Text);
			if(!Entry.m_Url.empty())
			{
				Entry.m_MediaKey = vMessages[i].m_Id > 0 ? vMessages[i].m_Id : i + 1;
				SMediaPreview *pPreview = EnsureMediaPreview(Entry.m_MediaKey, Entry.m_Url);
				Entry.m_MediaHeight = MediaPreviewHeight(pPreview, MessageTextWidth);
			}
		}
		Entry.m_RowHeight = maximum(46.0f, 20.0f + Entry.m_TextHeight + (Entry.m_MediaHeight > 0.0f ? Entry.m_MediaHeight + 8.0f : 0.0f) + 10.0f);
		if(!vVisible.empty() && TotalHeight + Entry.m_RowHeight > AvailableListHeight)
			break;
		TotalHeight += Entry.m_RowHeight;
		vVisible.push_back(Entry);
	}
	std::reverse(vVisible.begin(), vVisible.end());

	if(ShowHistoryInfo)
	{
		CUIRect HistoryInfo;
		List.HSplitTop(24.0f, &HistoryInfo, &List);
		if(m_LoadingOlderHistory || m_HistoryExhausted)
			Label(TextRender(), Ui(), HistoryInfo, m_LoadingOlderHistory ? "Loading history..." : "Start of history", 10.0f, TEXTALIGN_MC, IrcTextMuted());
		else
		{
			CUIRect LoadButton = HistoryInfo;
			LoadButton.VMargin(110.0f, &LoadButton);
			if(DoIrcButton(&m_LoadHistoryButton, "Load older", LoadButton, false, nullptr, 10.0f))
				SendHistoryRequest(true);
		}
	}

	for(size_t VisibleIndex = 0; VisibleIndex < vVisible.size(); ++VisibleIndex)
	{
		const SRenderMessage &Entry = vVisible[VisibleIndex];
		const SMessage &Message = vMessages[Entry.m_Index];
		CUIRect Row, Avatar, Text;
		List.HSplitTop(Entry.m_RowHeight, &Row, &List);
		Row.VSplitLeft(44.0f, &Avatar, &Text);
		Avatar.HSplitTop(34.0f, &Avatar, nullptr);
		RenderAvatar(Avatar, FindUser(Message.m_SenderId));
		CUIRect Name, Body;
		Text.HSplitTop(18.0f, &Name, &Body);
		Label(TextRender(), Ui(), Name, Message.m_SenderName.c_str(), 12.0f, TEXTALIGN_ML, ColorRGBA(0.92f, 0.93f, 0.96f, 1.0f));
		CUIRect MsgText, Preview;
		Body.HSplitTop(Entry.m_TextHeight, &MsgText, &Body);
		RenderWrappedText(MsgText, Message.m_Text.c_str(), 12.0f, ColorRGBA(0.78f, 0.80f, 0.86f, 1.0f));
		if(Entry.m_MediaHeight > 0.0f)
		{
			Body.HSplitTop(8.0f, nullptr, &Body);
			Body.HSplitTop(Entry.m_MediaHeight, &Preview, &Body);
			Preview.VSplitRight(maximum(0.0f, Preview.w - 330.0f), &Preview, nullptr);
			auto PreviewIt = m_MediaPreviews.find(Entry.m_MediaKey);
			RenderMediaPreview(Preview, PreviewIt == m_MediaPreviews.end() ? nullptr : &PreviewIt->second, Entry.m_Url, (int)(VisibleIndex % std::size(m_LinkButtons)));
		}
	}
	CUIRect Edit, Send;
	Input.Draw(IrcPanelDark(), IGraphics::CORNER_NONE, 0.0f);
	Input.Margin(10.0f, &Input);
	Input.VSplitRight(74.0f, &Edit, &Send);
	Edit.Draw(IrcPanel(), IGraphics::CORNER_ALL, 5.0f);
	Edit.Margin(8.0f, &Edit);
	Ui()->DoEditBox(&m_MessageInput, &Edit, 14.0f);
	if(DoIrcButton(&m_SendButton, "Send", Send, false, nullptr, 12.0f))
		SendMessage();
}

void CIrcChat::RenderRightPanel(CUIRect View)
{
	View.Draw(IrcPanelDark(), IGraphics::CORNER_R, 4.0f);
	CUIRect Search, Row;
	View.Margin(10.0f, &View);
	View.HSplitTop(30.0f, &Search, &View);
	Search.Draw(IrcPanel(), IGraphics::CORNER_ALL, 4.0f);
	Search.Margin(6.0f, &Search);
	if(Ui()->DoEditBox_Search(&m_SearchInput, &Search, 12.0f, true))
		SendSearch();
	View.HSplitTop(12.0f, nullptr, &View);

	const SUser *pProfileUser = m_SelectedDmUserId > 0 ? FindUser(m_SelectedDmUserId) : FindUser(m_LocalUserId);
	if(pProfileUser)
	{
		CUIRect Card, Banner, Body, Avatar, Name, About;
		View.HSplitTop(178.0f, &Card, &View);
		Card.Draw(IrcPanel(), IGraphics::CORNER_ALL, 5.0f);
		Card.HSplitTop(54.0f, &Banner, &Body);
		Banner.Draw(HexColor(pProfileUser->m_BannerColor, IrcAccent()), IGraphics::CORNER_T, 5.0f);
		Avatar = Body;
		Avatar.x += 14.0f;
		Avatar.y -= 28.0f;
		Avatar.w = 56.0f;
		Avatar.h = 56.0f;
		RenderAvatar(Avatar, pProfileUser);
		Body.Margin(14.0f, &Body);
		Body.HSplitTop(44.0f, nullptr, &Body);
		Body.HSplitTop(20.0f, &Name, &Body);
		Label(TextRender(), Ui(), Name, pProfileUser->m_Display.c_str(), 15.0f, TEXTALIGN_ML, ColorRGBA(0.96f, 0.97f, 1.0f, 1.0f));
		Body.HSplitTop(18.0f, &Row, &Body);
		CUIRect StatusIcon, StatusText;
		Row.VSplitLeft(14.0f, &StatusIcon, &StatusText);
		RenderStatusIcon(StatusIcon, pProfileUser->m_Online ? pProfileUser->m_Status.c_str() : "offline", 9.0f);
		Label(TextRender(), Ui(), StatusText, pProfileUser->m_Online ? pProfileUser->m_Status.c_str() : "offline", 10.0f, TEXTALIGN_ML, IrcTextMuted());
		Body.HSplitTop(8.0f, nullptr, &Body);
		Body.HSplitTop(46.0f, &About, &Body);
		RenderWrappedText(About, pProfileUser->m_Description.empty() ? "No description yet." : pProfileUser->m_Description.c_str(), 10.0f, ColorRGBA(0.76f, 0.78f, 0.84f, 1.0f));
		View.HSplitTop(14.0f, nullptr, &View);
	}

	if(m_View == EView::FRIENDS)
	{
		Label(TextRender(), Ui(), View, "Friends", 12.0f, TEXTALIGN_TL, ColorRGBA(0.86f, 0.87f, 0.91f, 1.0f));
		View.HSplitTop(24.0f, nullptr, &View);
		for(int64_t Id : m_PendingIn)
			if(const SUser *pUser = FindUser(Id))
			{
				View.HSplitTop(42.0f, &Row, &View);
				RenderUserRow(Row, *pUser, true);
			}
		for(int64_t Id : m_PendingOut)
			if(const SUser *pUser = FindUser(Id))
			{
				View.HSplitTop(42.0f, &Row, &View);
				RenderUserRow(Row, *pUser, true);
			}
		for(int64_t Id : m_Friends)
			if(const SUser *pUser = FindUser(Id))
			{
				View.HSplitTop(42.0f, &Row, &View);
				RenderUserRow(Row, *pUser, true);
			}
	}
	else if(m_View == EView::SEARCH)
	{
		Label(TextRender(), Ui(), View, "Search results", 12.0f, TEXTALIGN_TL, ColorRGBA(0.86f, 0.87f, 0.91f, 1.0f));
		View.HSplitTop(24.0f, nullptr, &View);
		for(int64_t Id : m_SearchResults)
			if(const SUser *pUser = FindUser(Id))
			{
				View.HSplitTop(42.0f, &Row, &View);
				RenderUserRow(Row, *pUser, true);
			}
	}
	else
	{
		Label(TextRender(), Ui(), View, "Online", 12.0f, TEXTALIGN_TL, ColorRGBA(0.86f, 0.87f, 0.91f, 1.0f));
		View.HSplitTop(24.0f, nullptr, &View);
		for(int64_t Id : m_PresenceOrder)
			if(const SUser *pUser = FindUser(Id); pUser && pUser->m_Online)
			{
				View.HSplitTop(38.0f, &Row, &View);
				RenderUserRow(Row, *pUser, false);
			}
	}
}

void CIrcChat::RenderSettings(CUIRect View)
{
	View.Draw(IrcBg(), IGraphics::CORNER_NONE, 0.0f);
	View.Margin(18.0f, &View);
	CUIRect Row;
	View.HSplitTop(28.0f, &Row, &View);
	Label(TextRender(), Ui(), Row, "BestGram Settings", 22.0f, TEXTALIGN_ML, ColorRGBA(1, 1, 1, 1));
	View.HSplitTop(14.0f, nullptr, &View);
	View.HSplitTop(18.0f, &Row, &View);
	Label(TextRender(), Ui(), Row, "Login", 12.0f, TEXTALIGN_ML, IrcTextMuted());
	View.HSplitTop(28.0f, &Row, &View);
	CUIRect LoginEdit, LoginButton;
	Row.VSplitRight(112.0f, &LoginEdit, &LoginButton);
	LoginEdit.VMargin(0.0f, &LoginEdit);
	Ui()->DoEditBox(&m_LoginInput, &LoginEdit, 14.0f);
	if(DoIrcButton(&m_ChangeLoginButton, "Change login", LoginButton, false, nullptr, 11.0f))
		SendLoginChange();
	View.HSplitTop(8.0f, nullptr, &View);
	View.HSplitTop(18.0f, &Row, &View);
	Label(TextRender(), Ui(), Row, "Current password", 12.0f, TEXTALIGN_ML, IrcTextMuted());
	View.HSplitTop(28.0f, &Row, &View);
	Ui()->DoEditBox(&m_PasswordInput, &Row, 14.0f);
	View.HSplitTop(8.0f, nullptr, &View);
	View.HSplitTop(18.0f, &Row, &View);
	Label(TextRender(), Ui(), Row, "New password", 12.0f, TEXTALIGN_ML, IrcTextMuted());
	View.HSplitTop(28.0f, &Row, &View);
	CUIRect PasswordEdit, PasswordButton;
	Row.VSplitRight(132.0f, &PasswordEdit, &PasswordButton);
	Ui()->DoEditBox(&m_NewPasswordInput, &PasswordEdit, 14.0f);
	if(DoIrcButton(&m_ChangePasswordButton, "Change password", PasswordButton, false, nullptr, 11.0f))
		SendPasswordChange();
	View.HSplitTop(16.0f, nullptr, &View);
	View.HSplitTop(18.0f, &Row, &View);
	Label(TextRender(), Ui(), Row, "About me", 12.0f, TEXTALIGN_ML, IrcTextMuted());
	View.HSplitTop(28.0f, &Row, &View);
	Ui()->DoEditBox(&m_DescriptionInput, &Row, 14.0f);
	View.HSplitTop(8.0f, nullptr, &View);
	View.HSplitTop(18.0f, &Row, &View);
	Label(TextRender(), Ui(), Row, "Banner color", 12.0f, TEXTALIGN_ML, IrcTextMuted());
	View.HSplitTop(28.0f, &Row, &View);
	Ui()->DoEditBox(&m_BannerColorInput, &Row, 14.0f);
	View.HSplitTop(8.0f, nullptr, &View);
	View.HSplitTop(28.0f, &Row, &View);
	CUIRect Preview, Save;
	Row.VSplitLeft(90.0f, &Preview, &Save);
	Preview.Draw(HexColor(m_BannerColorInput.GetString(), IrcAccent()), IGraphics::CORNER_ALL, BESTGRAM_ROUNDING);
	if(DoIrcButton(&m_SaveProfileButton, "Save profile", Save, false, nullptr, 11.0f))
		SendProfileUpdate();
	View.HSplitTop(16.0f, nullptr, &View);
	View.HSplitTop(26.0f, &Row, &View);
	if(DoIrcButton(&m_ResetTlsButton, "Reset TLS fingerprint", Row, false, nullptr, 11.0f))
		g_Config.m_BcIrcTlsFingerprint[0] = '\0';
	View.HSplitTop(8.0f, nullptr, &View);
	View.HSplitTop(26.0f, &Row, &View);
	if(DoIrcButton(&m_LogoutButton, "Logout", Row, false, nullptr, 11.0f))
	{
		QueueJson("{\"type\":\"auth.logout\",\"request_id\":\"" + MakeRequestId("logout") + "\"}");
		g_Config.m_BcIrcSessionToken[0] = '\0';
		m_LoggedIn = false;
		m_State = EConnectionState::CONNECTED;
	}
}

void CIrcChat::RenderSettingsPanel(CUIRect View)
{
	View.Draw(IrcPanelDark(), IGraphics::CORNER_R, BESTGRAM_ROUNDING);
	View.Margin(14.0f, &View);
	CUIRect Row;
	View.HSplitTop(24.0f, &Row, &View);
	Label(TextRender(), Ui(), Row, "Status", 15.0f, TEXTALIGN_ML, ColorRGBA(0.96f, 0.97f, 1.0f, 1.0f));
	View.HSplitTop(8.0f, nullptr, &View);

	const SUser *pMe = FindUser(m_LocalUserId);
	const char *apLabels[] = {"Online", "Idle", "Do Not Disturb", "Invisible"};
	const char *apValues[] = {"online", "idle", "dnd", "invisible"};
	for(int i = 0; i < 4; ++i)
	{
		View.HSplitTop(34.0f, &Row, &View);
		const bool Active = pMe && pMe->m_Status == apValues[i];
		if(Active || Ui()->MouseInside(&Row))
			Row.Draw(Active ? IrcActive() : IrcHover(), IGraphics::CORNER_ALL, BESTGRAM_ROUNDING);
		CUIRect Icon, Text;
		Row.Margin(7.0f, &Text);
		Text.VSplitLeft(20.0f, &Icon, &Text);
		RenderStatusIcon(Icon, apValues[i], 11.0f);
		Label(TextRender(), Ui(), Text, apLabels[i], 11.0f, TEXTALIGN_ML, Active ? ColorRGBA(1, 1, 1, 1) : ColorRGBA(0.80f, 0.82f, 0.88f, 1.0f));
		if(Ui()->DoButtonLogic(&m_SettingsStatusButtons[i], Active ? 1 : 0, &Row, BUTTONFLAG_LEFT, CUi::EButtonSoundType::BUTTON))
			SendStatus(apValues[i]);
		View.HSplitTop(4.0f, nullptr, &View);
	}

	View.HSplitTop(18.0f, nullptr, &View);
	View.HSplitTop(24.0f, &Row, &View);
	Label(TextRender(), Ui(), Row, "BestGram", 15.0f, TEXTALIGN_ML, ColorRGBA(0.96f, 0.97f, 1.0f, 1.0f));
	View.HSplitTop(8.0f, nullptr, &View);
	View.HSplitTop(30.0f, &Row, &View);
	if(DoIrcButton(&m_SettingsPanelSupportButton, "Support", Row, false, FontIcon::QUESTION, 11.0f))
		Client()->ViewLink("https://discord.gg/bestclient");
	View.HSplitTop(6.0f, nullptr, &View);
	View.HSplitTop(30.0f, &Row, &View);
	if(DoIrcButton(&m_SettingsPanelResetTlsButton, "Reset TLS pin", Row, false, nullptr, 11.0f))
		g_Config.m_BcIrcTlsFingerprint[0] = '\0';
	View.HSplitTop(6.0f, nullptr, &View);
	View.HSplitTop(30.0f, &Row, &View);
	if(DoIrcButton(&m_SettingsPanelLogoutButton, "Logout", Row, false, nullptr, 11.0f))
	{
		QueueJson("{\"type\":\"auth.logout\",\"request_id\":\"" + MakeRequestId("logout") + "\"}");
		g_Config.m_BcIrcSessionToken[0] = '\0';
		m_LoggedIn = false;
		m_State = EConnectionState::CONNECTED;
	}
}

void CIrcChat::RenderUserRow(CUIRect Row, const SUser &User, bool FriendActions)
{
	Row.Draw(Ui()->MouseInside(&Row) ? IrcHover() : ColorRGBA(0, 0, 0, 0), IGraphics::CORNER_ALL, BESTGRAM_ROUNDING);
	Row.Margin(3.0f, &Row);
	CUIRect Avatar, Text, Button;
	Row.VSplitLeft(32.0f, &Avatar, &Text);
	RenderAvatar(Avatar, &User);
	Text.VSplitRight(FriendActions ? 72.0f : 0.0f, &Text, FriendActions ? &Button : nullptr);
	CUIRect Name, Status;
	Text.HSplitTop(16.0f, &Name, &Status);
	Label(TextRender(), Ui(), Name, User.m_Display.c_str(), 12.0f, TEXTALIGN_ML, ColorRGBA(0.90f, 0.91f, 0.95f, 1.0f));
	CUIRect StatusIcon, StatusText;
	Status.VSplitLeft(12.0f, &StatusIcon, &StatusText);
	RenderStatusIcon(StatusIcon, User.m_Online ? User.m_Status.c_str() : "offline", 8.0f);
	Label(TextRender(), Ui(), StatusText, User.m_Online ? User.m_Status.c_str() : "offline", 10.0f, TEXTALIGN_ML, IrcTextMuted());
	if(FriendActions && User.m_Id != m_LocalUserId)
	{
		CUIRect Left, Right;
		Button.VSplitMid(&Left, &Right, 4.0f);
		const bool PendingIn = std::find(m_PendingIn.begin(), m_PendingIn.end(), User.m_Id) != m_PendingIn.end();
		const bool PendingOut = std::find(m_PendingOut.begin(), m_PendingOut.end(), User.m_Id) != m_PendingOut.end();
		const bool IsFriend = std::find(m_Friends.begin(), m_Friends.end(), User.m_Id) != m_Friends.end();
		const char *pActionLabel = PendingIn ? "OK" : (IsFriend ? "-" : (PendingOut ? "..." : "+"));
		static CButtonContainer s_Action[2];
		if(DoIrcButton(&s_Action[0], pActionLabel, Left, false, nullptr, 10.0f))
		{
			if(PendingIn)
				SendFriendAccept(User.m_Id);
			else if(IsFriend)
				SendFriendRemove(User.m_Id);
			else if(!PendingOut)
				SendFriendRequest(User.m_Id);
		}
		if(IsFriend && DoIrcButton(&s_Action[1], "DM", Right, false, nullptr, 10.0f))
			SendDmOpen(User.m_Id);
	}
}

void CIrcChat::RenderAvatar(const CUIRect &Rect, const SUser *pUser)
{
	Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, pUser ? HexColor(pUser->m_BannerColor, IrcAccent()).WithAlpha(0.55f) : IrcActive(), IGraphics::CORNER_ALL, Rect.w / 2.0f);
	int ClientId = -1;
	if(pUser)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameClient()->m_aClients[i].m_Active && str_comp(GameClient()->m_aClients[i].m_aName, pUser->m_Display.c_str()) == 0)
			{
				ClientId = i;
				break;
			}
		}
	}
	CTeeRenderInfo TeeInfo;
	if(ClientId >= 0 && GameClient()->m_aClients[ClientId].m_RenderInfo.Valid())
		TeeInfo = GameClient()->m_aClients[ClientId].m_RenderInfo;
	else
	{
		TeeInfo.Apply(GameClient()->m_Skins.Find("default"));
		TeeInfo.m_Size = Rect.h * 1.2f;
	}
	TeeInfo.m_Size = Rect.h * 1.15f;
	vec2 OffsetToMid;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &TeeInfo, OffsetToMid);
	RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), Rect.Center() + OffsetToMid);
}

int CIrcChat::DoIrcButton(CButtonContainer *pButton, const char *pLabel, CUIRect Rect, bool Active, const char *pIcon, float FontSize)
{
	const bool Hover = Ui()->MouseInside(&Rect);
	if(Active || Hover)
		Rect.Draw(Active ? IrcActive() : IrcHover(), IGraphics::CORNER_ALL, BESTGRAM_ROUNDING);
	CUIRect LabelRect = Rect;
	LabelRect.Margin(7.0f, &LabelRect);
	if(pIcon && pIcon[0])
	{
		CUIRect IconRect;
		LabelRect.VSplitLeft(18.0f, &IconRect, &LabelRect);
		Label(TextRender(), Ui(), IconRect, pIcon, FontSize, TEXTALIGN_ML, Active ? ColorRGBA(1, 1, 1, 1) : IrcTextMuted());
	}
	Label(TextRender(), Ui(), LabelRect, pLabel, FontSize, TEXTALIGN_ML, Active ? ColorRGBA(1, 1, 1, 1) : ColorRGBA(0.78f, 0.80f, 0.86f, 1.0f));
	return Ui()->DoButtonLogic(pButton, Active ? 1 : 0, &Rect, BUTTONFLAG_LEFT, CUi::EButtonSoundType::BUTTON);
}

void CIrcChat::RenderStatusIcon(CUIRect Rect, const char *pStatus, float Size)
{
	ColorRGBA Color = ColorRGBA(0.50f, 0.53f, 0.60f, 1.0f);
	const char *pIcon = FontIcon::CIRCLE;
	if(str_comp(pStatus, "online") == 0)
		Color = ColorRGBA(0.14f, 0.78f, 0.42f, 1.0f);
	else if(str_comp(pStatus, "idle") == 0)
		Color = ColorRGBA(0.95f, 0.68f, 0.18f, 1.0f);
	else if(str_comp(pStatus, "dnd") == 0)
	{
		Color = ColorRGBA(0.93f, 0.24f, 0.26f, 1.0f);
		Label(TextRender(), Ui(), Rect, FontIcon::CIRCLE, Size, TEXTALIGN_MC, Color);
		Label(TextRender(), Ui(), Rect, FontIcon::MINUS, Size * 0.72f, TEXTALIGN_MC, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
		return;
	}
	else if(str_comp(pStatus, "invisible") == 0 || str_comp(pStatus, "offline") == 0)
		Color = ColorRGBA(0.45f, 0.48f, 0.54f, 1.0f);
	Label(TextRender(), Ui(), Rect, pIcon, Size, TEXTALIGN_MC, Color);
}

void CIrcChat::RenderWrappedText(CUIRect Rect, const char *pText, float FontSize, ColorRGBA Color)
{
	CTextCursor Cursor;
	Cursor.SetPosition(vec2(Rect.x, Rect.y));
	Cursor.m_FontSize = FontSize;
	Cursor.m_LineWidth = Rect.w;
	TextRender()->TextColor(Color);
	TextRender()->TextEx(&Cursor, pText ? pText : "", -1);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

float CIrcChat::WrappedTextHeight(const char *pText, float FontSize, float Width) const
{
	float Height = 0.0f;
	STextSizeProperties Props;
	Props.m_pHeight = &Height;
	TextRender()->TextWidth(FontSize, pText ? pText : "", -1, maximum(1.0f, Width), 0, Props);
	return maximum(FontSize + 2.0f, Height);
}

CIrcChat::SMediaPreview *CIrcChat::EnsureMediaPreview(int64_t MessageId, const std::string &Url)
{
	if(Url.empty())
		return nullptr;
	SMediaPreview &Preview = m_MediaPreviews[MessageId];
	if(Preview.m_Url != Url)
	{
		if(Preview.m_pRequest)
			Preview.m_pRequest->Abort();
		MediaDecoder::UnloadFrames(Graphics(), Preview.m_vFrames);
		Preview = SMediaPreview();
		Preview.m_Url = Url;
		StartMediaPreview(Preview, Url);
	}
	else if(Preview.m_State == EMediaPreviewState::NONE)
		StartMediaPreview(Preview, Url);
	return &Preview;
}

void CIrcChat::StartMediaPreview(SMediaPreview &Preview, const std::string &Url)
{
	if(Url.empty())
		return;
	std::shared_ptr<CHttpRequest> pGet = HttpGet(Url.c_str());
	pGet->Timeout(CTimeout{8000, 0, 4096, 8});
	pGet->MaxResponseSize(IRC_MEDIA_MAX_RESPONSE_SIZE);
	pGet->FailOnErrorStatus(false);
	pGet->LogProgress(HTTPLOG::NONE);
	Preview.m_pRequest = pGet;
	Preview.m_ResolvedUrl = Url;
	Preview.m_State = EMediaPreviewState::LOADING;
	Http()->Run(pGet);
}

void CIrcChat::UpdateMediaPreviews()
{
	for(auto &[Id, Preview] : m_MediaPreviews)
	{
		(void)Id;
		if(Preview.m_State != EMediaPreviewState::LOADING || !Preview.m_pRequest || !Preview.m_pRequest->Done())
			continue;
		std::shared_ptr<CHttpRequest> pRequest = Preview.m_pRequest;
		Preview.m_pRequest = nullptr;
		if(pRequest->State() != EHttpState::DONE || pRequest->StatusCode() < 200 || pRequest->StatusCode() >= 400)
		{
			Preview.m_State = EMediaPreviewState::FAILED;
			continue;
		}
		unsigned char *pResult = nullptr;
		size_t ResultSize = 0;
		pRequest->Result(&pResult, &ResultSize);
		if(!pResult || ResultSize == 0)
		{
			Preview.m_State = EMediaPreviewState::FAILED;
			continue;
		}
		if(LooksLikeHtml(pResult, ResultSize))
		{
			std::string MediaUrl = ExtractMediaUrlFromHtml(pResult, ResultSize);
			if(!MediaUrl.empty() && MediaUrl != Preview.m_ResolvedUrl)
			{
				StartMediaPreview(Preview, MediaUrl);
				continue;
			}
		}
		if(!DecodeMediaPreview(Preview, pResult, ResultSize))
			Preview.m_State = EMediaPreviewState::FAILED;
	}

	if(m_MediaPreviews.size() > 160)
	{
		for(auto It = m_MediaPreviews.begin(); It != m_MediaPreviews.end() && m_MediaPreviews.size() > 128;)
		{
			if(It->second.m_State == EMediaPreviewState::FAILED)
			{
				MediaDecoder::UnloadFrames(Graphics(), It->second.m_vFrames);
				It = m_MediaPreviews.erase(It);
			}
			else
				++It;
		}
	}
}

void CIrcChat::ResetMediaPreviews()
{
	for(auto &[Id, Preview] : m_MediaPreviews)
	{
		(void)Id;
		if(Preview.m_pRequest)
			Preview.m_pRequest->Abort();
		MediaDecoder::UnloadFrames(Graphics(), Preview.m_vFrames);
	}
	m_MediaPreviews.clear();
}

bool CIrcChat::DecodeMediaPreview(SMediaPreview &Preview, const unsigned char *pData, size_t DataSize)
{
	MediaDecoder::UnloadFrames(Graphics(), Preview.m_vFrames);
	Preview.m_Animated = false;
	Preview.m_Width = 0;
	Preview.m_Height = 0;
	Preview.m_AnimationStart = 0;

	const std::string Ext = MediaDecoder::ExtractExtensionLower(Preview.m_ResolvedUrl.c_str());
	bool Success = false;
	if(HasGifSignature(pData, DataSize) || MediaDecoder::IsLikelyAnimatedImageExtension(Ext))
		Success = MediaDecoder::DecodeAnimatedImage(Graphics(), pData, DataSize, Preview.m_ResolvedUrl.c_str(), Preview.m_vFrames, Preview.m_Animated, Preview.m_Width, Preview.m_Height, Preview.m_AnimationStart, IRC_MEDIA_MAX_ANIMATION_MS);
	if(!Success && MediaDecoder::IsLikelyVideoExtension(Ext))
		Success = MediaDecoder::DecodeImageWithFfmpeg(Graphics(), pData, DataSize, Preview.m_ResolvedUrl.c_str(), Preview.m_vFrames, Preview.m_Animated, Preview.m_Width, Preview.m_Height, Preview.m_AnimationStart, true, IRC_MEDIA_MAX_ANIMATION_MS);
	if(!Success)
		Success = MediaDecoder::DecodeStaticImage(Graphics(), pData, DataSize, Preview.m_ResolvedUrl.c_str(), Preview.m_vFrames, Preview.m_Animated, Preview.m_Width, Preview.m_Height, Preview.m_AnimationStart);
	if(!Success)
		Success = MediaDecoder::DecodeAnimatedImage(Graphics(), pData, DataSize, Preview.m_ResolvedUrl.c_str(), Preview.m_vFrames, Preview.m_Animated, Preview.m_Width, Preview.m_Height, Preview.m_AnimationStart, IRC_MEDIA_MAX_ANIMATION_MS);
	if(!Success)
		Success = MediaDecoder::DecodeImageWithFfmpeg(Graphics(), pData, DataSize, Preview.m_ResolvedUrl.c_str(), Preview.m_vFrames, Preview.m_Animated, Preview.m_Width, Preview.m_Height, Preview.m_AnimationStart, true, IRC_MEDIA_MAX_ANIMATION_MS);
	Preview.m_State = Success && !Preview.m_vFrames.empty() ? EMediaPreviewState::READY : EMediaPreviewState::FAILED;
	return Preview.m_State == EMediaPreviewState::READY;
}

float CIrcChat::MediaPreviewHeight(const SMediaPreview *pPreview, float MaxWidth) const
{
	if(pPreview == nullptr)
		return 0.0f;
	if(pPreview->m_State == EMediaPreviewState::READY && pPreview->m_Width > 0 && pPreview->m_Height > 0)
	{
		const float MaxW = minimum(MaxWidth, 330.0f);
		const float MaxH = 190.0f;
		const float Scale = minimum(MaxW / (float)pPreview->m_Width, MaxH / (float)pPreview->m_Height);
		return std::clamp((float)pPreview->m_Height * Scale, 44.0f, MaxH);
	}
	return pPreview->m_State == EMediaPreviewState::FAILED ? 34.0f : 46.0f;
}

void CIrcChat::RenderMediaPreview(CUIRect Rect, SMediaPreview *pPreview, const std::string &Url, int ButtonIndex)
{
	Rect.Draw(IrcPanel(), IGraphics::CORNER_ALL, 5.0f);
	Rect.Margin(4.0f, &Rect);
	if(pPreview && pPreview->m_State == EMediaPreviewState::READY)
	{
		IGraphics::CTextureHandle Texture;
		if(MediaDecoder::GetCurrentFrameTexture(pPreview->m_vFrames, pPreview->m_Animated, pPreview->m_AnimationStart, Texture))
		{
			float W = Rect.w;
			float H = Rect.h;
			if(pPreview->m_Width > 0 && pPreview->m_Height > 0)
			{
				const float Scale = minimum(Rect.w / (float)pPreview->m_Width, Rect.h / (float)pPreview->m_Height);
				W = (float)pPreview->m_Width * Scale;
				H = (float)pPreview->m_Height * Scale;
			}
			const float X = Rect.x + (Rect.w - W) * 0.5f;
			const float Y = Rect.y + (Rect.h - H) * 0.5f;
			Graphics()->TextureSet(Texture);
			Graphics()->QuadsBegin();
			IGraphics::CQuadItem Quad(X, Y, W, H);
			Graphics()->QuadsDrawTL(&Quad, 1);
			Graphics()->QuadsEnd();
			Graphics()->TextureClear();
		}
	}
	else
	{
		const char *pText = pPreview && pPreview->m_State == EMediaPreviewState::FAILED ? "Media preview unavailable" : "Loading media...";
		Label(TextRender(), Ui(), Rect, pText, 11.0f, TEXTALIGN_ML, pPreview && pPreview->m_State == EMediaPreviewState::FAILED ? ColorRGBA(1.0f, 0.48f, 0.48f, 1.0f) : IrcTextMuted());
	}
	if(ButtonIndex >= 0 && ButtonIndex < (int)std::size(m_LinkButtons) && Ui()->DoButtonLogic(&m_LinkButtons[ButtonIndex], 0, &Rect, BUTTONFLAG_LEFT, CUi::EButtonSoundType::BUTTON))
		Client()->ViewLink(Url.c_str());
}

const char *CIrcChat::ConnectionStatusText() const
{
	switch(m_State)
	{
	case EConnectionState::DISCONNECTED: return "Disconnected";
	case EConnectionState::CONNECTING: return "Connecting...";
	case EConnectionState::CONNECTED: return "Connected";
	case EConnectionState::AUTHENTICATED: return "Connected";
	case EConnectionState::ERROR: return m_ErrorText.empty() ? "Connection error" : m_ErrorText.c_str();
	}
	return "";
}

const CIrcChat::SUser *CIrcChat::FindUser(int64_t UserId) const
{
	auto It = m_Users.find(UserId);
	return It == m_Users.end() ? nullptr : &It->second;
}

CIrcChat::SUser &CIrcChat::EnsureUser(int64_t UserId)
{
	SUser &User = m_Users[UserId];
	User.m_Id = UserId;
	if(User.m_Display.empty())
		User.m_Display = "user";
	return User;
}

std::vector<CIrcChat::SMessage> CIrcChat::CurrentRoomMessages() const
{
	const std::string Type = CurrentRoomType();
	const std::string Key = CurrentRoomKey();
	std::vector<SMessage> vResult;
	for(const SMessage &Msg : m_Messages)
		if(Msg.m_RoomType == Type && Msg.m_RoomKey == Key)
			vResult.push_back(Msg);
	return vResult;
}

int64_t CIrcChat::CurrentRoomOldestMessageId() const
{
	const std::string Type = CurrentRoomType();
	const std::string Key = CurrentRoomKey();
	int64_t Oldest = 0;
	for(const SMessage &Msg : m_Messages)
	{
		if(Msg.m_RoomType == Type && Msg.m_RoomKey == Key && Msg.m_Id > 0 && (Oldest == 0 || Msg.m_Id < Oldest))
			Oldest = Msg.m_Id;
	}
	return Oldest;
}

std::string CIrcChat::CurrentRoomType() const
{
	return m_CurrentDmKey.empty() ? "channel" : "dm";
}

std::string CIrcChat::CurrentRoomKey() const
{
	return m_CurrentDmKey.empty() ? m_CurrentChannel : m_CurrentDmKey;
}

std::string CIrcChat::BuildLocalSkinJson() const
{
	const char *pSkin = g_Config.m_ClDummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin;
	const int UseCustom = g_Config.m_ClDummy ? g_Config.m_ClDummyUseCustomColor : g_Config.m_ClPlayerUseCustomColor;
	const int Body = g_Config.m_ClDummy ? g_Config.m_ClDummyColorBody : g_Config.m_ClPlayerColorBody;
	const int Feet = g_Config.m_ClDummy ? g_Config.m_ClDummyColorFeet : g_Config.m_ClPlayerColorFeet;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "{\"skin\":\"%s\",\"custom\":%d,\"body\":%d,\"feet\":%d}", pSkin, UseCustom, Body, Feet);
	return aBuf;
}

std::string CIrcChat::JsonEscape(const char *pString)
{
	if(!pString)
		return "";
	std::string Out;
	for(const unsigned char *p = (const unsigned char *)pString; *p; ++p)
	{
		switch(*p)
		{
		case '\\': Out += "\\\\"; break;
		case '"': Out += "\\\""; break;
		case '\n': Out += "\\n"; break;
		case '\r': Out += "\\r"; break;
		case '\t': Out += "\\t"; break;
		default:
			if(*p < 0x20)
			{
				char aBuf[8];
				str_format(aBuf, sizeof(aBuf), "\\u%04x", *p);
				Out += aBuf;
			}
			else
				Out.push_back((char)*p);
			break;
		}
	}
	return Out;
}

std::string CIrcChat::Lower(std::string Value)
{
	for(char &c : Value)
		c = (char)std::tolower((unsigned char)c);
	return Value;
}

std::string CIrcChat::DisplayChannelName(const std::string &Channel)
{
	for(size_t i = 0; i < std::size(CHANNELS); ++i)
		if(Channel == CHANNELS[i])
			return CHANNEL_LABELS[i];
	return Channel;
}

std::string CIrcChat::ExtractFirstUrl(const std::string &Text)
{
	const size_t PosHttp = Text.find("http://");
	const size_t PosHttps = Text.find("https://");
	size_t Pos = std::string::npos;
	if(PosHttp != std::string::npos && PosHttps != std::string::npos)
		Pos = std::min(PosHttp, PosHttps);
	else
		Pos = PosHttp != std::string::npos ? PosHttp : PosHttps;
	if(Pos == std::string::npos)
		return {};
	size_t End = Pos;
	while(End < Text.size() && !std::isspace((unsigned char)Text[End]))
		++End;
	std::string Url = Text.substr(Pos, End - Pos);
	while(!Url.empty() && (Url.back() == '.' || Url.back() == ',' || Url.back() == ')' || Url.back() == ']' || Url.back() == '}'))
		Url.pop_back();
	return Url;
}

std::string CIrcChat::ExtractMediaUrlFromHtml(const unsigned char *pData, size_t DataSize)
{
	if(pData == nullptr || DataSize == 0)
		return {};
	std::string Html((const char *)pData, std::min(DataSize, (size_t)768 * 1024));
	auto ReplaceAll = [](std::string &Text, const char *pFrom, const char *pTo) {
		const size_t FromLen = str_length(pFrom);
		const size_t ToLen = str_length(pTo);
		for(size_t Pos = Text.find(pFrom); Pos != std::string::npos; Pos = Text.find(pFrom, Pos + ToLen))
			Text.replace(Pos, FromLen, pTo);
	};
	ReplaceAll(Html, "\\/", "/");
	ReplaceAll(Html, "\\u002F", "/");
	ReplaceAll(Html, "\\u002f", "/");
	ReplaceAll(Html, "\\u0026", "&");
	ReplaceAll(Html, "&amp;", "&");

	const char *apPrefixes[] = {
		"https://media.tenor.com/",
		"https://c.tenor.com/",
		"https://media.giphy.com/media/",
		"https://i.giphy.com/media/",
		"https://i.imgur.com/",
	};
	std::string Best;
	for(const char *pPrefix : apPrefixes)
	{
		size_t Pos = 0;
		while((Pos = Html.find(pPrefix, Pos)) != std::string::npos)
		{
			size_t End = Pos;
			while(End < Html.size())
			{
				const char c = Html[End];
				if(c == '"' || c == '\'' || c == '<' || c == '>' || c == ' ' || c == '\n' || c == '\r' || c == '\t')
					break;
				++End;
			}
			std::string Candidate = Html.substr(Pos, End - Pos);
			while(!Candidate.empty() && (Candidate.back() == ',' || Candidate.back() == ')' || Candidate.back() == ']' || Candidate.back() == '}'))
				Candidate.pop_back();
			const std::string LowerCandidate = Lower(Candidate);
			if(LowerCandidate.find(".gif") != std::string::npos || LowerCandidate.find(".webp") != std::string::npos || LowerCandidate.find(".png") != std::string::npos || LowerCandidate.find(".jpg") != std::string::npos || LowerCandidate.find(".jpeg") != std::string::npos)
				return Candidate;
			if(Best.empty() && (LowerCandidate.find(".mp4") != std::string::npos || LowerCandidate.find(".webm") != std::string::npos))
				Best = Candidate;
			Pos = End;
		}
	}
	return Best;
}
