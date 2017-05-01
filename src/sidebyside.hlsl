/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
struct PSInput {
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD;
};

Texture2D tex[4] : register(t0); /* 0:left-eye 1:right-eye*/
SamplerState ssampler : register(s0);

cbuffer cb_index : register(b0)
{
uint lr;
uint idx;
}

cbuffer cb0 : register(b1)
{
float4x4 worldmat[2];
}

PSInput VSMain(float3 position : POSITION, float2 uv : TEXCOORD)
{
	PSInput result;
	result.position = mul(float4(position, 1), worldmat[lr]);
	result.position = result.position / result.position.w;
	//result.position = float4(position, 1);
	result.uv = uv;
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	return tex[lr * 2 + idx].Sample(ssampler, input.uv);
}
