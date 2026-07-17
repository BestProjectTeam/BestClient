#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_SCRIPTING_IMPL_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_SCRIPTING_IMPL_H

#include <functional>
#include <string>
#include <variant>

#define SCRIPTING_IMPL "chai"

class IStorage;

class CScriptingCtx
{
private:
	class CScriptingCtxData;
	CScriptingCtxData *m_pData;

	// If you are getting link errors, define more in the .cpp file
	template<typename T, typename... Args>
	void AddFunctionInternal(const char *pName, const std::function<T(Args...)> &Function);

public:
	using Any = std::variant<std::nullptr_t, std::string, bool, int, float>;

	static const char *Implementation();

	CScriptingCtx();
	~CScriptingCtx();

	template<typename T>
	void AddGlobal(const char *pName, const T &Object);
	template<typename T>
	void AddFunction(const char *pName, const T &Function)
	{
		AddFunctionInternal(pName, std::function(Function));
	}
	void Run(IStorage *pStorage, const char *pFilename, const char *pArgs);
};

#endif
