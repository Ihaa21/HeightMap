
vec4 GBufferGenerateMaterial(uint ColorId, vec2 Uv, uint MaterialId)
{
    vec4 Result;
    Result.x = uintBitsToFloat(ColorId);
    Result.yz = Uv;
    Result.w = uintBitsToFloat(MaterialId);
    return Result;
}

vec4 GBufferGetColor(ivec2 PixelPos)
{
    vec3 FetchedData = texelFetch(GBufferMaterialTexture, PixelPos, 0).xyz;
    uint TextureId = floatBitsToUint(FetchedData.x);
    vec2 Uv = FetchedData.yz;
    vec4 Result = texture(ColorTextures[nonuniformEXT(TextureId)], Uv);
    
    return Result;
}

material GBufferGetMaterial(ivec2 PixelPos)
{
    float FetchedData = texelFetch(GBufferMaterialTexture, PixelPos, 0).w;
    uint MaterialId = floatBitsToUint(FetchedData);
    material Result = MaterialBuffer[MaterialId];
    
    return Result;
}
