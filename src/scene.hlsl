/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD;
	float3 norm : NORMAL;
	float4 worldposition : POSITION;
};

static float3 lightpos = {-2.f, 5.f, 0.0f};

Texture2D< float > shadow_texture : register(t0);
Texture2D color_texture : register(t1);
SamplerState ssampler : register(s0);

cbuffer cbg : register(b0)
{
float4x4 modelmat;
}

cbuffer cb1 : register(b9)
{
float4x4 viewmat;
}

cbuffer cb2 : register(b10)
{
float4x4 proj;
}

cbuffer cb3 : register(b11)
{
float4x4 lightviewmat;
}

cbuffer cb4 : register(b12)
{
float4x4 lightproj;
}

PSInput VSMain(float3 position : POSITION, float2 uv : TEXCOORD, float3 nrm : NORMAL)
{
	PSInput result;
	
	result.position = mul(float4(position, 1), modelmat);
	result.worldposition = result.position;
	result.norm = normalize(mul(float4(nrm, 1), modelmat));
	result.position = mul(result.position, transpose(viewmat));
	result.position = mul(result.position, transpose(proj));
	result.position = result.position / result.position.w;
	/* for defined CHECK_DEPTH_BUFFER */
	//result.position = float4(position, 1);
	result.uv = uv;
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 pos_shadowcoord = input.worldposition;
	pos_shadowcoord = mul(pos_shadowcoord, transpose(lightviewmat));
	pos_shadowcoord = mul(pos_shadowcoord, transpose(lightproj));
	pos_shadowcoord = pos_shadowcoord / pos_shadowcoord.w;

	float2 suv = pos_shadowcoord.xy;
	suv = suv * 0.5f + float2(0.5f, 0.5f); // lightspace-proj(-1,-1,0)-(1,1,1) remap to UV(0,0)-(1,1)
	suv.y = 1.f - suv.y;
	
	/* if defined CHECK_DEPTH_BUFFER */
	//suv = input.uv;

	float depth = shadow_texture.Sample(ssampler, suv);
	float k = (depth > (pos_shadowcoord.z - 0.001f)) ? 1.0f : 0.f;

	float4 col = color_texture.Sample(ssampler, input.uv);
	float f = dot(input.norm, normalize(lightpos - input.worldposition));
	return float4(col.xyz * f * (k + 0.2f), 1);
}
