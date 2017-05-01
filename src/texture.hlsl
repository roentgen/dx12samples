/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
struct PSInput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

Texture2D tex : register(t0);
SamplerState ssampler : register(s0);

cbuffer cb0 : register(b0)
{
float4x4 worldmat;
}

PSInput VSMain(float3 position : POSITION, float2 uv : TEXCOORD)
{
    PSInput result;
    result.position = mul(float4(position, 1), worldmat);
    result.position.w = 1.f;
    //result.position = float4(position, 1);
    result.uv = uv;//float2(0.5, 0.5);

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return tex.Sample(ssampler, input.uv);
}
