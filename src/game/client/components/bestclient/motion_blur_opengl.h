/* Copyright © 2026 BestProject Team */
#ifndef GAME_CLIENT_COMPONENTS_BESTCLIENT_MOTION_BLUR_OPENGL_H
#define GAME_CLIENT_COMPONENTS_BESTCLIENT_MOTION_BLUR_OPENGL_H

#include <engine/client/graphics_defines.h>

#include <cstdint>

struct SBcMotionBlurOpenGLState
{
	TWGLuint m_aTexture[2] = {0, 0};
	uint32_t m_TexWidth = 0;
	uint32_t m_TexHeight = 0;
	int m_ReadIndex = 0;
	bool m_HistoryValid = false;
	bool m_EnabledLastFrame = false;
	int64_t m_LastTime = 0;
};

#endif
