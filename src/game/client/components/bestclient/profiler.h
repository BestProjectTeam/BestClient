/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_PROFILER_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_PROFILER_H

#include <cstdint>

class CComponent;
class CGameClient;
class IGraphics;
class IStorage;
class ITextRender;

void BestClientProfilerReset();
void BestClientProfilerAddClientUpdate(int64_t Nanoseconds);
void BestClientProfilerSetFrameStages(int64_t RenderNanoseconds, int64_t SwapNanoseconds);
void BestClientProfilerBeginPass();
bool BestClientProfilerAddUpdate(const CComponent *pComponent, int64_t Nanoseconds);
bool BestClientProfilerAddRender(const CComponent *pComponent, int64_t Nanoseconds);
void BestClientProfilerSetComponentName(const CComponent *pComponent, const char *pName);
void BestClientProfilerCommitFrame();
void BestClientProfilerObserve(CGameClient *pGameClient);
void BestClientRenderProfiler(IGraphics *pGraphics, ITextRender *pTextRender, float FrameTimeAverage);
bool BestClientProfilerDump(IStorage *pStorage);
bool BestClientProfilerHasNotice();
void BestClientRenderProfilerNotice(IGraphics *pGraphics, ITextRender *pTextRender);

#endif
