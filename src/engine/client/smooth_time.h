/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#ifndef ENGINE_CLIENT_SMOOTH_TIME_H
#define ENGINE_CLIENT_SMOOTH_TIME_H

#include <base/time.h>
#include <engine/shared/config.h>

#include <cstdint>

class CGraph;

class CSmoothTime
{
public:
	enum EAdjustDirection
	{
		ADJUSTDIRECTION_DOWN = 0,
		ADJUSTDIRECTION_UP,
		NUM_ADJUSTDIRECTIONS,
	};

private:
	int64_t m_Snap;
	int64_t m_Current;
	int64_t m_Target;
	int64_t m_Margin;
	int64_t m_SnapMargin;
	int64_t m_CurrentMargin;
	int64_t m_TargetMargin;

	int m_SpikeCounter;
	float m_aAdjustSpeed[NUM_ADJUSTDIRECTIONS];

public:
	void Init(int64_t Target);
	void SetAdjustSpeed(EAdjustDirection Direction, float Value);

	int64_t Get(int64_t Now) const
	{
		const int64_t Diff = Now - m_Snap;
		const int64_t CurTime = m_Current + Diff;
		const int64_t TargetTime = m_Target + Diff;

		const float AdjustSpeed = (TargetTime > CurTime) ? m_aAdjustSpeed[ADJUSTDIRECTION_UP] : m_aAdjustSpeed[ADJUSTDIRECTION_DOWN];

		float Progress = (Diff / (float)time_freq()) * AdjustSpeed;
		if(Progress > 1.0f)
			Progress = 1.0f;

		const int64_t Result = CurTime + (int64_t)((TargetTime - CurTime) * Progress);
		if(g_Config.m_TcSmoothPredictionMargin)
			return Result + GetMargin(Now);
		return Result + m_Margin;
	}

	void UpdateInt(int64_t Target);
	void Update(CGraph *pGraph, int64_t Target, int TimeLeft, EAdjustDirection AdjustDirection);

	void UpdateMargin(int64_t Margin);
	int64_t GetMargin(int64_t Now) const;
};

#endif
