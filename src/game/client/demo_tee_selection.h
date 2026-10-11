#ifndef GAME_CLIENT_DEMO_TEE_SELECTION_H
#define GAME_CLIENT_DEMO_TEE_SELECTION_H

#include <base/vmath.h>

#include <algorithm>
#include <limits>
#include <span>

namespace DemoTeeSelection
{
struct SCandidate
{
	int m_ClientId;
	vec2 m_Position;
};

inline vec2 MouseToWorld(vec2 Mouse, vec2 UiSize, vec2 CameraCenter, vec2 WorldSize)
{
	return CameraCenter + (Mouse / UiSize - vec2(0.5f, 0.5f)) * WorldSize;
}

inline int Pick(std::span<const SCandidate> Candidates, vec2 MouseWorld, float Radius, int SelectedClientId)
{
	const SCandidate *pNearest = nullptr;
	float NearestDistance = Radius * Radius;
	for(const SCandidate &Candidate : Candidates)
	{
		const float Distance = length_squared(Candidate.m_Position - MouseWorld);
		if(Distance <= NearestDistance &&
			(pNearest == nullptr || Distance < NearestDistance || Candidate.m_ClientId < pNearest->m_ClientId))
		{
			pNearest = &Candidate;
			NearestDistance = Distance;
		}
	}
	if(pNearest == nullptr)
		return -1;

	constexpr float OverlapDistance = 12.0f;
	bool SelectedOverlaps = false;
	int FirstClientId = std::numeric_limits<int>::max();
	int NextClientId = std::numeric_limits<int>::max();
	for(const SCandidate &Candidate : Candidates)
	{
		if(length_squared(Candidate.m_Position - MouseWorld) > Radius * Radius ||
			length_squared(Candidate.m_Position - pNearest->m_Position) > OverlapDistance * OverlapDistance)
			continue;
		SelectedOverlaps |= Candidate.m_ClientId == SelectedClientId;
		FirstClientId = std::min(FirstClientId, Candidate.m_ClientId);
		if(Candidate.m_ClientId > SelectedClientId)
			NextClientId = std::min(NextClientId, Candidate.m_ClientId);
	}
	if(!SelectedOverlaps)
		return pNearest->m_ClientId;
	return NextClientId == std::numeric_limits<int>::max() ? FirstClientId : NextClientId;
}
}

#endif
