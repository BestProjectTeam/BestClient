/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_GIF_WHEEL_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_GIF_WHEEL_H

#include <base/str.h>

#include <engine/console.h>
#include <engine/graphics.h>

#include <generated/protocol.h>

#include <game/client/component.h>

#include <memory>
#include <vector>

class IConfigManager;
class CHttpRequest;

enum
{
	GIFWHEEL_MAX_SLOTS = 32,
};

// Circular quick-select menu of favorite CherryGifs, analogous to CBindWheel/CFastActions
// but each slot holds a gif (id/url/caption) instead of a console command.
class CGifWheel : public CComponent
{
public:
	class CSlot
	{
	public:
		char m_aGifId[32] = "";
		char m_aUrl[256] = "";
		char m_aCaption[128] = "";
		int m_EyeEmote = -1; // -1 = "None" (default) - don't touch the tee's current eyes at all
		IGraphics::CTextureHandle m_Thumbnail; // lazily loaded on first render of the wheel
		bool m_ThumbnailRequested = false;
		bool m_ThumbnailFailed = false;
		std::shared_ptr<CHttpRequest> m_pThumbnailRequest;

		bool operator==(const CSlot &Other) const
		{
			return str_comp(m_aGifId, Other.m_aGifId) == 0 && str_comp(m_aUrl, Other.m_aUrl) == 0;
		}
	};

	std::vector<CSlot> m_vSlots;

private:
	float m_AnimationTime = 0.0f;
	float m_aAnimationTimeItems[GIFWHEEL_MAX_SLOTS] = {0};
	float m_aAnimationTimeEyeEmotes[NUM_EMOTES] = {0}; // inner eye ring, mirrors CEmoticon

	bool m_Active = false;
	bool m_WasActive = false;
	int m_SelectedSlot = -1;
	int m_SelectedEyeEmote = -1; // hovering the inner eye ring; -1 = not hovering it

	static void ConAddGifWheel(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveGifWheel(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveAllGifWheelSlots(IConsole::IResult *pResult, void *pUserData);
	static void ConGifWheelExecuteHover(IConsole::IResult *pResult, void *pUserData);

	static void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData);

	void EnsureThumbnail(CSlot &Slot);
	void PollThumbnails();

public:
	CGifWheel();
	int Sizeof() const override { return sizeof(*this); }

	void OnReset() override;
	void OnRender() override;
	void OnConsoleInit() override;
	void OnRelease() override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	bool OnInput(const IInput::CEvent &Event) override;

	void AddSlot(const char *pGifId, const char *pUrl, const char *pCaption, int EyeEmote = -1);
	void RemoveSlot(int Index);
	void RemoveAllSlots();
	void SetSlotEyeEmote(int Index, int EyeEmote);

	// Kicks off thumbnail downloads for any slot that doesn't have one yet. Safe to call every
	// frame (no-ops for slots already loaded/loading/failed) - used by the settings UI, since
	// the in-game wheel only self-triggers this while actually online.
	void EnsureThumbnails();

	void ExecuteHoveredSlot();
	void ExecuteSlot(int Index);

	// Activates as a sub-mode of the emote wheel (see CEmoticon) - there is no dedicated bind for
	// this anymore, it can only be reached via the switch button in the middle of that wheel.
	void Activate();

	bool IsActive() const { return m_Active; }
};

#endif // GAME_CLIENT_COMPONENTS_BESTCLIENT_GIF_WHEEL_H
