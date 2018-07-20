#pragma pack_matrix(row_major)

cbuffer PerFrameConstants : register(b0) {
    float4x4 clip_from_view;
    float4x4 view_from_world;
    float4x4 world_from_view;
    float4 color;
}

struct VsInput {
    float3 pos_os   : POSITION;
    float3 normal_os: NORMAL;
    float2 uv       : UV;
};

struct VsOutput {
    float4 pos_cs   : SV_POSITION;
    float3 pos_vs   : POSITION_VS;
    float3 normal_ws: NORMAL_WS;
    float2 uv       : TEXCOORD;
};

VsOutput vs_main(VsInput input) {
    
    VsOutput result = (VsOutput) 0;
    
    float3 pos_vs = mul(view_from_world, float4(input.pos_os, 1.0));// ASSUMPTION(cerlet): object space = world_space

    result.pos_vs = pos_vs;
    result.pos_cs = mul(clip_from_view, float4(pos_vs, 1.0));
    result.normal_ws = normalize(input.normal_os);
    result.uv = input.uv;

    return result;
}

