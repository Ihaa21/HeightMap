
//
// NOTE: Scene
//

#include "shader_light_types.cpp"

struct instance_entry
{
    mat4 WVTransform;
    mat4 WVPTransform;
    uint ColorId;
    uint MaterialId;
};

struct material
{
    // TODO: Add all lighting parameters here
    vec4 TintColor;
    float SpecularPower;
};

#define SCENE_DESCRIPTOR_LAYOUT(set_number)                             \
    layout(set = set_number, binding = 0) uniform scene_buffer          \
    {                                                                   \
        vec3 CameraPos;                                                 \
        uint NumPointLights;                                            \
        mat4 VPTransform;                                               \
        mat4 VTransform;                                                \
        mat4 PInverse;                                                  \
        vec2 Resolution;                                                \
    } SceneBuffer;                                                      \
                                                                        \
    layout(set = set_number, binding = 1) uniform sampler2D ColorTextures[128]; \
                                                                        \
    layout(set = set_number, binding = 2) buffer material_buffer        \
    {                                                                   \
        material MaterialBuffer[];                                      \
    };                                                                  \
                                                                        \
    layout(set = set_number, binding = 3) buffer instance_buffer        \
    {                                                                   \
        instance_entry InstanceBuffer[];                                \
    };                                                                  \
                                                                        \
    layout(set = set_number, binding = 4) buffer point_light_buffer     \
    {                                                                   \
        point_light PointLights[];                                      \
    };                                                                  \
                                                                        \
    layout(set = set_number, binding = 5) buffer directional_light_buffer \
    {                                                                   \
        directional_light DirectionalLight;                             \
    };                                                                  \
                                                                        \
    layout(set = set_number, binding = 6) buffer shadow_transforms      \
    {                                                                   \
        mat4 DirectionalTransforms[];                                   \
    };                                                                  \
    

//
// NOTE: GBuffer
//

struct plane
{
    vec3 Normal;
    float Distance;
};

struct frustum
{
    // NOTE: Left, Right, Top, Bottom
    plane Planes[4];
};

plane PlaneCreate(vec3 P0, vec3 P1, vec3 P2)
{
    plane Result;

    vec3 V0 = P1 - P0;
    vec3 V1 = P2 - P0;
    Result.Normal = normalize(cross(V0, V1));
    Result.Distance = dot(Result.Normal, P0);
    
    return Result;
}

bool SphereInsidePlane(vec3 SphereCenter, float SphereRadius, plane Plane)
{
    bool Result = dot(Plane.Normal, SphereCenter) - Plane.Distance < -SphereRadius;
    return Result;
}

bool SphereInsideFrustum(vec3 SphereCenter, float SphereRadius, frustum Frustum, float NearZ, float FarZ)
{
    bool Result = true;

    if (SphereCenter.z + SphereRadius < NearZ || SphereCenter.z - SphereRadius > FarZ)
    {
        Result = false;
    }

    for (int PlaneId = 0; PlaneId < 4; ++PlaneId)
    {
        if (SphereInsidePlane(SphereCenter, SphereRadius, Frustum.Planes[PlaneId]))
        {
            Result = false;
        }
    }
    
    return Result;
}

vec4 ClipToView(mat4 InverseProjection, vec4 ClipPos)
{
    vec4 Result = InverseProjection * ClipPos;
    Result = Result / Result.w;
    return Result;
}

vec4 ScreenToView(mat4 InverseProjection, vec2 ScreenSize, vec4 ScreenPos)
{
    vec2 Ndc = 2.0f * (ScreenPos.xy / ScreenSize) - vec2(1.0f);
    vec4 Result = ClipToView(InverseProjection, vec4(Ndc, ScreenPos.zw));
    return Result;
}

#define GBUFFER_DESCRIPTOR_LAYOUT(set_number)                           \
    layout(set = set_number, binding = 0) uniform tiled_deferred_globals \
    {                                                                   \
        mat4 InverseProjection;                                         \
        vec2 ScreenSize;                                                \
        uvec2 GridSize;                                                 \
    };                                                                  \
                                                                        \
    layout(set = set_number, binding = 1) buffer grid_frustums          \
    {                                                                   \
        frustum GridFrustums[];                                         \
    };                                                                  \
                                                                        \
    layout(set = set_number, binding = 2, rg32ui) uniform uimage2D LightGrid_O; \
    layout(set = set_number, binding = 3) buffer light_index_list_opaque \
    {                                                                   \
        uint LightIndexList_O[];                                        \
    };                                                                  \
                                                                        \
    layout(set = set_number, binding = 4) buffer light_index_counter_opaque \
    {                                                                   \
        uint LightIndexCounter_O;                                       \
    };                                                                  \
                                                                        \
    layout(set = set_number, binding = 5, rg32ui) uniform uimage2D LightGrid_T; \
    layout(set = set_number, binding = 6) buffer light_index_list_transparent \
    {                                                                   \
        uint LightIndexList_T[];                                        \
    };                                                                  \
                                                                        \
    layout(set = set_number, binding = 7) buffer light_index_counter_transparent \
    {                                                                   \
        uint LightIndexCounter_T;                                       \
    };                                                                  \
                                                                        \
    layout(set = set_number, binding = 8) uniform sampler2D GBufferPositionTexture; \
    layout(set = set_number, binding = 9) uniform sampler2D GBufferNormalTexture; \
    layout(set = set_number, binding = 10) uniform sampler2D GBufferMaterialTexture; \
    layout(set = set_number, binding = 11) uniform sampler2D GBufferDepthTexture; \
    layout(set = set_number, binding = 12) uniform sampler2D DirectionalShadow; \
    
//
// NOTE: Grass Indirect Arguments
//

struct indirect_args
{
    uint NumVerticesPerInstance;
    uint NumInstances;
    uint StartVertexIndex;
    uint StartInstanceIndex;
};
