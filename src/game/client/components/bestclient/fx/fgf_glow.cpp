/* Copyright © 2026 BestProject Team */
#include "fgf_glow.h"

#include <base/math.h>

#include <engine/image.h>

#include <algorithm>
#include <cmath>

static float GlowFalloff(float Distance)
{
	if(Distance >= 1.0f)
		return 0.0f;
	const float Inv = 1.0f - Distance;
	return Inv * Inv * (0.35f + 0.65f * Inv);
}

static float GlowAlpha(float Dx, float Dy)
{
	return GlowFalloff(std::sqrt(Dx * Dx + Dy * Dy));
}

static float RingAlpha(float Dx, float Dy)
{
	const float Distance = std::sqrt(Dx * Dx + Dy * Dy);
	if(Distance >= 1.0f)
		return 0.0f;
	const float Band = (Distance - 0.78f) / 0.09f;
	return std::exp(-Band * Band) * std::clamp((1.0f - Distance) / 0.1f, 0.0f, 1.0f);
}

static float DiscAlpha(float Dx, float Dy)
{
	return std::clamp((1.0f - std::sqrt(Dx * Dx + Dy * Dy)) / 0.06f, 0.0f, 1.0f);
}

static float BeamAlpha(float Dx, float Dy)
{
	return GlowFalloff(std::abs(Dy));
}

static IGraphics::CTextureHandle CreateTexture(IGraphics *pGraphics, int Width, int Height, float (*pAlpha)(float Dx, float Dy), const char *pName)
{
	CImageInfo Image;
	Image.m_Width = Width;
	Image.m_Height = Height;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.Allocate();
	for(int y = 0; y < Height; y++)
	{
		for(int x = 0; x < Width; x++)
		{
			const float Dx = (x + 0.5f) / Width * 2.0f - 1.0f;
			const float Dy = (y + 0.5f) / Height * 2.0f - 1.0f;
			uint8_t *pPixel = &Image.m_pData[(y * Width + x) * 4];
			pPixel[0] = 255;
			pPixel[1] = 255;
			pPixel[2] = 255;
			pPixel[3] = (uint8_t)std::clamp(round_to_int(pAlpha(Dx, Dy) * 255.0f), 0, 255);
		}
	}
	return pGraphics->LoadTextureRawMove(Image, 0, pName);
}

IGraphics::CTextureHandle FgfGlow::CreateGlowTexture(IGraphics *pGraphics)
{
	return CreateTexture(pGraphics, 64, 64, GlowAlpha, "fgf_glow");
}

IGraphics::CTextureHandle FgfGlow::CreateRingTexture(IGraphics *pGraphics)
{
	return CreateTexture(pGraphics, 128, 128, RingAlpha, "fgf_ring");
}

IGraphics::CTextureHandle FgfGlow::CreateDiscTexture(IGraphics *pGraphics)
{
	return CreateTexture(pGraphics, 128, 128, DiscAlpha, "fgf_disc");
}

IGraphics::CTextureHandle FgfGlow::CreateBeamTexture(IGraphics *pGraphics)
{
	return CreateTexture(pGraphics, 8, 64, BeamAlpha, "fgf_beam");
}

IGraphics::CTextureHandle FgfGlow::CreateLeafTexture(IGraphics *pGraphics)
{
	const int Size = 64;
	CImageInfo Image;
	Image.m_Width = Size;
	Image.m_Height = Size;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.Allocate();
	for(int y = 0; y < Size; y++)
	{
		for(int x = 0; x < Size; x++)
		{
			const float U = (x + 0.5f) / Size * 2.0f - 1.0f;
			const float V = (y + 0.5f) / Size * 2.0f - 1.0f;
			const float HalfWidth = 0.5f * std::pow(std::max(0.0f, 1.0f - U * U), 0.8f) * (1.0f - 0.25f * U);
			const float Inside = HalfWidth - std::abs(V);
			const bool Stem = U < -0.8f && std::abs(V) < 0.05f;

			float Alpha = std::clamp(Inside / 0.05f, 0.0f, 1.0f);
			float Shade = mix(0.8f, 1.0f, (V + 1.0f) * 0.5f);
			if(Inside < 0.07f)
				Shade = 0.45f;
			else if(std::abs(V) < 0.03f && std::abs(U) < 0.8f)
				Shade *= 0.7f;
			if(Stem)
			{
				Alpha = 1.0f;
				Shade = 0.45f;
			}

			uint8_t *pPixel = &Image.m_pData[(y * Size + x) * 4];
			pPixel[0] = pPixel[1] = pPixel[2] = (uint8_t)round_to_int(Shade * 255.0f);
			pPixel[3] = (uint8_t)round_to_int(Alpha * 255.0f);
		}
	}
	return pGraphics->LoadTextureRawMove(Image, 0, "fgf_leaf");
}

