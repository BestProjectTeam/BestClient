#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_HUD_WATCH_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_HUD_WATCH_H

#include <base/color.h>
#include <engine/shared/config.h>

#include <game/client/component.h>
#include <game/client/ui.h>

#include <unordered_map>
#include <vector>

struct SHudWatchItem
{
	const SConfigVariable *m_pVar;
	char m_aLabel[64];
	ColorRGBA m_Color;
	std::unordered_map<int, ColorRGBA> m_vValueColors;
};

class CHudWatch : public CComponent
{
	std::vector<SHudWatchItem> m_vWatches;
	bool m_Dirty;

	void LoadFromConfig();
	void SaveToConfig();

public:
	CHudWatch();
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnRender() override;
	void OnMessage(int MsgType, void *pRawMsg) override {}

	bool IsWatched(const SConfigVariable *pVar) const;
	void SetWatch(const SConfigVariable *pVar, const char *pLabel, ColorRGBA Color, std::unordered_map<int, ColorRGBA> ValueColors = {});
	void RemoveWatch(const SConfigVariable *pVar);

	const std::vector<SHudWatchItem> &GetWatches() const { return m_vWatches; }

	CUIRect GetHudEditorRect(bool ForcePreview = false) const;
};

#endif
