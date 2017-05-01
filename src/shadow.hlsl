/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
    float4 pos : PROJ_POS;
};

Texture2D< float > shadow_texture : register(t0);
Texture2D color_texture : register(t1);
SamplerState ssampler : register(s0);
cbuffer cb0 : register(b0)
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
    result.position = mul(result.position, transpose(lightviewmat));
    
    result.position = mul(result.position, transpose(lightproj));
    result.position = result.position / result.position.w;
    //result.pos = float4(position, 1);

    //result.position = float4(position, 1);
    result.uv = uv;
    result.pos = result.position;
    return result;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return float4(input.pos.z, 1.0f, input.pos.z, 1.f);
}

