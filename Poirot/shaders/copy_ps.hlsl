struct PsInput {
    float4 pos_ss       : SV_POSITION;
    float2 uv           : TEXCOORD0;
    float3 view_ray_ws  : VIEW_RAY_WS;
};

struct PsOutput {
    float4 color : SV_TARGET;
};

Texture2D hdr_buffer : register(t0, space0);
SamplerState trilinear_clamp : register(s0);

PsOutput ps_main(PsInput input) {
    PsOutput result = (PsOutput) 0;

    float3 radiance = hdr_buffer.Sample(trilinear_clamp, input.uv).rgb;
    // Gamma correction
    result.color = float4(pow(radiance, 1.0 / 2.2), 1.0);
    return result;
}