void FgfGlow::BeginStrips(IGraphics *pGraphics, IGraphics::CTextureHandle BeamTexture)
{
	pGraphics->TextureSet(BeamTexture);
	pGraphics->QuadsBegin();
	pGraphics->QuadsSetRotation(0.0f);
	pGraphics->QuadsSetSubsetFree(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f);
}

void FgfGlow::DrawStrip(IGraphics *pGraphics, const vec2 *pPoints, const float *pWidths, int NumPoints, ColorRGBA Color)
{
	NumPoints = std::min<int>(NumPoints, MAX_STRIP_POINTS);
	if(NumPoints < 2)
		return;

	vec2 aNormals[MAX_STRIP_POINTS];
	for(int i = 0; i < NumPoints; i++)
	{
		const vec2 Tangent = pPoints[std::min(i + 1, NumPoints - 1)] - pPoints[std::max(i - 1, 0)];
		const float Length = length(Tangent);
		aNormals[i] = Length > 0.001f ? vec2(-Tangent.y, Tangent.x) / Length : vec2(0.0f, 0.0f);
	}

	IGraphics::CFreeformItem aQuads[MAX_STRIP_POINTS - 1];
	for(int i = 0; i < NumPoints - 1; i++)
	{
		const vec2 &From = pPoints[i];
		const vec2 &To = pPoints[i + 1];
		aQuads[i] = IGraphics::CFreeformItem(
			To + aNormals[i + 1] * pWidths[i + 1], To - aNormals[i + 1] * pWidths[i + 1],
			From + aNormals[i] * pWidths[i], From - aNormals[i] * pWidths[i]);
	}

	pGraphics->SetColor(Color);
	pGraphics->QuadsDrawFreeform(aQuads, NumPoints - 1);
}

ColorRGBA FgfGlow::Whiten(ColorRGBA Color, float Amount)
{
	return ColorRGBA(mix(Color.r, 1.0f, Amount), mix(Color.g, 1.0f, Amount), mix(Color.b, 1.0f, Amount), 1.0f);
}

ColorRGBA FgfGlow::Blend(ColorRGBA A, ColorRGBA B, float Amount)
{
	return ColorRGBA(mix(A.r, B.r, Amount), mix(A.g, B.g, Amount), mix(A.b, B.b, Amount), 1.0f);
}

void CGlowParticles::Clear()
{
	m_NumSparks = 0;
	m_NumRings = 0;
}

CGlowParticles::CSpark *CGlowParticles::NewSpark()
{
	if(m_NumSparks >= MAX_SPARKS)
		return nullptr;
	CSpark *pSpark = &m_aSparks[m_NumSparks++];
	*pSpark = CSpark{};
	pSpark->m_Drag = 1.0f;
	return pSpark;
}

void CGlowParticles::AddRing(vec2 Pos, float LifeSpan, float StartSize, float EndSize, float Alpha, ColorRGBA Color)
{
	if(m_NumRings >= MAX_RINGS)
		return;
	CRing &Ring = m_aRings[m_NumRings++];
	Ring.m_Pos = Pos;
	Ring.m_Life = 0.0f;
	Ring.m_LifeSpan = LifeSpan;
	Ring.m_StartSize = StartSize;
	Ring.m_EndSize = EndSize;
	Ring.m_Alpha = Alpha;
	Ring.m_Color = Color;
}

