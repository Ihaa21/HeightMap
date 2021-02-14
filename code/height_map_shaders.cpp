#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier: enable

#include "descriptor_layouts.cpp"
#include "blinn_phong_lighting.cpp"

GBUFFER_DESCRIPTOR_LAYOUT(0)
SCENE_DESCRIPTOR_LAYOUT(1)

#include "gbuffer_functions.cpp"

layout(set = 2, binding = 0) uniform height_map_inputs
{
    mat4 WVPTransform;
    mat4 WVTransform;
    mat4 ShadowTransform;
    ivec2 MousePixelCoord;
    uint MaterialId;
    uint ColorId;
    uint Width;
    uint Height;
} HeightMapInputs;

layout(set = 2, binding = 1) uniform sampler2D HeightMap;

layout(set = 2, binding = 2) buffer height_map_readback
{
    vec3 ViewIntersectPos;
} HeightMapReadBack;

void HeightMapGetPosAndUv(uint VertexIndex, inout vec3 Pos, inout vec2 Uv)
{
    uvec2 VertexId;
    VertexId.y = VertexIndex / HeightMapInputs.Height;
    VertexId.x = VertexIndex - VertexId.y * HeightMapInputs.Height;

    Uv = vec2(VertexId) / vec2((HeightMapInputs.Width-1), (HeightMapInputs.Height-1));
    // NOTE: Out pos is between -0.5, 0.5 for x,y. Z is relative to that
    Pos.xy = 0.5f * (2.0 * Uv - vec2(1));
    Pos.z = texture(HeightMap, Uv).x;
}

//
// NOTE: GBuffer Height Map
//

#if GBUFFER_VERT

layout(location = 0) out vec3 OutViewPos;
layout(location = 1) out vec3 OutViewNormal;
layout(location = 2) out vec2 OutUv;
layout(location = 3) out flat uint OutMaterialId;
layout(location = 4) out flat uint OutColorId;

void main()
{
    // NOTE: Generate position and uv
    vec3 Pos;
    vec2 Uv;
    HeightMapGetPosAndUv(gl_VertexIndex, Pos, Uv);
    
    // NOTE: Calculate normal
    float HeightX1 = textureOffset(HeightMap, Uv, ivec2(-1, 0)).x;
    float HeightX2 = textureOffset(HeightMap, Uv, ivec2(1, 0)).x;
    float HeightY1 = textureOffset(HeightMap, Uv, ivec2(0, -1)).x;
    float HeightY2 = textureOffset(HeightMap, Uv, ivec2(0, 1)).x;

    vec3 Dir0 = normalize(vec3(2.0f / float(HeightMapInputs.Width), 0, HeightX2 - HeightX1));
    vec3 Dir1 = normalize(vec3(0, 2.0f / float(HeightMapInputs.Height), HeightY2 - HeightY1));
    vec3 Normal = normalize(cross(Dir0, Dir1));
    
    gl_Position = HeightMapInputs.WVPTransform * vec4(Pos, 1);
    OutViewPos = (HeightMapInputs.WVTransform * vec4(Pos, 1)).xyz;
    OutViewNormal = normalize((HeightMapInputs.WVTransform * vec4(Normal, 0)).xyz);
    OutUv = Uv;
    OutMaterialId = HeightMapInputs.MaterialId;
    OutColorId = HeightMapInputs.ColorId;
}

#endif

#if GBUFFER_FRAG

layout(location = 0) in vec3 InViewPos;
layout(location = 1) in vec3 InViewNormal;
layout(location = 2) in vec2 InUv;
layout(location = 3) in flat uint InMaterialId;
layout(location = 4) in flat uint InColorId;

layout(location = 0) out vec4 OutViewPos;
layout(location = 1) out vec4 OutViewNormal;
layout(location = 2) out vec4 OutMaterial;

void main()
{
    ivec2 PixelCoord = ivec2(gl_FragCoord.xy);
    if (HeightMapInputs.MousePixelCoord.x == PixelCoord.x &&
        HeightMapInputs.MousePixelCoord.y == PixelCoord.y)
    {
        HeightMapReadBack.ViewIntersectPos = InViewPos;
    }
    
    // TODO: We are sampling color twice, once here for discard and once in lighting
    // NOTE: Check if we have to discard
    vec4 Color = texture(ColorTextures[InColorId], InUv);
    if (Color.a == 0)
    {
        discard;
    }
    
    OutViewPos = vec4(InViewPos, 0);
    OutViewNormal = vec4(normalize(InViewNormal), 0);
    OutMaterial = GBufferGenerateMaterial(InColorId, InUv, InMaterialId);
}

#endif

//
// NOTE: Shadow Vert
//

#if SHADOW_VERT

layout(location = 0) out float OutDepth;

void main()
{
    vec3 Pos;
    vec2 Uv;
    HeightMapGetPosAndUv(gl_VertexIndex, Pos, Uv);
    
    vec4 Position = HeightMapInputs.ShadowTransform * vec4(Pos, 1);
    gl_Position = Position;
    OutDepth = Position.z;
}

#endif
