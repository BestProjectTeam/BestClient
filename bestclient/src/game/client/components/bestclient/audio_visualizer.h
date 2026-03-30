/* (c) BestClient */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_AUDIO_VISUALIZER_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_AUDIO_VISUALIZER_H

#include <game/client/component.h>

#include "subsystem_runtime.h"

#include <SDL_audio.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class CAudioVisualizer : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	struct SComplex
	{
		float r = 0.0f;
		float i = 0.0f;
	};

	void OnReset() override;
	void OnShutdown() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnUpdate() override;

	bool HasSignal() const { return m_HasSignal; }
	bool IsCaptureValid() const { return m_CaptureValid; }
	bool GetBands(int Bands, float *pOut) const;

private:
	class CPcmRingBuffer
	{
	public:
		void Reset(size_t CapacitySamples);
		void Clear();
		size_t Size() const { return m_Size; }
		size_t Capacity() const { return m_vData.size(); }
		void PushBack(const int16_t *pSamples, size_t Count);
		void DiscardFront(size_t Count);
		void CopyLast(size_t Count, int16_t *pDst) const;

	private:
		std::vector<int16_t> m_vData;
		size_t m_Head = 0;
		size_t m_Size = 0;
	};

	SDL_AudioDeviceID m_CaptureDevice = 0;
	SDL_AudioSpec m_CaptureSpec = {};
	bool m_CaptureValid = false;
	std::string m_CaptureDeviceName;
	int m_LastCaptureConfigIndex = -2;

	CPcmRingBuffer m_Pcm;
	std::vector<float> m_vWindow;
	std::vector<SComplex> m_vFft;
	std::vector<int16_t> m_vMixedSamples;
	std::vector<int16_t> m_vFrameSamples;
	std::vector<float> m_vBands; // 0..1
	bool m_HasSignal = false;
	ESubsystemRuntimeState m_RuntimeState = ESubsystemRuntimeState::DISABLED;
	int64_t m_LastDebugPrintTick = 0;
	bool m_DumpedDevices = false;
	float m_LastRms = 0.0f;
	float m_LastMaxAbs = 0.0f;
	int64_t m_LastSpectrumUpdateTick = 0;
	int64_t m_LastPerfReportTick = 0;
	int64_t m_LastUpdateCostTick = 0;
	int64_t m_MaxUpdateCostTick = 0;
	int64_t m_TotalUpdateCostTick = 0;
	int64_t m_UpdateSamples = 0;

	int m_LastFftSize = 0;
	int m_LastBands = 0;

	bool NeedCapture() const;
	bool EnsureCaptureOpen();
	void CloseCapture();
	void EnsureBuffers();
	void CaptureAudio();
	void ComputeBands(float Delta, bool Force);
};

#endif
