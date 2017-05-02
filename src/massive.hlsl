/**
 * (C) roentgen 
 * this code is licensed under the MIT License.
 */
static float3 lightpos = {-2.f, 5.f, 0.0f};

Texture2D< float > shadow_texture : register(t0);
Texture2D color_texture : register(t1);

SamplerState ssampler : register(s0);

cbuffer cb : register(b0)
{
uint idx_col;
uint idx_row;
uint lr;
}

cbuffer cb1 : register(b1)
{
float4x4 viewmat;
}

cbuffer cb2 : register(b3)
{
float4x4 proj        ;// : packoffset(c0);
float4x4 lightviewmat;// : packoffset(c16);
float4x4 lightproj   ;// : packoffset(c32);
}

cbuffer cb0 : register(b4)
{
float4x4 modelmat[1024];
}

/* Shadow Pass */

struct PSShadowPassInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
    float4 pos : PROJ_POS;
};

PSShadowPassInput VSShadowPassMain(float3 position : POSITION, float2 uv : TEXCOORD, float3 nrm : NORMAL)
{
    PSShadowPassInput result;
    uint i = idx_row * 10 + idx_col;
    result.position = mul(float4(position, 1), modelmat[i]);
    result.position = mul(result.position, transpose(lightviewmat));
    
    result.position = mul(result.position, transpose(lightproj));
    //result.position = float4(position, 1);
    result.uv = uv;
    result.pos = result.position;
    return result;
}

float4 PSShadowPassMain(PSShadowPassInput input) : SV_TARGET
{
    return float4(input.pos.z, 1.0f, input.pos.z, 1.f);
}

/* Scene Pass */

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
    float3 norm : NORMAL;
    float4 worldposition : POSITION;
};

PSInput VSMain(float3 position : POSITION, float2 uv : TEXCOORD, float3 nrm : NORMAL)
{
    PSInput result;

    uint i = idx_row * 10 + idx_col;
    float4x4 m = modelmat[i];
    result.position = mul(float4(position, 1), m);
    result.worldposition = result.position;
    //result.norm = normalize(mul(float4(nrm, 1), m));
    result.norm = mul(nrm, (float3x3)m);
    result.position = mul(result.position, transpose(viewmat));
    result.position = mul(result.position, transpose(proj));

    /* for defined CHECK_DEPTH_BUFFER */
    //result.position = float4(position * 0.8, 1);
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
    float ambient = 0.25f;
    float k = (depth > (pos_shadowcoord.z - 0.001f)) ? 1.0f : 0.f;

    float f = saturate(dot(input.norm, normalize(lightpos - input.worldposition)));
    return float4(float3(f, f, f) * k + float3(ambient, ambient, ambient), 1);
}
