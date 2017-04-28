/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD;
	float4 color : COLOR;
};

Texture2D tex : register(t0);
SamplerState ssampler : register(s0);

cbuffer cb0 : register(b0)
{
float4x4 worldmat;
}

cbuffer cb1 : register(b1) {
int coveralpha;
}

PSInput VSMain(float3 position : POSITION, float2 uv : TEXCOORD)
{
	PSInput result;
	result.position = mul(float4(position, 1), worldmat);
	result.position.w = 1.f;
	result.color = float4(1, 1, 1, ((float)coveralpha) / 255.f);
	//result.position = float4(position, 1);
	result.uv = uv;
	return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float4 p = tex.Sample(ssampler, input.uv);
	p.a *= input.color.a;
	return p;
}
