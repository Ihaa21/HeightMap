#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier: enable

#include "descriptor_layouts.cpp"
#include "blinn_phong_lighting.cpp"

//
// NOTE: Descriptor Sets
//

#define TILE_DIM_IN_PIXELS 8

GBUFFER_DESCRIPTOR_LAYOUT(0)
SCENE_DESCRIPTOR_LAYOUT(1)

#include "gbuffer_functions.cpp"

//
// NOTE: Grid Frustum Shader
//

#if GRID_FRUSTUM

layout(local_size_x = TILE_DIM_IN_PIXELS, local_size_y = TILE_DIM_IN_PIXELS, local_size_z = 1) in;

void main()
{
    uvec2 GridPos = uvec2(gl_GlobalInvocationID.xy);
    if (GridPos.x < GridSize.x && GridPos.y < GridSize.y)
    {
        // NOTE: Compute four corner points of tile
        vec3 CameraPos = vec3(0);
        vec4 BotLeft = vec4((GridPos + vec2(0, 0)) * vec2(TILE_DIM_IN_PIXELS), 0, 1);
        vec4 BotRight = vec4((GridPos + vec2(1, 0)) * vec2(TILE_DIM_IN_PIXELS), 0, 1);
        vec4 TopLeft = vec4((GridPos + vec2(0, 1)) * vec2(TILE_DIM_IN_PIXELS), 0, 1);
        vec4 TopRight = vec4((GridPos + vec2(1, 1)) * vec2(TILE_DIM_IN_PIXELS), 0, 1);
     
        // NOTE: Transform corner points to far plane in view space (we assume a counter clock wise winding order)
        BotLeft = ScreenToView(InverseProjection, ScreenSize, BotLeft);
        BotRight = ScreenToView(InverseProjection, ScreenSize, BotRight);
        TopLeft = ScreenToView(InverseProjection, ScreenSize, TopLeft);
        TopRight = ScreenToView(InverseProjection, ScreenSize, TopRight);
   
        // NOTE: Build the frustum planes and store
        frustum Frustum;
        Frustum.Planes[0] = PlaneCreate(CameraPos, BotLeft.xyz, TopLeft.xyz);
        Frustum.Planes[1] = PlaneCreate(CameraPos, TopRight.xyz, BotRight.xyz);
        Frustum.Planes[2] = PlaneCreate(CameraPos, TopLeft.xyz, TopRight.xyz);
        Frustum.Planes[3] = PlaneCreate(CameraPos, BotRight.xyz, BotLeft.xyz);
        
        // NOTE: Write out to buffer
        uint WriteIndex = GridPos.y * GridSize.x + GridPos.x;
        GridFrustums[WriteIndex] = Frustum;
    }
}

#endif

//
// NOTE: Light Culling Shader
//

#if LIGHT_CULLING

shared frustum SharedFrustum;
shared uint SharedMinDepth;
shared uint SharedMaxDepth;

// NOTE: Opaque
shared uint SharedGlobalLightId_O;
shared uint SharedCurrLightId_O;
shared uint SharedLightIds_O[1024];

// NOTE: Transparent
shared uint SharedGlobalLightId_T;
shared uint SharedCurrLightId_T;
shared uint SharedLightIds_T[1024];

void LightAppendOpaque(uint LightId)
{
    uint WriteArrayId = atomicAdd(SharedCurrLightId_O, 1);
    if (WriteArrayId < 1024)
    {
        SharedLightIds_O[WriteArrayId] = LightId;
    }
}

void LightAppendTransparent(uint LightId)
{
    uint WriteArrayId = atomicAdd(SharedCurrLightId_T, 1);
    if (WriteArrayId < 1024)
    {
        SharedLightIds_T[WriteArrayId] = LightId;
    }
}

layout(local_size_x = TILE_DIM_IN_PIXELS, local_size_y = TILE_DIM_IN_PIXELS, local_size_z = 1) in;

