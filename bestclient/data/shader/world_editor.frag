uniform sampler2D gTextureSampler;
uniform sampler2D gTextureSampler2;
uniform float gParams[8];
uniform vec2 gTexelSize0;
uniform vec2 gTexelSize1;
uniform int gPassIndex;

in vec2 texCoord;
out vec4 FragClr;

float luminance(vec3 Color)
{
	return dot(Color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 sampleBlur5(sampler2D Sampler, vec2 Uv, vec2 Step)
{
	vec3 Color = texture(Sampler, Uv).rgb * 0.227027;
	Color += texture(Sampler, Uv + Step * 1.384615).rgb * 0.316216;
	Color += texture(Sampler, Uv - Step * 1.384615).rgb * 0.316216;
	Color += texture(Sampler, Uv + Step * 3.230769).rgb * 0.070270;
	Color += texture(Sampler, Uv - Step * 3.230769).rgb * 0.070270;
	return Color;
}

vec3 sampleBloomOnePass(vec2 Uv)
{
	float Threshold = mix(0.2, 1.2, clamp(gParams[4], 0.0, 1.0));
	const vec2 aOffsets[8] = vec2[](
		vec2(1.5, 0.0),
		vec2(-1.5, 0.0),
		vec2(0.0, 1.5),
		vec2(0.0, -1.5),
		vec2(3.0, 3.0),
		vec2(-3.0, 3.0),
		vec2(3.0, -3.0),
		vec2(-3.0, -3.0)
	);
	vec3 Bloom = vec3(0.0);
	for(int i = 0; i < 8; i++)
	{
		vec3 Sample = texture(gTextureSampler, Uv + aOffsets[i] * gTexelSize0 * 2.5).rgb;
		float Bright = max(luminance(Sample) - Threshold, 0.0);
		Bloom += Sample * Bright;
	}
	return Bloom / 8.0;
}

void main()
{
	vec4 Src0 = texture(gTextureSampler, texCoord);
	vec4 Src1 = texture(gTextureSampler2, texCoord);

	if(gPassIndex == 0)
	{
		FragClr = Src0;
	}
	else if(gPassIndex == 1)
	{
		float Strength = clamp(gParams[0], 0.0, 1.0);
		float Response = clamp(gParams[1], 0.0, 1.0);
		float HasHistory = gParams[2];
		float Delta = abs(luminance(Src0.rgb) - luminance(Src1.rgb));
		float HistoryWeight = Strength * mix(0.15, 0.85, Response) * HasHistory * (1.0 - clamp(Delta * 4.0, 0.0, 1.0));
		FragClr = vec4(mix(Src0.rgb, Src1.rgb, HistoryWeight), Src0.a);
	}
	else if(gPassIndex == 2)
	{
		float Threshold = mix(0.2, 1.2, clamp(gParams[0], 0.0, 1.0));
		float Bright = max(luminance(Src0.rgb) - Threshold, 0.0);
		FragClr = vec4(Src0.rgb * Bright, 1.0);
	}
	else if(gPassIndex == 3)
	{
		FragClr = vec4(sampleBlur5(gTextureSampler, texCoord, vec2(gTexelSize0.x, 0.0)), 1.0);
	}
	else if(gPassIndex == 4)
	{
		FragClr = vec4(sampleBlur5(gTextureSampler, texCoord, vec2(0.0, gTexelSize0.y)), 1.0);
	}
	else if(gPassIndex == 5)
	{
		float Intensity = mix(0.0, 2.5, clamp(gParams[0], 0.0, 1.0));
		FragClr = vec4(Src0.rgb + Src1.rgb * Intensity, Src0.a);
	}
	else if(gPassIndex == 6)
	{
		float Intensity = mix(0.0, 2.0, clamp(gParams[0], 0.0, 1.0));
		float Threshold = mix(0.01, 0.35, clamp(gParams[1], 0.0, 1.0));
		float LumaC = luminance(Src0.rgb);
		float LumaL = luminance(texture(gTextureSampler, texCoord - vec2(gTexelSize0.x, 0.0)).rgb);
		float LumaR = luminance(texture(gTextureSampler, texCoord + vec2(gTexelSize0.x, 0.0)).rgb);
		float LumaU = luminance(texture(gTextureSampler, texCoord - vec2(0.0, gTexelSize0.y)).rgb);
		float LumaD = luminance(texture(gTextureSampler, texCoord + vec2(0.0, gTexelSize0.y)).rgb);
		float Edge = abs(LumaL - LumaR) + abs(LumaU - LumaD) + abs(LumaC - 0.25 * (LumaL + LumaR + LumaU + LumaD));
		float Mask = smoothstep(Threshold, Threshold * 2.5, Edge) * Intensity;
		vec3 OutlineColor = vec3(0.0);
		FragClr = vec4(mix(Src0.rgb, OutlineColor, clamp(Mask, 0.0, 1.0)), Src0.a);
	}
	else if(gPassIndex == 7)
	{
		vec3 BaseColor = Src0.rgb;
		if(gParams[2] > 0.0)
		{
			float Strength = clamp(gParams[0], 0.0, 1.0);
			float Response = clamp(gParams[1], 0.0, 1.0);
			float Delta = abs(luminance(Src0.rgb) - luminance(Src1.rgb));
			float HistoryWeight = Strength * mix(0.15, 0.85, Response) * (1.0 - clamp(Delta * 4.0, 0.0, 1.0));
			BaseColor = mix(BaseColor, Src1.rgb, HistoryWeight);
		}
		if(gParams[3] > 0.0)
		{
			float BloomIntensity = mix(0.0, 2.0, clamp(gParams[3], 0.0, 1.0));
			BaseColor += sampleBloomOnePass(texCoord) * BloomIntensity;
		}
		if(gParams[5] > 0.0)
		{
			float Intensity = mix(0.0, 2.0, clamp(gParams[5], 0.0, 1.0));
			float Threshold = mix(0.01, 0.35, clamp(gParams[6], 0.0, 1.0));
			float LumaC = luminance(Src0.rgb);
			float LumaL = luminance(texture(gTextureSampler, texCoord - vec2(gTexelSize0.x, 0.0)).rgb);
			float LumaR = luminance(texture(gTextureSampler, texCoord + vec2(gTexelSize0.x, 0.0)).rgb);
			float LumaU = luminance(texture(gTextureSampler, texCoord - vec2(0.0, gTexelSize0.y)).rgb);
			float LumaD = luminance(texture(gTextureSampler, texCoord + vec2(0.0, gTexelSize0.y)).rgb);
			float Edge = abs(LumaL - LumaR) + abs(LumaU - LumaD) + abs(LumaC - 0.25 * (LumaL + LumaR + LumaU + LumaD));
			float Mask = smoothstep(Threshold, Threshold * 2.5, Edge) * Intensity;
			BaseColor = mix(BaseColor, vec3(0.0), clamp(Mask, 0.0, 1.0));
		}
		FragClr = vec4(BaseColor, Src0.a);
	}
	else
	{
		FragClr = Src0;
	}
}
