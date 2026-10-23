/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_KEYBIND_MAP_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_KEYBIND_MAP_H

#include <base/str.h>

#include <engine/keys.h>

enum class EKeybindMapFormat
{
	FULL = 0,
	TENKEYLESS = 1,
	COMPACT = 2,
};

enum class EKeybindMapZone
{
	ALPHA = 0,
	FUNCTION_NAV = 1,
	NUMPAD = 2,
};

enum class EKeybindMapCommit
{
	APPLY = 0,
	CLEAR = 1,
	RESET = 2,
};

struct CKeybindMapKey
{
	int m_Key;
	const char *m_pLabel;
	float m_X;
	float m_Y;
	float m_W;
	float m_H;
	EKeybindMapZone m_Zone;
};

struct CKeybindMapBinding
{
	int m_Key = KEY_UNKNOWN;
	char m_aCommand[512] = {'\0'};
};

inline constexpr CKeybindMapKey KEYBIND_MAP_KEYS[] = {
	{KEY_GRAVE, "`", 0.0f, 0.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_1, "1", 1.0f, 0.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_2, "2", 2.0f, 0.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_3, "3", 3.0f, 0.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_4, "4", 4.0f, 0.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_5, "5", 5.0f, 0.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_6, "6", 6.0f, 0.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_7, "7", 7.0f, 0.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_8, "8", 8.0f, 0.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_9, "9", 9.0f, 0.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_0, "0", 10.0f, 0.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_MINUS, "-", 11.0f, 0.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_EQUALS, "=", 12.0f, 0.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_BACKSPACE, "Bksp", 13.0f, 0.0f, 2.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_TAB, "Tab", 0.0f, 1.0f, 1.5f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_Q, "Q", 1.5f, 1.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_W, "W", 2.5f, 1.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_E, "E", 3.5f, 1.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_R, "R", 4.5f, 1.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_T, "T", 5.5f, 1.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_Y, "Y", 6.5f, 1.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_U, "U", 7.5f, 1.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_I, "I", 8.5f, 1.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_O, "O", 9.5f, 1.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_P, "P", 10.5f, 1.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_LEFTBRACKET, "[", 11.5f, 1.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_RIGHTBRACKET, "]", 12.5f, 1.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_BACKSLASH, "\\", 13.5f, 1.0f, 1.5f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_CAPSLOCK, "Caps", 0.0f, 2.0f, 1.75f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_A, "A", 1.75f, 2.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_S, "S", 2.75f, 2.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_D, "D", 3.75f, 2.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_F, "F", 4.75f, 2.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_G, "G", 5.75f, 2.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_H, "H", 6.75f, 2.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_J, "J", 7.75f, 2.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_K, "K", 8.75f, 2.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_L, "L", 9.75f, 2.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_SEMICOLON, ";", 10.75f, 2.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_APOSTROPHE, "'", 11.75f, 2.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_RETURN, "Enter", 12.75f, 2.0f, 2.25f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_LSHIFT, "Shift", 0.0f, 3.0f, 2.25f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_Z, "Z", 2.25f, 3.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_X, "X", 3.25f, 3.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_C, "C", 4.25f, 3.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_V, "V", 5.25f, 3.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_B, "B", 6.25f, 3.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_N, "N", 7.25f, 3.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_M, "M", 8.25f, 3.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_COMMA, ",", 9.25f, 3.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_PERIOD, ".", 10.25f, 3.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_SLASH, "/", 11.25f, 3.0f, 1.0f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_RSHIFT, "Shift", 12.25f, 3.0f, 2.75f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_LCTRL, "Ctrl", 0.0f, 4.0f, 1.25f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_LGUI, "Super", 1.25f, 4.0f, 1.25f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_LALT, "Alt", 2.5f, 4.0f, 1.25f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_SPACE, "Space", 3.75f, 4.0f, 6.25f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_RALT, "Alt", 10.0f, 4.0f, 1.25f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_RGUI, "Super", 11.25f, 4.0f, 1.25f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_APPLICATION, "Menu", 12.5f, 4.0f, 1.25f, 1.0f, EKeybindMapZone::ALPHA},
	{KEY_RCTRL, "Ctrl", 13.75f, 4.0f, 1.25f, 1.0f, EKeybindMapZone::ALPHA},

	{KEY_ESCAPE, "Esc", 0.0f, -1.3f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_F1, "F1", 2.0f, -1.3f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_F2, "F2", 3.0f, -1.3f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_F3, "F3", 4.0f, -1.3f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_F4, "F4", 5.0f, -1.3f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_F5, "F5", 6.5f, -1.3f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_F6, "F6", 7.5f, -1.3f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_F7, "F7", 8.5f, -1.3f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_F8, "F8", 9.5f, -1.3f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_F9, "F9", 11.0f, -1.3f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_F10, "F10", 12.0f, -1.3f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_F11, "F11", 13.0f, -1.3f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_F12, "F12", 14.0f, -1.3f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_PRINTSCREEN, "Prt", 15.75f, -1.3f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_SCROLLLOCK, "Scr", 16.75f, -1.3f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_PAUSE, "Pau", 17.75f, -1.3f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_INSERT, "Ins", 15.75f, 0.0f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_HOME, "Home", 16.75f, 0.0f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_PAGEUP, "PgUp", 17.75f, 0.0f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_DELETE, "Del", 15.75f, 1.0f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_END, "End", 16.75f, 1.0f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_PAGEDOWN, "PgDn", 17.75f, 1.0f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_UP, "Up", 16.75f, 3.0f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_LEFT, "Left", 15.75f, 4.0f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_DOWN, "Down", 16.75f, 4.0f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},
	{KEY_RIGHT, "Right", 17.75f, 4.0f, 1.0f, 1.0f, EKeybindMapZone::FUNCTION_NAV},

	{KEY_NUMLOCKCLEAR, "Num", 19.5f, 0.0f, 1.0f, 1.0f, EKeybindMapZone::NUMPAD},
	{KEY_KP_DIVIDE, "/", 20.5f, 0.0f, 1.0f, 1.0f, EKeybindMapZone::NUMPAD},
	{KEY_KP_MULTIPLY, "*", 21.5f, 0.0f, 1.0f, 1.0f, EKeybindMapZone::NUMPAD},
	{KEY_KP_MINUS, "-", 22.5f, 0.0f, 1.0f, 1.0f, EKeybindMapZone::NUMPAD},
	{KEY_KP_7, "7", 19.5f, 1.0f, 1.0f, 1.0f, EKeybindMapZone::NUMPAD},
	{KEY_KP_8, "8", 20.5f, 1.0f, 1.0f, 1.0f, EKeybindMapZone::NUMPAD},
	{KEY_KP_9, "9", 21.5f, 1.0f, 1.0f, 1.0f, EKeybindMapZone::NUMPAD},
	{KEY_KP_PLUS, "+", 22.5f, 1.0f, 1.0f, 2.0f, EKeybindMapZone::NUMPAD},
	{KEY_KP_4, "4", 19.5f, 2.0f, 1.0f, 1.0f, EKeybindMapZone::NUMPAD},
	{KEY_KP_5, "5", 20.5f, 2.0f, 1.0f, 1.0f, EKeybindMapZone::NUMPAD},
	{KEY_KP_6, "6", 21.5f, 2.0f, 1.0f, 1.0f, EKeybindMapZone::NUMPAD},
	{KEY_KP_1, "1", 19.5f, 3.0f, 1.0f, 1.0f, EKeybindMapZone::NUMPAD},
	{KEY_KP_2, "2", 20.5f, 3.0f, 1.0f, 1.0f, EKeybindMapZone::NUMPAD},
	{KEY_KP_3, "3", 21.5f, 3.0f, 1.0f, 1.0f, EKeybindMapZone::NUMPAD},
	{KEY_KP_ENTER, "Ent", 22.5f, 3.0f, 1.0f, 2.0f, EKeybindMapZone::NUMPAD},
	{KEY_KP_0, "0", 19.5f, 4.0f, 2.0f, 1.0f, EKeybindMapZone::NUMPAD},
	{KEY_KP_PERIOD, ".", 21.5f, 4.0f, 1.0f, 1.0f, EKeybindMapZone::NUMPAD},
};