void main()
{    
    uint NumThreadsPerGroup = TILE_DIM_IN_PIXELS * TILE_DIM_IN_PIXELS;

    // NOTE: Skip threads that go past the screen
    if (!(gl_GlobalInvocationID.x < ScreenSize.x && gl_GlobalInvocationID.y < ScreenSize.y))
    {
        return;
    }
    
    // NOTE: Setup shared variables
    if (gl_LocalInvocationIndex == 0)
    {
        SharedFrustum = GridFrustums[uint(gl_WorkGroupID.y) * GridSize.x + uint(gl_WorkGroupID.x)];
        SharedMinDepth = 0xFFFFFFFF;
        SharedMaxDepth = 0;
        SharedCurrLightId_O = 0;
        SharedCurrLightId_T = 0;
    }

    barrier();
    
    // NOTE: Calculate min/max depth in grid tile (since our depth values are between 0 and 1, we can reinterpret them as ints and
    // comparison will still work correctly)
    ivec2 ReadPixelId = ivec2(gl_GlobalInvocationID.xy);
    uint PixelDepth = floatBitsToInt(texelFetch(GBufferDepthTexture, ReadPixelId, 0).x);
    atomicMin(SharedMinDepth, PixelDepth);
    atomicMax(SharedMaxDepth, PixelDepth);

    barrier();

    // NOTE: Convert depth bounds to frustum planes in view space
    float MinDepth = uintBitsToFloat(SharedMinDepth);
    float MaxDepth = uintBitsToFloat(SharedMaxDepth);

    MinDepth = ClipToView(InverseProjection, vec4(0, 0, MinDepth, 1)).z;
    MaxDepth = ClipToView(InverseProjection, vec4(0, 0, MaxDepth, 1)).z;

    float NearClipDepth = ClipToView(InverseProjection, vec4(0, 0, 1, 1)).z;
    plane MinPlane = { vec3(0, 0, 1), MaxDepth };
    
    // NOTE: Cull lights against tiles frustum (each thread culls one light at a time)
    for (uint LightId = gl_LocalInvocationIndex; LightId < SceneBuffer.NumPointLights; LightId += NumThreadsPerGroup)
    {
        point_light Light = PointLights[LightId];
        if (SphereInsideFrustum(Light.Pos, Light.MaxDistance, SharedFrustum, NearClipDepth, MinDepth))
        {
            LightAppendTransparent(LightId);

            if (!SphereInsidePlane(Light.Pos, Light.MaxDistance, MinPlane))
            {
                LightAppendOpaque(LightId);
            }
        }
    }

    barrier();

    // NOTE: Get space and light index lists
    if (gl_LocalInvocationIndex == 0)
    {
        ivec2 WritePixelId = ivec2(gl_WorkGroupID.xy);

        // NOTE: Without the ifs, we get a lot of false positives, might be quicker to skip the atomic? Idk if this matters a lot
        if (SharedCurrLightId_O != 0)
        {
            SharedGlobalLightId_O = atomicAdd(LightIndexCounter_O, SharedCurrLightId_O);
            imageStore(LightGrid_O, WritePixelId, ivec4(SharedGlobalLightId_O, SharedCurrLightId_O, 0, 0));
        }
        if (SharedCurrLightId_T != 0)
        {
            SharedGlobalLightId_T = atomicAdd(LightIndexCounter_T, SharedCurrLightId_T);
            imageStore(LightGrid_T, WritePixelId, ivec4(SharedGlobalLightId_T, SharedCurrLightId_T, 0, 0));
        }
    }

    barrier();

    // NOTE: Write opaque
    for (uint LightId = gl_LocalInvocationIndex; LightId < SharedCurrLightId_O; LightId += NumThreadsPerGroup)
    {
        LightIndexList_O[SharedGlobalLightId_O + LightId] = SharedLightIds_O[LightId];
    }

    // NOTE: Write transparent
    for (uint LightId = gl_LocalInvocationIndex; LightId < SharedCurrLightId_T; LightId += NumThreadsPerGroup)
    {
        LightIndexList_T[SharedGlobalLightId_T + LightId] = SharedLightIds_T[LightId];
    }
}

#endif

//
// NOTE: GBuffer Vertex
//

#if GBUFFER_VERT

layout(location = 0) in vec3 InPos;
layout(location = 1) in vec3 InNormal;
layout(location = 2) in vec2 InUv;

layout(location = 0) out vec3 OutViewPos;
layout(location = 1) out vec3 OutViewNormal;
layout(location = 2) out vec2 OutUv;
layout(location = 3) out flat uint OutMaterialId;
layout(location = 4) out flat uint OutColorId;

