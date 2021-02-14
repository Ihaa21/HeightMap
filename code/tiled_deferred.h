#pragma once

#define TILE_SIZE_IN_PIXELS 8
#define MAX_LIGHTS_PER_TILE 1024

struct tiled_deferred_globals
{
    // TODO: Move to camera?
    m4 InverseProjection;
    v2 ScreenSize;
    u32 GridSizeX;
    u32 GridSizeY;
};

struct directional_shadow
{
    vk_linear_arena Arena;
    
    u32 Width;
    u32 Height;
    VkSampler Sampler;
    VkImage Image;
    render_target_entry Entry;
    render_target Target;
    vk_pipeline* Pipeline;
};

struct height_map_uniforms
{
    m4 WVPTransform;
    m4 WVTransform;
    m4 ShadowTransform;
    v2i MousePixelPos;
    u32 MaterialId;
    u32 ColorId;
    u32 Width;
    u32 Height;
    u32 Pad[2];
};

struct height_map_readback
{
    v3 ViewIntersectPos;
};

enum height_brush
{
    HeightBrush_None,

    HeightBrush_Gaussian,
    HeightBrush_Square,
};

struct height_map
{
    vk_linear_arena Arena;

    // NOTE: Input
    height_brush HeightBrush;
    v2 BrushRadius;
    f32 BrushVel;
    
    height_map_uniforms UniformsCpu;
    VkBuffer UniformBuffer;
    VkBuffer IndexBuffer;

    vk_image HeightMap;
    f32* HeightMapData;
    
    VkDeviceMemory ReadBackMemory;
    VkBuffer ReadBackBuffer;
    height_map_readback* ReadBackPtr;

    VkDescriptorSetLayout DescLayout;
    VkDescriptorSet Descriptor;

    vk_pipeline* GBufferPipeline;
    vk_pipeline* ShadowPipeline;
};

struct tiled_deferred_state
{
    vk_linear_arena RenderTargetArena;

    directional_shadow Shadow;
    height_map HeightMap;
    
    // NOTE: GBuffer
    VkImage GBufferPositionImage;
    render_target_entry GBufferPositionEntry;
    VkImage GBufferNormalImage;
    render_target_entry GBufferNormalEntry;
    VkImage GBufferMaterialImage;
    render_target_entry GBufferMaterialEntry;
    VkImage DepthImage;
    render_target_entry DepthEntry;
    VkImage OutColorImage;
    render_target_entry OutColorEntry;
    render_target GBufferPass;
    render_target LightingPass;

    // NOTE: Global data
    VkBuffer TiledDeferredGlobals;
    VkBuffer GridFrustums;
    VkBuffer LightIndexList_O;
    VkBuffer LightIndexCounter_O;
    vk_image LightGrid_O;
    VkBuffer LightIndexList_T;
    VkBuffer LightIndexCounter_T;
    vk_image LightGrid_T;
    VkDescriptorSetLayout TiledDeferredDescLayout;
    VkDescriptorSet TiledDeferredDescriptor;

    render_mesh* QuadMesh;
    
    vk_pipeline* GridFrustumPipeline;
    vk_pipeline* GBufferPipeline;
    vk_pipeline* LightCullPipeline;
    vk_pipeline* LightingPipeline;
};