inline constexpr int KeybindMapKeyCount()
{
	return (int)(sizeof(KEYBIND_MAP_KEYS) / sizeof(KEYBIND_MAP_KEYS[0]));
}

inline const CKeybindMapKey *KeybindMapKeys()
{
	return KEYBIND_MAP_KEYS;
}

inline bool KeybindMapZoneVisible(EKeybindMapFormat Format, EKeybindMapZone Zone)
{
	if(Zone == EKeybindMapZone::ALPHA)
		return true;
	if(Zone == EKeybindMapZone::FUNCTION_NAV)
		return Format != EKeybindMapFormat::COMPACT;
	return Format == EKeybindMapFormat::FULL;
}

inline bool KeybindMapKeyVisible(EKeybindMapFormat Format, int Key)
{
	for(const CKeybindMapKey &KeyDef : KEYBIND_MAP_KEYS)
	{
		if(KeyDef.m_Key == Key)
			return KeybindMapZoneVisible(Format, KeyDef.m_Zone);
	}
	return false;
}

inline const char *KeybindMapKeyLabel(int Key)
{
	for(const CKeybindMapKey &KeyDef : KEYBIND_MAP_KEYS)
	{
		if(KeyDef.m_Key == Key)
			return KeyDef.m_pLabel;
	}
	return "";
}

