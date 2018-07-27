struct PsInput {
    float4 pos_ss       : SV_POSITION;
    float2 uv           : TEXCOORD0;
    float3 view_ray_ws  : VIEW_RAY_WS;
};

struct PsOutput {
    float4 color : SV_TARGET;
};

TextureCube env_map_radiance : register(t1, space0);
SamplerState trilinear_clamp : register(s0);

PsOutput ps_main(PsInput input)
{
    PsOutput result = (PsOutput) 0;
    float3 radiance = env_map_radiance.Sample(trilinear_clamp, normalize(input.view_ray_ws)).rgb;
    // Gamma correction
    result.color = float4(radiance, 1.0);
    return result;
}