void CGlowParticles::Update(float Passed)
{
	for(int i = 0; i < m_NumSparks;)
	{
		CSpark &Spark = m_aSparks[i];
		Spark.m_Life += Passed;
		if(Spark.m_Life >= Spark.m_LifeSpan)
		{
			Spark = m_aSparks[--m_NumSparks];
			continue;
		}

		if(Spark.m_Orbital)
		{
			const float Spin = Spark.m_AngularSpeed * (1.0f + 30.0f / (Spark.m_Radius + 10.0f));
			Spark.m_Radius = std::max(0.0f, Spark.m_Radius + Spark.m_RadialSpeed * Passed);
			Spark.m_Angle += Spin * Passed;
			const vec2 Outward = direction(Spark.m_Angle);
			Spark.m_Pos = Spark.m_Center + Outward * Spark.m_Radius;
			Spark.m_Vel = vec2(-Outward.y, Outward.x) * Spin * Spark.m_Radius + Outward * Spark.m_RadialSpeed;
		}
		else
		{
			Spark.m_Vel.y += Spark.m_Gravity * Passed;
			Spark.m_Vel *= std::pow(Spark.m_Drag, Passed);
			Spark.m_Pos += Spark.m_Vel * Passed;
		}
		i++;
	}

	for(int i = 0; i < m_NumRings;)
	{
		m_aRings[i].m_Life += Passed;
		if(m_aRings[i].m_Life >= m_aRings[i].m_LifeSpan)
		{
			m_aRings[i] = m_aRings[--m_NumRings];
			continue;
		}
		i++;
	}
}

void CGlowParticles::Draw(IGraphics *pGraphics, IGraphics::CTextureHandle GlowTexture, IGraphics::CTextureHandle RingTexture, float AlphaScale) const
{
	pGraphics->TextureSet(RingTexture);
	pGraphics->QuadsBegin();
	pGraphics->QuadsSetRotation(0.0f);
	for(int i = 0; i < m_NumRings; i++)
	{
		const CRing &Ring = m_aRings[i];
		const float Progress = Ring.m_Life / Ring.m_LifeSpan;
		const float Spread = 1.0f - (1.0f - Progress) * (1.0f - Progress);
		const float Size = mix(Ring.m_StartSize, Ring.m_EndSize, Spread);
		pGraphics->SetColor(Ring.m_Color.WithAlpha(Ring.m_Alpha * (1.0f - Progress) * AlphaScale));
		IGraphics::CQuadItem Quad(Ring.m_Pos.x, Ring.m_Pos.y, Size, Size);
		pGraphics->QuadsDraw(&Quad, 1);
	}
	pGraphics->QuadsEnd();

	pGraphics->TextureSet(GlowTexture);
	pGraphics->QuadsBegin();
	for(int i = 0; i < m_NumSparks; i++)
	{
		const CSpark &Spark = m_aSparks[i];
		const float Progress = Spark.m_Life / Spark.m_LifeSpan;
		const float Size = mix(Spark.m_StartSize, Spark.m_EndSize, Progress);
		const float Alpha = mix(Spark.m_StartAlpha, Spark.m_EndAlpha, Progress) * std::min(1.0f, Spark.m_Life / 0.05f);
		const float Speed = length(Spark.m_Vel);

		const ColorRGBA Color = Spark.m_ShiftColor ? FgfGlow::Blend(Spark.m_Color, Spark.m_EndColor, Progress) : Spark.m_Color;
		pGraphics->SetColor(Color.WithAlpha(Alpha * AlphaScale));
		pGraphics->QuadsSetRotation(Speed > 0.01f ? angle(Spark.m_Vel) : 0.0f);
		IGraphics::CQuadItem Quad(Spark.m_Pos.x, Spark.m_Pos.y, Size + Speed * Spark.m_Stretch, Size);
		pGraphics->QuadsDraw(&Quad, 1);
	}
	pGraphics->QuadsEnd();
	pGraphics->QuadsSetRotation(0.0f);
}