inline bool KeybindMapIsBound(const char *pCommand)
{
	return pCommand != nullptr && pCommand[0] != '\0';
}

inline const char *KeybindMapMoveLeftCommand()
{
	return "+left";
}

inline const char *KeybindMapDefaultCommand(int Key)
{
	switch(Key)
	{
	case KEY_F1: return "toggle_local_console";
	case KEY_F2: return "toggle_remote_console";
	case KEY_F3: return "vote yes";
	case KEY_F4: return "vote no";
	case KEY_F10: return "screenshot";
	case KEY_TAB: return "+scoreboard";
	case KEY_A: return "+left";
	case KEY_D: return "+right";
	case KEY_SPACE: return "+jump";
	case KEY_LSHIFT: return "+emote";
	case KEY_Q: return "say /spec";
	case KEY_RETURN: return "+show_chat; chat all";
	case KEY_RSHIFT: return "+spectate";
	case KEY_1: return "+weapon1";
	case KEY_2: return "+weapon2";
	case KEY_3: return "+weapon3";
	case KEY_4: return "+weapon4";
	case KEY_5: return "+weapon5";
	case KEY_T: return "+show_chat; chat all";
	case KEY_Y: return "+show_chat; chat team";
	case KEY_U: return "+show_chat";
	case KEY_I: return "+show_chat; chat all /c ";
	case KEY_K: return "kill";
	case KEY_P: return "say /pause";
	case KEY_KP_PLUS: return "zoom+";
	case KEY_KP_MINUS: return "zoom-";
	case KEY_KP_MULTIPLY: return "zoom";
	case KEY_PAUSE: return "say /pause";
	case KEY_UP: return "+jump";
	case KEY_LEFT: return "+left";
	case KEY_RIGHT: return "+right";
	case KEY_LEFTBRACKET: return "+prevweapon";
	case KEY_RIGHTBRACKET: return "+nextweapon";
	case KEY_C: return "say /rank";
	case KEY_V: return "say /info";
	case KEY_B: return "say /top5";
	case KEY_S: return "+showhookcoll";
	case KEY_X: return "toggle cl_dummy 0 1";
	case KEY_H: return "toggle cl_dummy_hammer 0 1";
	case KEY_SLASH: return "+show_chat; chat all /";
	case KEY_KP_0: return "say /emote normal 999999";
	case KEY_KP_1: return "say /emote happy 999999";
	case KEY_KP_2: return "say /emote angry 999999";
	case KEY_KP_3: return "say /emote pain 999999";
	case KEY_KP_4: return "say /emote surprise 999999";
	case KEY_KP_5: return "say /emote blink 999999";
	case KEY_MINUS: return "spectate_previous";
	case KEY_EQUALS: return "spectate_next";
	case KEY_LALT: return "toggle_scoreboard_cursor";
	case KEY_W: return "+jump";
	default: return "";
	}
}

inline bool KeybindMapCommit(CKeybindMapBinding *pBindings, int NumBindings, int Key, EKeybindMapCommit Commit, const char *pCommand)
{
	if(pBindings == nullptr || NumBindings <= 0 || Key <= KEY_UNKNOWN || Key >= KEY_LAST || Key == KEY_ESCAPE)
		return false;

	const char *pNext = "";
	if(Commit == EKeybindMapCommit::APPLY)
		pNext = pCommand != nullptr ? pCommand : "";
	else if(Commit == EKeybindMapCommit::RESET)
		pNext = KeybindMapDefaultCommand(Key);

	bool Updated = false;
	for(int i = 0; i < NumBindings; i++)
	{
		if(pBindings[i].m_Key != Key)
			continue;
		str_copy(pBindings[i].m_aCommand, pNext, sizeof(pBindings[i].m_aCommand));
		Updated = true;
	}
	return Updated;
}

inline bool KeybindMapActionVisible(const char *pSearch, const char *pLabel, const char *pCommand)
{
	if(pSearch == nullptr || pSearch[0] == '\0')
		return true;
	if(pLabel != nullptr && str_utf8_find_nocase(pLabel, pSearch) != nullptr)
		return true;
	return pCommand != nullptr && str_utf8_find_nocase(pCommand, pSearch) != nullptr;
}

#endif
