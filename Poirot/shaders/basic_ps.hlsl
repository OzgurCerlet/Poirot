#pragma pack_matrix(row_major)

cbuffer PerFrameConstants : register(b0) {
    float4x4 clip_from_view;
    float4x4 view_from_world;
    float4x4 world_from_view;
    float4 color;
}

struct MaterialData{
    float4 base_color_factor;
    float metallic_factor;
    float roughness_factor;
    float alpha_mask_cutoff;
    int base_color_texture_index;
    int normal_texture_index;
    int metallic_roughness_texture_index;
    int emissive_texture_index;
    int occlusion_texture_index;
    int is_alpha_masked;
};

cbuffer MaterialDataCB : register(b1) {
    MaterialData a_material_data[32];
}

cbuffer PerDrawConstants : register(b2) {
    uint transform_index;
    uint material_index;
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

//TextureCube env_map : register(t0);
#define MAX_NUM_MATERIAL_TEXTURES 32
Texture2D a_material_textures[MAX_NUM_MATERIAL_TEXTURES] : register(t0);
TextureCube env_map : register(t1);

SamplerState trilinear_wrap : register(s0);

static const float k_min_roughness = 0.04;

// See http://www.thetenthplanet.de/archives/1180
float3x3 cotangent_frame(float3 normal_ws, float3 pos_ws, float2 uv) {
    // get edge vectors of the pixel triangle
    float3 dp1 = ddx(pos_ws);
    float3 dp2 = ddy(pos_ws);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);
 
    // solve the linear system
    float3 dp2perp = cross(dp2, normal_ws);
    float3 dp1perp = cross(normal_ws, dp1);
    float3 tangent_ws = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 bitangent_ws = dp2perp * duv1.y + dp1perp * duv2.y;
 
    // construct a scale-invariant frame 
    float inv_max = 1.0 / sqrt(max(dot(tangent_ws, tangent_ws), dot(bitangent_ws, bitangent_ws)));
    return float3x3(tangent_ws * inv_max, bitangent_ws * inv_max, normal_ws);
}

float3 compute_normal(PsInput input, Texture2D normal_texture) {    
    float3 normal_ws = normalize(input.normal_ws);
    float3 pos_ws = mul(world_from_view, float4(input.pos_vs, 1.0)).xyz;
    float3x3 world_from_tangent = cotangent_frame(normal_ws, pos_ws, input.uv);
    float3 normal_ts = normal_texture.Sample(trilinear_wrap, input.uv).rgb * 2.0 - 1.0;
    normal_ws = normalize(mul(world_from_tangent, normal_ts));

    return normal_ws;
}

PsOutput ps_main(PsInput input) {
    
    PsOutput result = (PsOutput) 0;
    MaterialData mat_data = a_material_data[material_index];
    
    float4 base_color = mat_data.base_color_factor;
    if (mat_data.base_color_texture_index >= 0) {
        base_color *= a_material_textures[mat_data.base_color_texture_index].Sample(trilinear_wrap, input.uv); 
    }

    //float2 metallic_rougness_sample = a_material_textures[metallic_roughness_texture_index].Sample(trilinear_wrap, input.uv).bg * metallic_roughness_factors;
    //float metallic = clamp(metallic_rougness_sample.r, 0.0, 1.0);
    //float perceptual_rougness = clamp(metallic_rougness_sample.g, k_min_roughness, 1.0);
    //float alpha_roughness = perceptual_rougness * perceptual_rougness; 
    //float3 normal_ws = compute_normal(input, a_material_textures[normal_texture_index]);

    result.color = float4(base_color.rgb * base_color.a, base_color.a); // Premultiplied alpha
  
    return result;
}

//float3 dir_vs = normalize(input.pos_vs);
//float3 dir_ws = normalize(mul(world_from_view, float4(dir_vs, 0.0)));
//float3 normal_ws = normalize(input.normal_ws);
//float3 reflected_ws = normalize(reflect(dir_ws, normal_ws)); 
//float3 radiance = env_map.Sample(trilinear_wrap, reflected_ws).rgb *0.8;