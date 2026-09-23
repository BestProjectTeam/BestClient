/* Copyright © 2026 BestProject Team */
#include <cmath>

#include <game/client/components/bestclient/motion_blur.h>

void CCommandProcessorFragment_OpenGL3_3::EnsureMotionBlurTexture()
{
	SBcMotionBlurOpenGLState &State = m_MotionBlur;
	if(State.m_aTexture[0] != 0 && State.m_aTexture[1] != 0 && State.m_TexWidth == m_CanvasWidth && State.m_TexHeight == m_CanvasHeight)
		return;

	DestroyMotionBlurTexture();

	State.m_TexWidth = m_CanvasWidth;
	State.m_TexHeight = m_CanvasHeight;

	for(TWGLuint &Texture : State.m_aTexture)
	{
		glGenTextures(1, &Texture);
		glBindTexture(GL_TEXTURE_2D, Texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, State.m_TexWidth, State.m_TexHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	State.m_ReadIndex = 0;
}

void CCommandProcessorFragment_OpenGL3_3::DestroyMotionBlurTexture()
{
	SBcMotionBlurOpenGLState &State = m_MotionBlur;
	for(TWGLuint &Texture : State.m_aTexture)
	{
		if(Texture != 0)
			glDeleteTextures(1, &Texture);
		Texture = 0;
	}
	State.m_TexWidth = 0;
	State.m_TexHeight = 0;
	State.m_ReadIndex = 0;
	State.m_HistoryValid = false;
	State.m_LastTime = 0;
}

void CCommandProcessorFragment_OpenGL3_3::RenderMotionBlurGL()
{
	SBcMotionBlurOpenGLState &State = m_MotionBlur;
	const int ReadIndex = State.m_ReadIndex;
	const int WriteIndex = 1 - ReadIndex;

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, State.m_aTexture[ReadIndex]);

	if(!State.m_HistoryValid)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, State.m_TexWidth, State.m_TexHeight);
		glBindTexture(GL_TEXTURE_2D, State.m_aTexture[WriteIndex]);
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, State.m_TexWidth, State.m_TexHeight);
		glBindTexture(GL_TEXTURE_2D, 0);
		State.m_HistoryValid = true;
		return;
	}

	const float StrengthT = g_Config.m_BcMotionBlurStrength / 95.0f;
	const float RefAlpha60 = StrengthT * StrengthT * 0.58f;
	const float FrameTime = BcMotionBlurAdvanceFrameTime(State.m_LastTime);
	const float BlendAlpha = BcMotionBlurPersistence(RefAlpha60, FrameTime);
	if(BlendAlpha <= 0.0f)
		return;

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, m_CanvasWidth, m_CanvasHeight);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	if(m_LastClipEnable)
	{
		glDisable(GL_SCISSOR_TEST);
		m_LastClipEnable = false;
	}

	UseProgram(m_pPrimitiveProgramTextured);
	const float aIdentityPos[8] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
	glUniformMatrix4x2fv(m_pPrimitiveProgramTextured->m_LocPos, 1, true, aIdentityPos);
	m_pPrimitiveProgramTextured->m_LastScreenTL = vec2(0.0f, 0.0f);
	m_pPrimitiveProgramTextured->m_LastScreenBR = vec2(0.0f, 0.0f);

	glBindSampler(0, 0);
	glBindTexture(GL_TEXTURE_2D, State.m_aTexture[ReadIndex]);
	glUniform1i(m_pPrimitiveProgramTextured->m_LocTextureSampler, 0);
	m_pPrimitiveProgramTextured->m_LastTextureSampler = -1;

	const uint8_t Alpha = (uint8_t)std::round(BlendAlpha * 255.0f);
	CCommandBuffer::SVertex aVertices[4];
	aVertices[0].m_Pos = vec2(-1.0f, -1.0f);
	aVertices[0].m_Tex = vec2(0.0f, 0.0f);
	aVertices[1].m_Pos = vec2(1.0f, -1.0f);
	aVertices[1].m_Tex = vec2(1.0f, 0.0f);
	aVertices[2].m_Pos = vec2(1.0f, 1.0f);
	aVertices[2].m_Tex = vec2(1.0f, 1.0f);
	aVertices[3].m_Pos = vec2(-1.0f, 1.0f);
	aVertices[3].m_Tex = vec2(0.0f, 1.0f);
	for(auto &Vertex : aVertices)
	{
		Vertex.m_Color.r = 255;
		Vertex.m_Color.g = 255;
		Vertex.m_Color.b = 255;
		Vertex.m_Color.a = Alpha;
	}

	UploadStreamBufferData(EPrimitiveType::QUADS, aVertices, sizeof(CCommandBuffer::SVertex), 1);
	glBindVertexArray(m_aPrimitiveDrawVertexId[m_LastStreamBuffer]);
	if(m_aLastIndexBufferBound[m_LastStreamBuffer] != m_QuadDrawIndexBufferId)
	{
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_QuadDrawIndexBufferId);
		m_aLastIndexBufferBound[m_LastStreamBuffer] = m_QuadDrawIndexBufferId;
	}
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
	m_LastStreamBuffer = (m_LastStreamBuffer + 1 >= MAX_STREAM_BUFFER_COUNT ? 0 : m_LastStreamBuffer + 1);

	glBindTexture(GL_TEXTURE_2D, State.m_aTexture[WriteIndex]);
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, State.m_TexWidth, State.m_TexHeight);
	glBindTexture(GL_TEXTURE_2D, 0);
	State.m_ReadIndex = WriteIndex;

	m_LastBlendMode = EBlendMode::NONE;
}

void CCommandProcessorFragment_OpenGL3_3::Cmd_BeforeSwap()
{
	SBcMotionBlurOpenGLState &State = m_MotionBlur;
	const bool Enabled = BcMotionBlurEnabled();
	if(Enabled != State.m_EnabledLastFrame)
	{
		State.m_HistoryValid = false;
		State.m_LastTime = 0;
		State.m_EnabledLastFrame = Enabled;
	}
	if(!Enabled || m_CanvasWidth == 0 || m_CanvasHeight == 0)
		return;

	EnsureMotionBlurTexture();
	RenderMotionBlurGL();
}
