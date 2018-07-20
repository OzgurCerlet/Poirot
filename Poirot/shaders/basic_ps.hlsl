#pragma pack_matrix(row_major)
cbuffer PerFrameConstants : register(b0) {
    float4x4 clip_from_view;
    float4x4 view_from_world;
    float4x4 world_from_view;
    float4 color;
}

struct PsInput {
    float4 pos_ss   : SV_POSITION;
    float3 pos_vs   : POSITION_VS;
    float3 normal_ws: NORMAL_WS;
    float2 uv       : TEXCOORD;
};

struct PsOutput {
    float4 color    : SV_TARGET;
};

TextureCube env_map : register(t0);
Texture2D albedo    : register(t1);

SamplerState trilinear_wrap : register(s0);

PsOutput ps_main(PsInput input) {
    
    PsOutput result = (PsOutput) 0;
    
    float3 dir_vs = normalize(input.pos_vs);
    float3 dir_ws = normalize(mul(world_from_view, float4(dir_vs, 0.0)));
    float3 normal_ws = normalize(input.normal_ws);
    float3 reflected_ws = normalize(reflect(dir_ws, normal_ws));
    
    //float3 radiance = env_map.Sample(trilinear_wrap, reflected_ws).rgb *0.8;
    float3 radiance = albedo.Sample(trilinear_wrap, input.uv).rgb;
    float exposure = 1.0;
    float3 color = pow((float3) 1.0 - exp(-radiance * exposure), (float3) (1.0 / 2.2));

    //color = normal_ws;
    result.color.xyz = color;

    return result;
}