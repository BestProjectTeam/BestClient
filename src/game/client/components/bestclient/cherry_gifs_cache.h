/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_CHERRY_GIFS_CACHE_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_CHERRY_GIFS_CACHE_H

#include <cstddef>
#include <vector>

class IStorage;

// Small disk cache for downloaded CherryGifs bytes, keyed by the API's own gif id. Shared between
// the browser grid (cherry_gifs.cpp) and the wheel (gif_wheel.cpp) so a gif already seen once
// doesn't get re-downloaded from the network every time the client restarts, or when the same gif
// shows up in both places.
namespace CherryGifsCache
{
bool Load(IStorage *pStorage, const char *pGifId, std::vector<unsigned char> &vOutData);
void Save(IStorage *pStorage, const char *pGifId, const unsigned char *pData, size_t DataSize);
}

#endif // GAME_CLIENT_COMPONENTS_BESTCLIENT_CHERRY_GIFS_CACHE_H
