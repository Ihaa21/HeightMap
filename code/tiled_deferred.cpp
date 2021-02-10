
inline directional_shadow ShadowCreate(u32 Width, u32 Height, VkDescriptorSet TiledDeferredDescriptor)
{
    directional_shadow Result = {};

    Result.Arena = VkLinearArenaCreate(RenderState->Device, RenderState->LocalMemoryId, MegaBytes(64));

    {
        VkSamplerCreateInfo SamplerCreateInfo = {};
        SamplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        SamplerCreateInfo.magFilter = VK_FILTER_NEAREST;
        SamplerCreateInfo.minFilter = VK_FILTER_NEAREST;
        SamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        SamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        SamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        SamplerCreateInfo.anisotropyEnable = VK_FALSE;
        SamplerCreateInfo.maxAnisotropy = 0.0f;
        SamplerCreateInfo.compareEnable = VK_FALSE;
        SamplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        SamplerCreateInfo.mipLodBias = 0;
        SamplerCreateInfo.minLod = 0;
        SamplerCreateInfo.maxLod = 0;
        SamplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        SamplerCreateInfo.unnormalizedCoordinates = VK_FALSE;

        VkCheckResult(vkCreateSampler(RenderState->Device, &SamplerCreateInfo, 0, &Result.Sampler));
    }
    
    RenderTargetEntryCreate(&Result.Arena, Width, Height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_IMAGE_ASPECT_DEPTH_BIT, &Result.Image, &Result.Entry);
    VkDescriptorImageWrite(&RenderState->DescriptorManager, TiledDeferredDescriptor, 12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                           Result.Entry.View, Result.Sampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

    VkDescriptorManagerFlush(RenderState->Device, &RenderState->DescriptorManager);

    return Result;
}

inline void TiledDeferredSwapChainChange(tiled_deferred_state* State, u32 Width, u32 Height, VkFormat ColorFormat,
                                         render_scene* Scene, VkDescriptorSet* OutputRtSet)
{
    b32 ReCreate = State->RenderTargetArena.Used != 0;
    VkArenaClear(&State->RenderTargetArena);
    
    // NOTE: Render Target Data
    {
        RenderTargetEntryReCreate(&State->RenderTargetArena, Width, Height, VK_FORMAT_R32G32B32A32_SFLOAT,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                  VK_IMAGE_ASPECT_COLOR_BIT, &State->GBufferPositionImage, &State->GBufferPositionEntry);
        RenderTargetEntryReCreate(&State->RenderTargetArena, Width, Height, VK_FORMAT_R32G32B32A32_SFLOAT,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                  VK_IMAGE_ASPECT_COLOR_BIT, &State->GBufferNormalImage, &State->GBufferNormalEntry);
        RenderTargetEntryReCreate(&State->RenderTargetArena, Width, Height, VK_FORMAT_R32G32B32A32_SFLOAT,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                  VK_IMAGE_ASPECT_COLOR_BIT, &State->GBufferMaterialImage, &State->GBufferMaterialEntry);
        RenderTargetEntryReCreate(&State->RenderTargetArena, Width, Height, VK_FORMAT_D32_SFLOAT,
                                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                  VK_IMAGE_ASPECT_DEPTH_BIT, &State->DepthImage, &State->DepthEntry);
        RenderTargetEntryReCreate(&State->RenderTargetArena, Width, Height, ColorFormat,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                  VK_IMAGE_ASPECT_COLOR_BIT, &State->OutColorImage, &State->OutColorEntry);

        if (ReCreate)
        {
            RenderTargetUpdateEntries(&DemoState->TempArena, &State->GBufferPass);
            RenderTargetUpdateEntries(&DemoState->TempArena, &State->LightingPass);
        }
        
        VkDescriptorImageWrite(&RenderState->DescriptorManager, *OutputRtSet, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               State->OutColorEntry.View, DemoState->PointSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        
        // NOTE: GBuffer
        VkDescriptorImageWrite(&RenderState->DescriptorManager, State->TiledDeferredDescriptor, 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               State->GBufferPositionEntry.View, DemoState->PointSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        VkDescriptorImageWrite(&RenderState->DescriptorManager, State->TiledDeferredDescriptor, 9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               State->GBufferNormalEntry.View, DemoState->PointSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        VkDescriptorImageWrite(&RenderState->DescriptorManager, State->TiledDeferredDescriptor, 10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               State->GBufferMaterialEntry.View, DemoState->PointSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        VkDescriptorImageWrite(&RenderState->DescriptorManager, State->TiledDeferredDescriptor, 11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               State->DepthEntry.View, DemoState->PointSampler, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    }
    
    // NOTE: Tiled Data
    {
        u32 NumTilesX = CeilU32(f32(Width) / f32(TILE_SIZE_IN_PIXELS));
        u32 NumTilesY = CeilU32(f32(Height) / f32(TILE_SIZE_IN_PIXELS));

        // NOTE: Destroy old data
        if (ReCreate)
        {
            vkDestroyBuffer(RenderState->Device, State->GridFrustums, 0);
            vkDestroyBuffer(RenderState->Device, State->LightIndexList_O, 0);
            vkDestroyBuffer(RenderState->Device, State->LightIndexList_T, 0);
            vkDestroyImageView(RenderState->Device, State->LightGrid_O.View, 0);
            vkDestroyImage(RenderState->Device, State->LightGrid_O.Image, 0);
            vkDestroyImageView(RenderState->Device, State->LightGrid_T.View, 0);
            vkDestroyImage(RenderState->Device, State->LightGrid_T.Image, 0);
        }
        
        State->GridFrustums = VkBufferCreate(RenderState->Device, &State->RenderTargetArena, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                             sizeof(frustum) * NumTilesX * NumTilesY);
        State->LightGrid_O = VkImageCreate(RenderState->Device, &State->RenderTargetArena, NumTilesX, NumTilesY, VK_FORMAT_R32G32_UINT,
                                           VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        State->LightIndexList_O = VkBufferCreate(RenderState->Device, &State->RenderTargetArena, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                 sizeof(u32) * MAX_LIGHTS_PER_TILE * NumTilesX * NumTilesY);
        State->LightGrid_T = VkImageCreate(RenderState->Device, &State->RenderTargetArena, NumTilesX, NumTilesY, VK_FORMAT_R32G32_UINT,
                                           VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        State->LightIndexList_T = VkBufferCreate(RenderState->Device, &State->RenderTargetArena, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                 sizeof(u32) * MAX_LIGHTS_PER_TILE * NumTilesX * NumTilesY);

        VkDescriptorBufferWrite(&RenderState->DescriptorManager, State->TiledDeferredDescriptor, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, State->GridFrustums);
        VkDescriptorImageWrite(&RenderState->DescriptorManager, State->TiledDeferredDescriptor, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                               State->LightGrid_O.View, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, State->TiledDeferredDescriptor, 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, State->LightIndexList_O);
        VkDescriptorImageWrite(&RenderState->DescriptorManager, State->TiledDeferredDescriptor, 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                               State->LightGrid_T.View, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, State->TiledDeferredDescriptor, 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, State->LightIndexList_T);
    }

    VkDescriptorManagerFlush(RenderState->Device, &RenderState->DescriptorManager);
    
    // NOTE: Init Grid Frustums
    vk_commands Commands = RenderState->Commands;
    VkCommandsBegin(RenderState->Device, Commands);
    {
        // NOTE: Init our images
        VkBarrierImageAdd(&RenderState->BarrierManager, VK_ACCESS_MEMORY_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_MEMORY_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_IMAGE_LAYOUT_GENERAL,
                          VK_IMAGE_ASPECT_COLOR_BIT, State->LightGrid_O.Image);
        VkBarrierImageAdd(&RenderState->BarrierManager, VK_ACCESS_MEMORY_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_MEMORY_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_IMAGE_LAYOUT_GENERAL,
                          VK_IMAGE_ASPECT_COLOR_BIT, State->LightGrid_T.Image);
        VkBarrierManagerFlush(&RenderState->BarrierManager, Commands.Buffer);

        // NOTE: Update our tiled deferred globals
        {
            tiled_deferred_globals* Data = VkTransferPushWriteStruct(&RenderState->TransferManager, State->TiledDeferredGlobals, tiled_deferred_globals,
                                                                     BarrierMask(VkAccessFlagBits(0), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT),
                                                                     BarrierMask(VK_ACCESS_UNIFORM_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT));
            *Data = {};
            Data->InverseProjection = Inverse(CameraGetP(&Scene->Camera));
            Data->ScreenSize = V2(RenderState->WindowWidth, RenderState->WindowHeight);
            Data->GridSizeX = CeilU32(f32(RenderState->WindowWidth) / f32(TILE_SIZE_IN_PIXELS));
            Data->GridSizeY = CeilU32(f32(RenderState->WindowHeight) / f32(TILE_SIZE_IN_PIXELS));
        }
        VkTransferManagerFlush(&RenderState->TransferManager, RenderState->Device, RenderState->Commands.Buffer, &RenderState->BarrierManager);

        vkCmdBindPipeline(Commands.Buffer, VK_PIPELINE_BIND_POINT_COMPUTE, State->GridFrustumPipeline->Handle);
        VkDescriptorSet DescriptorSets[] =
            {
                State->TiledDeferredDescriptor,
            };
        vkCmdBindDescriptorSets(Commands.Buffer, VK_PIPELINE_BIND_POINT_COMPUTE, State->GridFrustumPipeline->Layout, 0,
                                ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
        u32 DispatchX = CeilU32(f32(RenderState->WindowWidth) / f32(8 * TILE_SIZE_IN_PIXELS));
        u32 DispatchY = CeilU32(f32(RenderState->WindowHeight) / f32(8 * TILE_SIZE_IN_PIXELS));
        vkCmdDispatch(Commands.Buffer, DispatchX, DispatchY, 1);
    }
    VkCommandsSubmit(RenderState->GraphicsQueue, Commands);
}

inline void TiledDeferredCreate(renderer_create_info CreateInfo, VkDescriptorSet* OutputRtSet, tiled_deferred_state* Result)
{
    *Result = {};

    u64 HeapSize = GigaBytes(1);
    Result->RenderTargetArena = VkLinearArenaCreate(RenderState->Device, RenderState->LocalMemoryId, HeapSize);
    
    // NOTE: Create globals
    {        
        Result->TiledDeferredGlobals = VkBufferCreate(RenderState->Device, &RenderState->GpuArena, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                      sizeof(tiled_deferred_globals));
        Result->LightIndexCounter_O = VkBufferCreate(RenderState->Device, &RenderState->GpuArena, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                     sizeof(u32));
        Result->LightIndexCounter_T = VkBufferCreate(RenderState->Device, &RenderState->GpuArena, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                     sizeof(u32));
        
        {
            vk_descriptor_layout_builder Builder = VkDescriptorLayoutBegin(&Result->TiledDeferredDescLayout);

            // NOTE: Tiled Descriptors
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);

            // NOTE: GBuffer Descriptors
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);

            // NOTE: Shadow Descriptors
            VkDescriptorLayoutAdd(&Builder, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
            
            VkDescriptorLayoutEnd(RenderState->Device, &Builder);
        }

        Result->TiledDeferredDescriptor = VkDescriptorSetAllocate(RenderState->Device, RenderState->DescriptorPool, Result->TiledDeferredDescLayout);

        // NOTE: Tiled Data
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, Result->TiledDeferredDescriptor, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, Result->TiledDeferredGlobals);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, Result->TiledDeferredDescriptor, 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Result->LightIndexCounter_O);
        VkDescriptorBufferWrite(&RenderState->DescriptorManager, Result->TiledDeferredDescriptor, 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, Result->LightIndexCounter_T);
    }

    // NOTE: Grid Frustum
    {
        VkDescriptorSetLayout Layouts[] = 
            {
                Result->TiledDeferredDescLayout,
            };
            
        Result->GridFrustumPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                              "tiled_deferred_grid_frustum.spv", "main", Layouts, ArrayCount(Layouts));
    }
    
    Result->Shadow = ShadowCreate(1024, 1024, Result->TiledDeferredDescriptor);
    TiledDeferredSwapChainChange(Result, CreateInfo.Width, CreateInfo.Height, CreateInfo.ColorFormat, CreateInfo.Scene, OutputRtSet);
        
    // NOTE: Create PSOs
    // IMPORTANT: We don't do this in a single render pass since we cannot do compute between graphics
    {        
        // NOTE: GBuffer Pass
        {
            // NOTE: RT
            {
                render_target_builder Builder = RenderTargetBuilderBegin(&DemoState->Arena, &DemoState->TempArena, CreateInfo.Width, CreateInfo.Height);
                RenderTargetAddTarget(&Builder, &Result->GBufferPositionEntry, VkClearColorCreate(0, 0, 0, 1));
                RenderTargetAddTarget(&Builder, &Result->GBufferNormalEntry, VkClearColorCreate(0, 0, 0, 1));
                RenderTargetAddTarget(&Builder, &Result->GBufferMaterialEntry, VkClearColorCreate(0, 0, 0, 0xFFFFFFFF));
                RenderTargetAddTarget(&Builder, &Result->DepthEntry, VkClearDepthStencilCreate(0, 0));
                            
                vk_render_pass_builder RpBuilder = VkRenderPassBuilderBegin(&DemoState->TempArena);

                u32 GBufferPositionId = VkRenderPassAttachmentAdd(&RpBuilder, Result->GBufferPositionEntry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                                  VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                u32 GBufferNormalId = VkRenderPassAttachmentAdd(&RpBuilder, Result->GBufferNormalEntry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                                VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                u32 GBufferColorId = VkRenderPassAttachmentAdd(&RpBuilder, Result->GBufferMaterialEntry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                               VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                u32 DepthId = VkRenderPassAttachmentAdd(&RpBuilder, Result->DepthEntry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                        VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

                VkRenderPassSubPassBegin(&RpBuilder, VK_PIPELINE_BIND_POINT_GRAPHICS);
                VkRenderPassColorRefAdd(&RpBuilder, GBufferPositionId, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                VkRenderPassColorRefAdd(&RpBuilder, GBufferNormalId, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                VkRenderPassColorRefAdd(&RpBuilder, GBufferColorId, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                VkRenderPassDepthRefAdd(&RpBuilder, DepthId, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
                VkRenderPassSubPassEnd(&RpBuilder);

                VkRenderPassDependency(&RpBuilder, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                       VK_ACCESS_SHADER_READ_BIT, VK_DEPENDENCY_BY_REGION_BIT);
                
                Result->GBufferPass = RenderTargetBuilderEnd(&Builder, VkRenderPassBuilderEnd(&RpBuilder, RenderState->Device));
            }

            // NOTE: GBuffer PSO
            {
                vk_pipeline_builder Builder = VkPipelineBuilderBegin(&DemoState->TempArena);

                // NOTE: Shaders
                VkPipelineShaderAdd(&Builder, "tiled_deferred_gbuffer_vert.spv", "main", VK_SHADER_STAGE_VERTEX_BIT);
                VkPipelineShaderAdd(&Builder, "tiled_deferred_gbuffer_frag.spv", "main", VK_SHADER_STAGE_FRAGMENT_BIT);
                
                // NOTE: Specify input vertex data format
                VkPipelineVertexBindingBegin(&Builder);
                VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32_SFLOAT, sizeof(v3));
                VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32_SFLOAT, sizeof(v3));
                VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32_SFLOAT, sizeof(v2));
                VkPipelineVertexBindingEnd(&Builder);

                VkPipelineInputAssemblyAdd(&Builder, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE);
                VkPipelineDepthStateAdd(&Builder, VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER);

                VkPipelineColorAttachmentAdd(&Builder, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
                                             VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
                VkPipelineColorAttachmentAdd(&Builder, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
                                             VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);
                VkPipelineColorAttachmentAdd(&Builder, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
                                             VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);

                VkDescriptorSetLayout DescriptorLayouts[] =
                    {
                        Result->TiledDeferredDescLayout,
                        CreateInfo.SceneDescLayout,
                    };
            
                Result->GBufferPipeline = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager,
                                                               Result->GBufferPass.RenderPass, 0, DescriptorLayouts, ArrayCount(DescriptorLayouts));
            }
        }

        // NOTE: Shadow Pass
        {
            // NOTE: RT
            {
                render_target_builder Builder = RenderTargetBuilderBegin(&DemoState->Arena, &DemoState->TempArena, CreateInfo.Width, CreateInfo.Height);
                RenderTargetAddTarget(&Builder, &Result->Shadow.Entry, VkClearDepthStencilCreate(0, 0));
                            
                vk_render_pass_builder RpBuilder = VkRenderPassBuilderBegin(&DemoState->TempArena);

                u32 ShadowId = VkRenderPassAttachmentAdd(&RpBuilder, Result->Shadow.Entry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                        VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

                VkRenderPassSubPassBegin(&RpBuilder, VK_PIPELINE_BIND_POINT_GRAPHICS);
                VkRenderPassDepthRefAdd(&RpBuilder, ShadowId, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
                VkRenderPassSubPassEnd(&RpBuilder);

                VkRenderPassDependency(&RpBuilder, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                       VK_ACCESS_SHADER_READ_BIT, VK_DEPENDENCY_BY_REGION_BIT);
                
                Result->Shadow.Target = RenderTargetBuilderEnd(&Builder, VkRenderPassBuilderEnd(&RpBuilder, RenderState->Device));
            }
            
            // NOTE: Shadow PSO
            {
                vk_pipeline_builder Builder = VkPipelineBuilderBegin(&DemoState->TempArena);

                // NOTE: Shaders
                VkPipelineShaderAdd(&Builder, "shadow_vert.spv", "main", VK_SHADER_STAGE_VERTEX_BIT);
                
                // NOTE: Specify input vertex data format
                VkPipelineVertexBindingBegin(&Builder);
                VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32_SFLOAT, sizeof(v3));
                VkPipelineVertexAttributeAddOffset(&Builder, sizeof(v3));
                VkPipelineVertexAttributeAddOffset(&Builder, sizeof(v2));
                VkPipelineVertexBindingEnd(&Builder);

                VkPipelineInputAssemblyAdd(&Builder, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE);
                VkPipelineDepthStateAdd(&Builder, VK_TRUE, VK_TRUE, VK_COMPARE_OP_GREATER);
                VkPipelineDepthOffsetAdd(&Builder, 4.0f, 1.0f, 1.5f);

                VkDescriptorSetLayout DescriptorLayouts[] =
                    {
                        Result->TiledDeferredDescLayout,
                        CreateInfo.SceneDescLayout,
                    };
            
                Result->Shadow.Pipeline = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager,
                                                               Result->Shadow.Target.RenderPass, 0, DescriptorLayouts,
                                                               ArrayCount(DescriptorLayouts));
            }
        }

        // NOTE: Light Cull
        {
            VkDescriptorSetLayout Layouts[] =
                {
                    Result->TiledDeferredDescLayout,
                    CreateInfo.SceneDescLayout,
                };
            
            Result->LightCullPipeline = VkPipelineComputeCreate(RenderState->Device, &RenderState->PipelineManager, &DemoState->TempArena,
                                                                "tiled_deferred_light_culling.spv", "main", Layouts, ArrayCount(Layouts));
        }

        // NOTE: Lighting Pass 
        {
            // NOTE: RT
            {
                render_target_builder Builder = RenderTargetBuilderBegin(&DemoState->Arena, &DemoState->TempArena, CreateInfo.Width, CreateInfo.Height);
                RenderTargetAddTarget(&Builder, &Result->OutColorEntry, VkClearColorCreate(0, 0, 0, 1));
                RenderTargetAddTarget(&Builder, &Result->DepthEntry, VkClearColorCreate(0, 0, 0, 1));
                            
                vk_render_pass_builder RpBuilder = VkRenderPassBuilderBegin(&DemoState->TempArena);

                u32 OutColorId = VkRenderPassAttachmentAdd(&RpBuilder, Result->OutColorEntry.Format, VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                           VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED,
                                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                u32 DepthId = VkRenderPassAttachmentAdd(&RpBuilder, Result->DepthEntry.Format, VK_ATTACHMENT_LOAD_OP_LOAD,
                                                        VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                                                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

                VkRenderPassDependency(&RpBuilder, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                       VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_DEPENDENCY_BY_REGION_BIT);

                VkRenderPassSubPassBegin(&RpBuilder, VK_PIPELINE_BIND_POINT_GRAPHICS);
                VkRenderPassColorRefAdd(&RpBuilder, OutColorId, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                VkRenderPassSubPassEnd(&RpBuilder);

                Result->LightingPass = RenderTargetBuilderEnd(&Builder, VkRenderPassBuilderEnd(&RpBuilder, RenderState->Device));
            }

            {
                vk_pipeline_builder Builder = VkPipelineBuilderBegin(&DemoState->TempArena);

                // NOTE: Shaders
                VkPipelineShaderAdd(&Builder, "tiled_deferred_lighting_vert.spv", "main", VK_SHADER_STAGE_VERTEX_BIT);
                VkPipelineShaderAdd(&Builder, "tiled_deferred_lighting_frag.spv", "main", VK_SHADER_STAGE_FRAGMENT_BIT);
                
                // NOTE: Specify input vertex data format
                VkPipelineVertexBindingBegin(&Builder);
                VkPipelineVertexAttributeAdd(&Builder, VK_FORMAT_R32G32B32_SFLOAT, 2*sizeof(v3) + sizeof(v2));
                VkPipelineVertexBindingEnd(&Builder);

                VkPipelineInputAssemblyAdd(&Builder, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE);
                VkPipelineColorAttachmentAdd(&Builder, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
                                             VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO);

                VkDescriptorSetLayout DescriptorLayouts[] =
                    {
                        Result->TiledDeferredDescLayout,
                        CreateInfo.SceneDescLayout,
                    };
            
                Result->LightingPipeline = VkPipelineBuilderEnd(&Builder, RenderState->Device, &RenderState->PipelineManager,
                                                                Result->LightingPass.RenderPass, 0, DescriptorLayouts, ArrayCount(DescriptorLayouts));
            }
        }
    }
}

inline void TiledDeferredAddMeshes(tiled_deferred_state* State, render_scene* Scene, render_mesh* QuadMesh)
{
    State->QuadMesh = QuadMesh;
}

inline void TiledDeferredRender(vk_commands Commands, tiled_deferred_state* State, render_scene* Scene)
{
    // NOTE: Clear images
    {
        // NOTE: Clear buffers and upload data
        VkClearValue ClearColor = VkClearColorCreate(0, 0, 0, 0);
        VkImageSubresourceRange Range = {};
        Range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        Range.baseMipLevel = 0;
        Range.levelCount = 1;
        Range.baseArrayLayer = 0;
        Range.layerCount = 1;
        
        vkCmdClearColorImage(Commands.Buffer, State->LightGrid_O.Image, VK_IMAGE_LAYOUT_GENERAL, &ClearColor.color, 1, &Range);
        vkCmdClearColorImage(Commands.Buffer, State->LightGrid_T.Image, VK_IMAGE_LAYOUT_GENERAL, &ClearColor.color, 1, &Range);
        vkCmdFillBuffer(Commands.Buffer, State->LightIndexCounter_O, 0, sizeof(u32), 0);
        vkCmdFillBuffer(Commands.Buffer, State->LightIndexCounter_T, 0, sizeof(u32), 0);
    }

    // NOTE: Generate Grass
    {
        //VkBarrierBufferAdd(&RenderState->BarrierManager, VK_ACCESS_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
        //                   VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, Grass->IndirectArg);

#if 0
        // NOTE: Grass Lines
        {
            grass_lines* GrassLines = &State->GrassLines;

            VkBarrierBufferAdd(&RenderState->BarrierManager, VK_ACCESS_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                               VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, GrassLines->IndirectArg);
            VkBarrierManagerFlush(&RenderState->BarrierManager, Commands.Buffer);
            
            vk_pipeline* Pipeline = GrassLines->GenBladesPipeline;
            vkCmdBindPipeline(Commands.Buffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline->Handle);
            VkDescriptorSet DescriptorSets[] =
                {
                    GrassLines->Descriptor,
                };
            vkCmdBindDescriptorSets(Commands.Buffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline->Layout, 0, ArrayCount(DescriptorSets),
                                    DescriptorSets, 0, 0);
            u32 DispatchX = CeilU32(f32(GrassLines->UniformsCpu.NumBladesX) / f32(8));
            u32 DispatchY = CeilU32(f32(GrassLines->UniformsCpu.NumBladesY) / f32(8));
            vkCmdDispatch(Commands.Buffer, DispatchX, DispatchY, 1);

            VkBarrierBufferAdd(&RenderState->BarrierManager, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                               VK_ACCESS_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, GrassLines->IndirectArg);
            VkBarrierManagerFlush(&RenderState->BarrierManager, Commands.Buffer);
        }
#endif
        
#if 0
        // NOTE: Grass Blades
        {
            grass_blades* GrassBlades = &State->GrassBlades;

            VkBarrierBufferAdd(&RenderState->BarrierManager, VK_ACCESS_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                               VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, GrassBlades->IndirectArg);
            VkBarrierManagerFlush(&RenderState->BarrierManager, Commands.Buffer);
            
            vk_pipeline* Pipeline = GrassBlades->GenBladesPipeline;
            vkCmdBindPipeline(Commands.Buffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline->Handle);
            VkDescriptorSet DescriptorSets[] =
                {
                    GrassBlades->Descriptor,
                };
            vkCmdBindDescriptorSets(Commands.Buffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pipeline->Layout, 0, ArrayCount(DescriptorSets),
                                    DescriptorSets, 0, 0);
            u32 DispatchX = CeilU32(f32(GrassBlades->UniformsCpu.NumBladesX) / f32(8));
            u32 DispatchY = CeilU32(f32(GrassBlades->UniformsCpu.NumBladesY) / f32(8));
            vkCmdDispatch(Commands.Buffer, DispatchX, DispatchY, 1);

            VkBarrierBufferAdd(&RenderState->BarrierManager, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                               VK_ACCESS_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, GrassBlades->IndirectArg);
            VkBarrierManagerFlush(&RenderState->BarrierManager, Commands.Buffer);
        }
#endif
        
        //VkBarrierBufferAdd(&RenderState->BarrierManager, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        //                   VK_ACCESS_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, Grass->IndirectArg);
    }
    
    // NOTE: Shadow Pass
    RenderTargetPassBegin(&State->Shadow.Target, Commands, RenderTargetRenderPass_SetViewPort | RenderTargetRenderPass_SetScissor);
    {
        vk_pipeline* Pipeline = State->Shadow.Pipeline;
        vkCmdBindPipeline(Commands.Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline->Handle);
        VkDescriptorSet DescriptorSets[] =
            {
                State->TiledDeferredDescriptor,
                Scene->SceneDescriptor,
            };
        vkCmdBindDescriptorSets(Commands.Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline->Layout, 0,
                                ArrayCount(DescriptorSets), DescriptorSets, 0, 0);

        for (u32 InstanceId = 0; InstanceId < Scene->NumOpaqueInstances; ++InstanceId)
        {
            instance_entry* CurrInstance = Scene->OpaqueInstances + InstanceId;
            render_mesh* CurrMesh = Scene->RenderMeshes + CurrInstance->MeshId;
            
            VkDeviceSize Offset = 0;
            vkCmdBindVertexBuffers(Commands.Buffer, 0, 1, &CurrMesh->VertexBuffer, &Offset);
            vkCmdBindIndexBuffer(Commands.Buffer, CurrMesh->IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(Commands.Buffer, CurrMesh->NumIndices, 1, 0, 0, InstanceId);
        }
    }
    RenderTargetPassEnd(Commands);
    
    RenderTargetPassBegin(&State->GBufferPass, Commands, RenderTargetRenderPass_SetViewPort | RenderTargetRenderPass_SetScissor);
    // NOTE: GBuffer Pass
    {
        // NOTE: Regular entities
        {
            vk_pipeline* Pipeline = State->GBufferPipeline;
            vkCmdBindPipeline(Commands.Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline->Handle);

            for (u32 InstanceId = 0; InstanceId < Scene->NumOpaqueInstances; ++InstanceId)
            {
                instance_entry* CurrInstance = Scene->OpaqueInstances + InstanceId;
                render_mesh* CurrMesh = Scene->RenderMeshes + CurrInstance->MeshId;

                VkDescriptorSet DescriptorSets[] =
                    {
                        State->TiledDeferredDescriptor,
                        Scene->SceneDescriptor,
                    };
                vkCmdBindDescriptorSets(Commands.Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipeline->Layout, 0,
                                        ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
            
                VkDeviceSize Offset = 0;
                vkCmdBindVertexBuffers(Commands.Buffer, 0, 1, &CurrMesh->VertexBuffer, &Offset);
                vkCmdBindIndexBuffer(Commands.Buffer, CurrMesh->IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(Commands.Buffer, CurrMesh->NumIndices, 1, 0, 0, InstanceId);
            }
        }
    }
    RenderTargetPassEnd(Commands);
    
    // NOTE: Light Culling Pass
    {
        vkCmdBindPipeline(Commands.Buffer, VK_PIPELINE_BIND_POINT_COMPUTE, State->LightCullPipeline->Handle);
        VkDescriptorSet DescriptorSets[] =
            {
                State->TiledDeferredDescriptor,
                Scene->SceneDescriptor,
            };
        vkCmdBindDescriptorSets(Commands.Buffer, VK_PIPELINE_BIND_POINT_COMPUTE, State->LightCullPipeline->Layout, 0,
                                ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
        u32 DispatchX = CeilU32(f32(RenderState->WindowWidth) / f32(TILE_SIZE_IN_PIXELS));
        u32 DispatchY = CeilU32(f32(RenderState->WindowHeight) / f32(TILE_SIZE_IN_PIXELS));
        vkCmdDispatch(Commands.Buffer, DispatchX, DispatchY, 1);
    }

    vkCmdPipelineBarrier(Commands.Buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 0, 0);
    
    // NOTE: Lighting Pass
    RenderTargetPassBegin(&State->LightingPass, Commands, RenderTargetRenderPass_SetViewPort | RenderTargetRenderPass_SetScissor);
    {
        vkCmdBindPipeline(Commands.Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, State->LightingPipeline->Handle);
        {
            VkDescriptorSet DescriptorSets[] =
                {
                    State->TiledDeferredDescriptor,
                    Scene->SceneDescriptor,
                };
            vkCmdBindDescriptorSets(Commands.Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, State->LightingPipeline->Layout, 0,
                                    ArrayCount(DescriptorSets), DescriptorSets, 0, 0);
        }

        VkDeviceSize Offset = 0;
        vkCmdBindVertexBuffers(Commands.Buffer, 0, 1, &State->QuadMesh->VertexBuffer, &Offset);
        vkCmdBindIndexBuffer(Commands.Buffer, State->QuadMesh->IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(Commands.Buffer, State->QuadMesh->NumIndices, 1, 0, 0, 0);
    }

    RenderTargetPassEnd(Commands);
}