void main()
{
    instance_entry Entry = InstanceBuffer[gl_InstanceIndex];
    
    gl_Position = Entry.WVPTransform * vec4(InPos, 1);
    OutViewPos = (Entry.WVTransform * vec4(InPos, 1)).xyz;
    OutViewNormal = (Entry.WVTransform * vec4(InNormal, 0)).xyz;
    OutUv = InUv;
    OutMaterialId = Entry.MaterialId;
    OutColorId = Entry.ColorId;
}

#endif

//
// NOTE: GBuffer Fragment
//

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
// NOTE: Directional Light Vert
//

#if TILED_DEFERRED_LIGHTING_VERT

layout(location = 0) in vec3 InPos;

void main()
{
    gl_Position = vec4(2.0*InPos, 1);
}

#endif

//
// NOTE: Tiled Deferred Lighting
//

#if TILED_DEFERRED_LIGHTING_FRAG

float DirLightOcclusionGet(vec3 SurfaceNormal, vec3 LightDir, vec3 LightPos)
{
    // NOTE: You can embedd the NDC transform in the matrix but then you need a separate set of transforms for each object
    vec2 Uv = 0.5*LightPos.xy + vec2(0.5);

    // NOTE: This bias comes from http://www.opengl-tutorial.org/intermediate-tutorials/tutorial-16-shadow-mapping/ and
    // https://www.trentreed.net/blog/exponential-shadow-maps/ . The idea is dot is cos(theta) so acos(cos(theta)) = theta so it all
    // becomes tan(angle between vectors). Since we are scaling by the tangent of the angle between vectors, the more perpendicular the
    // angles are, the larger our bias will be. This appears to be a decent approximation
    // TODO: The bias still seems very bad on spheres....
    float Bias = clamp(0.005 * tan(acos(clamp(dot(SurfaceNormal, LightDir), 0, 1))), 0, 0.01);
    float Depth = texture(DirectionalShadow, Uv).x;
    // NOTE: Both our depth and LightPos.z are in reversed z
    float Result = step(Depth, LightPos.z + Bias);
    return Result;
}

layout(location = 0) out vec4 OutColor;

void main()
{
    vec3 CameraPos = vec3(0, 0, 0);
    ivec2 PixelPos = ivec2(gl_FragCoord.xy);

    material Material = GBufferGetMaterial(PixelPos);    
    vec3 SurfacePos = texelFetch(GBufferPositionTexture, PixelPos, 0).xyz;
    vec3 SurfaceNormal = texelFetch(GBufferNormalTexture, PixelPos, 0).xyz;
    vec3 SurfaceColor = GBufferGetColor(PixelPos).rgb * Material.TintColor.rgb;
    vec3 View = normalize(CameraPos - SurfacePos);
    vec3 Color = vec3(0);

    // NOTE: Calculate lighting for point lights
    ivec2 GridPos = PixelPos / ivec2(TILE_DIM_IN_PIXELS);
    uvec2 LightIndexMetaData = imageLoad(LightGrid_O, GridPos).xy; // NOTE: Stores the pointer + # of elements
    for (int i = 0; i < LightIndexMetaData.y; ++i)
    {
        uint LightId = LightIndexList_O[LightIndexMetaData.x + i];
        point_light CurrLight = PointLights[LightId];
        vec3 LightDir = normalize(SurfacePos - CurrLight.Pos);
        Color += BlinnPhongLighting(View, SurfaceColor, SurfaceNormal, Material.SpecularPower, LightDir,
                                    PointLightAttenuate(SurfacePos, CurrLight));
    }

    // NOTE: Calculate lighting for directional lights
    {
        float Occlusion = DirLightOcclusionGet(SurfaceNormal, DirectionalLight.Dir, (DirectionalLight.VPTransform * vec4(SurfacePos, 1)).xyz);
        Color += Occlusion*BlinnPhongLighting(View, SurfaceColor, SurfaceNormal, Material.SpecularPower, DirectionalLight.Dir,
                                              DirectionalLight.Color);
        Color += DirectionalLight.AmbientLight * SurfaceColor;
    }

    Color = clamp(Color, 0, 1);
    OutColor = vec4(Color, 1);
}

#endif

#if SHADOW_VERT

layout(location = 0) in vec3 InPos;

layout(location = 0) out float OutDepth;

void main()
{
    vec4 Position = DirectionalTransforms[gl_InstanceIndex] * vec4(InPos, 1);
    gl_Position = Position;
    OutDepth = Position.z;
}

#endif
