#ifndef CLIENTINDICATORSRV_TIMESTAMP_COMPAT_H
#define CLIENTINDICATORSRV_TIMESTAMP_COMPAT_H

#include <cstdlib>
#include <cstdint>

namespace BestClientIndicatorCompat
{
constexpr int64_t TIMESTAMP_WINDOW_SECONDS = 30;
constexpr int64_t TIMESTAMP_COMPAT_LOG_INTERVAL_SECONDS = 60;

enum class ETimestampValidation
{
	WITHIN_WINDOW,
	COMPATIBILITY_ACCEPTED,
};

struct CTimestampCheckResult
{
	ETimestampValidation m_Mode = ETimestampValidation::WITHIN_WINDOW;
	int64_t m_Now = 0;
	int64_t m_Timestamp = 0;
	int64_t m_SkewSeconds = 0;

	bool InWindow() const
	{
		return m_Mode == ETimestampValidation::WITHIN_WINDOW;
	}

	bool UsesCompatibilityPath() const
	{
		return m_Mode == ETimestampValidation::COMPATIBILITY_ACCEPTED;
	}
};

inline CTimestampCheckResult CheckTimestamp(int64_t Now, int64_t Timestamp)
{
	const int64_t SkewSeconds = Now - Timestamp;
	CTimestampCheckResult Result;
	Result.m_Now = Now;
	Result.m_Timestamp = Timestamp;
	Result.m_SkewSeconds = SkewSeconds;
	Result.m_Mode = llabs(SkewSeconds) <= TIMESTAMP_WINDOW_SECONDS ?
		ETimestampValidation::WITHIN_WINDOW :
		ETimestampValidation::COMPATIBILITY_ACCEPTED;
	return Result;
}
}

#endif
