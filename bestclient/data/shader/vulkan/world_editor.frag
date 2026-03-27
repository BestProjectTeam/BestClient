#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform sampler2D gTextureSampler;
layout(binding = 1) uniform sampler2D gTextureSampler2;

layout(push_constant) uniform SWorldEditorParams
{
	vec4 gParamsA;
	vec4 gParamsB;
	vec2 gTexelSize0;
	vec2 gTexelSize1;
} gPush;

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 FragClr;

float luminance(vec3 Color)
{
	return dot(Color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 sampleBloomOnePass(vec2 Uv)
{
	float Threshold = mix(0.2, 1.2, clamp(gPush.gParamsB.y, 0.0, 1.0));
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
		vec3 Sample = texture(gTextureSampler, Uv + aOffsets[i] * gPush.gTexelSize0 * 2.5).rgb;
		float Bright = max(luminance(Sample) - Threshold, 0.0);
		Bloom += Sample * Bright;
	}
	return Bloom / 8.0;
}

void main()
{
	vec4 Src0 = texture(gTextureSampler, texCoord);
	vec4 Src1 = texture(gTextureSampler2, texCoord);
	vec3 BaseColor = Src0.rgb;

	if(gPush.gParamsA.z > 0.0)
	{
		float Strength = clamp(gPush.gParamsA.x, 0.0, 1.0);
		float Response = clamp(gPush.gParamsA.y, 0.0, 1.0);
		float Delta = abs(luminance(Src0.rgb) - luminance(Src1.rgb));
		float HistoryWeight = Strength * mix(0.15, 0.85, Response) * (1.0 - clamp(Delta * 4.0, 0.0, 1.0));
		BaseColor = mix(BaseColor, Src1.rgb, HistoryWeight);
	}

	if(gPush.gParamsA.w > 0.0)
	{
		float BloomIntensity = mix(0.0, 2.0, clamp(gPush.gParamsA.w, 0.0, 1.0));
		BaseColor += sampleBloomOnePass(texCoord) * BloomIntensity;
	}

	if(gPush.gParamsB.z > 0.0)
	{
		float Intensity = mix(0.0, 2.0, clamp(gPush.gParamsB.z, 0.0, 1.0));
		float Threshold = mix(0.01, 0.35, clamp(gPush.gParamsB.w, 0.0, 1.0));
		float LumaC = luminance(Src0.rgb);
		float LumaL = luminance(texture(gTextureSampler, texCoord - vec2(gPush.gTexelSize0.x, 0.0)).rgb);
		float LumaR = luminance(texture(gTextureSampler, texCoord + vec2(gPush.gTexelSize0.x, 0.0)).rgb);
		float LumaU = luminance(texture(gTextureSampler, texCoord - vec2(0.0, gPush.gTexelSize0.y)).rgb);
		float LumaD = luminance(texture(gTextureSampler, texCoord + vec2(0.0, gPush.gTexelSize0.y)).rgb);
		float Edge = abs(LumaL - LumaR) + abs(LumaU - LumaD) + abs(LumaC - 0.25 * (LumaL + LumaR + LumaU + LumaD));
		float Mask = smoothstep(Threshold, Threshold * 2.5, Edge) * Intensity;
		BaseColor = mix(BaseColor, vec3(0.0), clamp(Mask, 0.0, 1.0));
	}

	FragClr = vec4(BaseColor, Src0.a);
}
