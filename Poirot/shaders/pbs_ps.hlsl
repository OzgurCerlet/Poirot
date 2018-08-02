
cbuffer PerFrameConstants : register(b0) {
    float4x4 clip_from_view;
    float4x4 view_from_clip;
    float4x4 view_from_world;
    float4x4 world_from_view;
    float3 cam_pos_ws;
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
    uint isolation_mode_index;
    uint test_factor;
}

struct PsInput {
    float4 pos_ss   : SV_POSITION;
    float3 pos_vs   : POSITION_VS;
    float3 pos_ws   : POSITION_WS;
    float3 normal_ws: NORMAL_WS;
    float2 uv       : TEXCOORD;
};

struct PsOutput {
    float4 color    : SV_TARGET;
};

Texture2D env_brdf_lut          : register(t0, space1);
TextureCube env_map_irradiance  : register(t2, space1);
TextureCube env_map_specular    : register(t3, space1);

Texture2D a_material_textures[] : register(t0, space2);

SamplerState trilinear_clamp    : register(s0);
SamplerState trilinear_wrap_ai16: register(s1);

static const float k_min_roughness = 0.04;

// See http://www.thetenthplanet.de/archives/1180
float3x3 cotangent_frame(float3 normal_ws, float3 pos_ws, float2 uv) {
    // get edge vectors of the pixel triangle
    float3 dp1 = ddx_fine(pos_ws);
    float3 dp2 = ddy_fine(pos_ws);
    float2 duv1 = ddx_fine(uv);
    float2 duv2 = ddy_fine(uv);
 
    // solve the linear system
    float3 dp2perp = cross(dp2, normal_ws);
    float3 dp1perp = cross(normal_ws, dp1);
    float3 tangent_ws = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 bitangent_ws = dp2perp * duv1.y + dp1perp * duv2.y;
 
    // construct a scale-invariant frame 
    float inv_max = 1.0 / sqrt(max(dot(tangent_ws, tangent_ws), dot(bitangent_ws, bitangent_ws)));
    //float3x3(tangent_ws * inv_max, bitangent_ws * inv_max, normal_ws) INVESTIGATE the difference!
    return transpose(float3x3(bitangent_ws * inv_max, tangent_ws * inv_max,  normal_ws));
}

float3 compute_normal(PsInput input, float3 normal_ws, Texture2D normal_texture) {
    float3x3 world_from_tangent = cotangent_frame(normal_ws, input.pos_ws, input.uv); 
    float3 normal_ts = normalize(normal_texture.Sample(trilinear_wrap_ai16, input.uv).rgb * 2.0 - 1.0);
    normal_ws = normalize(mul(world_from_tangent, normal_ts));
    
    return normal_ws;
}


float3 f_schlick_roughness(float cos_theta, float3 F0, float roughness) {
    return F0 + ( max((float3)(1.0 - roughness), F0) - F0 ) * pow(1.0 - cos_theta, 5.0);
}

float3 specular_reflection(float reflectance0, float reflectance90, float VdotH) {
    return reflectance0 + (reflectance90 - reflectance0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
}

PsOutput ps_main(PsInput input) {
    
    PsOutput result = (PsOutput) 0;
    MaterialData mat_data = a_material_data[material_index];
    
    float4 base_color = mat_data.base_color_factor;
    if (mat_data.base_color_texture_index >= 0) {
        base_color *= a_material_textures[mat_data.base_color_texture_index].Sample(trilinear_wrap_ai16, input.uv);
    }

    float metallic = mat_data.metallic_factor;
    float roughness = mat_data.roughness_factor;
    if (mat_data.metallic_roughness_texture_index >= 0) {
        float2 metallic_roughness = a_material_textures[mat_data.metallic_roughness_texture_index].Sample(trilinear_wrap_ai16, input.uv).bg; // WARNING! it is called metallicRoughnes texture but mapped to out of order channels: roughness -> g, metallic -> b!
        metallic *= metallic_roughness.x;
        roughness *= metallic_roughness.y;
    }
    metallic = clamp(metallic, 0.0, 1.0);
    roughness = clamp(roughness, k_min_roughness, 1.0);

    float3 normal_ws = normalize(input.normal_ws);
    if (mat_data.normal_texture_index >= 0) {
        normal_ws = compute_normal(input, normal_ws, a_material_textures[mat_data.normal_texture_index]);  
    }

    float3 f0 = 0.04;
    float3 diffuse_color = base_color.rgb * (1.0 - f0);
    diffuse_color *=  1.0 - metallic;
    float3 specular_color = lerp(f0, base_color.rgb, metallic);

    float3 view_ws = normalize(cam_pos_ws - input.pos_ws);
    float3 reflected_ws = -normalize(reflect(view_ws, normal_ws));
    float NdotV = clamp(abs(dot(normal_ws, view_ws)), 0.001, 1.0);

    float2 brdf = env_brdf_lut.Sample(trilinear_clamp, float2(NdotV, 1.0 - roughness)).xy;
    float3 diffuse_irradiance = env_map_irradiance.Sample(trilinear_clamp, normal_ws).rgb;
    
    uint mip_level = 0, width = 0, height = 0, mip_count = 0;
    env_map_specular.GetDimensions(mip_level, width, height, mip_count);
    float3 specular_irradiance = env_map_specular.SampleLevel(trilinear_clamp, reflected_ws, roughness * (mip_count - 1)).rgb;

    float3 diffuse = diffuse_irradiance * diffuse_color;
    float3 specular = specular_irradiance * (specular_color * brdf.x + brdf.y);

    float3 color = diffuse + specular;

    float3 emission = 0;
    if (mat_data.emissive_texture_index >= 0) {
        emission = a_material_textures[mat_data.emissive_texture_index].Sample(trilinear_wrap_ai16, input.uv).rgb;
        color += emission;
    }
 
    result.color = float4(color.rgb, base_color.a);
    
    switch (isolation_mode_index) {
        case 1: result.color = base_color; break;
        case 2: result.color = (float4) metallic; break;
        case 3: result.color = (float4) roughness; break;
        case 4: result.color = float4(normal_ws, 1.0) * 0.5 + 0.5; break;
        case 5: result.color = (float4) base_color.a; break;
        case 6: result.color = float4(emission, 1.0); break;
        case 7: result.color = float4(diffuse, 1.0); break;
        case 8: result.color = float4(specular, 1.0); break;
        default: break;
    }
        
    return result;
}
