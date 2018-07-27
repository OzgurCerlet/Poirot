
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

TextureCube env_map_radiance    : register(t0, space1);
TextureCube env_map_irradiance  : register(t1, space1);
TextureCube env_map_specular    : register(t2, space1);
Texture2D env_brdf_lut          : register(t3, space1);

Texture2D a_material_textures[] : register(t0, space2);

SamplerState trilinear_clamp    : register(s0);
SamplerState trilinear_wrap_ai16: register(s1);

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
    //float3x3(tangent_ws * inv_max, bitangent_ws * inv_max, normal_ws) INVESTIGATE the difference!
    return transpose(float3x3(bitangent_ws * inv_max, tangent_ws * inv_max,  normal_ws)); 
}

float3 compute_normal(PsInput input, float3 normal_ws, Texture2D normal_texture) {

#if 1
    
    float3x3 world_from_tangent = cotangent_frame(normal_ws, input.pos_ws, input.uv);

#else

    float3 pos_dx = ddx(input.pos_ws);
    float3 pos_dy = ddy(input.pos_ws);
    float2 tex_dx = ddx(input.uv);
    float2 tex_dy = ddy(input.uv);
    float3 t = (tex_dy.y * pos_dx - tex_dx.y * pos_dy) / (tex_dx.x * tex_dy.y - tex_dy.x * tex_dx.y);
    t = normalize(t - normal_ws * dot(normal_ws, t));
    float3 b = normalize(cross(normal_ws, t));

    float3x3 world_from_tangent = transpose(float3x3(b, t, normal_ws));
#endif
    
    float3 normal_ts = normal_texture.Sample(trilinear_wrap_ai16, input.uv).rgb * 2.0 - 1.0;
    normal_ws = normalize(mul(world_from_tangent, normal_ts));
    
    return normal_ws;
}

// From http://filmicgames.com/archives/75
float3 tone_map_uncharted_2(float3 x) {
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float4 tone_map(float4 color) {
    float3 out_color = tone_map_uncharted_2(color.rgb * 4.5);
    out_color = out_color * (1.0f / tone_map_uncharted_2(11.2f));
    return float4(out_color.rgb, color.a);
}

// From
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
    else {
        metallic = clamp(metallic, 0.0, 1.0);
        roughness = clamp(roughness, k_min_roughness, 1.0);
    }

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
    
    float lod = roughness * 10.0;
	// retrieve a scale and bias to F0. See [1], Figure 3
    float2 brdf = env_brdf_lut.Sample(trilinear_clamp, float2(NdotV, 1.0 - roughness)).xy;
    float3 diffuseLight = env_map_irradiance.Sample(trilinear_clamp, normal_ws).rgb;
    float3 specularLight = env_map_specular.SampleLevel(trilinear_clamp, reflected_ws, lod).rgb;

    float3 diffuse = diffuseLight * diffuse_color;
    float3 specular = specularLight * (specular_color * brdf.x + brdf.y);

    float3 color = 0;
    color += diffuse + specular;

    if (mat_data.emissive_texture_index >= 0) {
        float3 emission = a_material_textures[mat_data.emissive_texture_index].Sample(trilinear_wrap_ai16, input.uv).rgb;
        color += emission;
    }

    // // Gamma correction
    //result.color = float4(pow(color.rgb, 1.0 / 2.2), base_color.a);
    result.color = float4(color.rgb, base_color.a);
    return result;
}
