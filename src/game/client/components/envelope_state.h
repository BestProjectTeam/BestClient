#ifndef GAME_CLIENT_COMPONENTS_ENVELOPE_STATE_H
#define GAME_CLIENT_COMPONENTS_ENVELOPE_STATE_H

#include <game/client/component.h>
#include <game/map/render_interfaces.h>
#include <game/map/render_map.h>

#include <array>
#include <chrono>
#include <memory>

class CEnvelopeState : public CComponent, public IEnvelopeEval
{
public:
	CEnvelopeState() :
		m_pEnvelopePoints(nullptr), m_pMap(nullptr) {}
	CEnvelopeState(IMap *pMap, bool OnlineOnly);
	void EnvelopeEval(int TimeOffsetMillis, int EnvelopeIndex, ColorRGBA &Result, size_t Channels) const override;

	int Sizeof() const override { return sizeof(*this); }

private:
	class CCachedEval
	{
	public:
		int m_EnvelopeIndex = -1;
		int m_TimeOffsetMillis = 0;
		size_t m_Channels = 0;
		std::chrono::nanoseconds m_EvalTime{-1};
		ColorRGBA m_Result;
	};
	mutable std::array<CCachedEval, 32> m_aEvalCache{};

	std::shared_ptr<CMapBasedEnvelopePointAccess> m_pEnvelopePoints;
	IMap *m_pMap;
	bool m_OnlineOnly;
};

#endif
