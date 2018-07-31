struct PsInput {
    float4 pos_ss       : SV_POSITION;
    float2 uv           : TEXCOORD0;
    float3 view_ray_ws  : VIEW_RAY_WS;
};

struct PsOutput {
    float4 color : SV_TARGET;
};

cbuffer PerDrawConstants : register(b2) {
    uint background_index;
    uint mip_level;
}

TextureCube env_map_radiance    : register(t1, space1);
TextureCube env_map_irradiance  : register(t2, space1);
TextureCube env_map_specular    : register(t3, space1);

SamplerState trilinear_clamp    : register(s0);

PsOutput ps_main(PsInput input)
{
    PsOutput result = (PsOutput) 0;
    float3 radiance = 0;
    float3 view_ray_ws = normalize(input.view_ray_ws);
    if (background_index == 0) {
        radiance = env_map_radiance.Sample(trilinear_clamp, view_ray_ws).rgb;
    }
    else if (background_index == 1) {
        radiance = env_map_irradiance.Sample(trilinear_clamp, view_ray_ws).rgb;
    }
    else {
        radiance = env_map_specular.SampleLevel(trilinear_clamp, view_ray_ws, mip_level).rgb;
    }

    // Gamma correction
    result.color = float4(radiance, 1.0);
    return result;
}