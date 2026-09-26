// Each branch addresses a constant slot; LOD can differ between fragments.
#pragma once
float4 localLoadColor(uint slot, int3 coord) {
    switch (slot) {
    case 0: return gTextures[0].Load(coord);
    case 1: return gTextures[1].Load(coord);
    case 2: return gTextures[2].Load(coord);
    case 3: return gTextures[3].Load(coord);
    case 4: return gTextures[4].Load(coord);
    case 5: return gTextures[5].Load(coord);
    case 6: return gTextures[6].Load(coord);
    case 7: return gTextures[7].Load(coord);
    default: return 0.0f;
    }
}

float4 localLoadTMEM(uint slot, int2 texel, RDPTile tile, uint tlut) {
    // Select a byte source inside the decoder, rather than duplicating every
    // N64 texture format and palette decoding branch for all eight slots.
    return sampleTMEM(texel, tile.siz, tile.fmt, tile.address, tile.stride, tlut, tile.palette, slot);
}

float4 localNative(uint slot, uint samplerIndex, int2 texel, float2 textureSize) {
    int2 size = int2(textureSize);
    uint mode = clamp(samplerIndex, 1u, 9u) - 1u;
    uint2 addressMode = uint2(mode / 3u, mode % 3u);
    int2 wrapped = (texel % size + size) % size;
    int2 mirrored = (texel % (size * 2) + size * 2) % (size * 2);
    mirrored = min(mirrored, size * 2 - 1 - mirrored);
    int2 coord = int2(addressMode.x == 0 ? wrapped.x : addressMode.x == 1 ? mirrored.x : clamp(texel.x, 0, size.x - 1),
                      addressMode.y == 0 ? wrapped.y : addressMode.y == 1 ? mirrored.y : clamp(texel.y, 0, size.y - 1));
    return localLoadColor(slot, int3(coord, 0));
}

float4 localHardwareGrad(Texture2D texture, uint samplerIndex, float2 uv, float2 dx, float2 dy, float2 size) {
    switch (samplerIndex) {
    case 1: return texture.SampleGrad(gLinearWrapWrapSampler, uv, dx, dy);
    case 2: return texture.SampleGrad(gLinearWrapMirrorSampler, uv, dx, dy);
    case 3: return texture.SampleGrad(gLinearWrapClampSampler, uv, dx, dy);
    case 4: return texture.SampleGrad(gLinearMirrorWrapSampler, uv, dx, dy);
    case 5: return texture.SampleGrad(gLinearMirrorMirrorSampler, uv, dx, dy);
    case 6: return texture.SampleGrad(gLinearMirrorClampSampler, uv, dx, dy);
    case 7: return texture.SampleGrad(gLinearClampWrapSampler, uv, dx, dy);
    case 8: return texture.SampleGrad(gLinearClampMirrorSampler, uv, dx, dy);
    case 9: return texture.SampleGrad(gLinearClampClampSampler, uv, dx, dy);
    default: return texture.SampleGrad(gLinearClampClampSampler, uv, dx, dy);
    }
}

float4 localGrad(uint slot, uint samplerIndex, float2 uv, float2 dx, float2 dy, float2 size) {
    switch (slot) {
    case 0: return localHardwareGrad(gTextures[0], samplerIndex, uv, dx, dy, size);
    case 1: return localHardwareGrad(gTextures[1], samplerIndex, uv, dx, dy, size);
    case 2: return localHardwareGrad(gTextures[2], samplerIndex, uv, dx, dy, size);
    case 3: return localHardwareGrad(gTextures[3], samplerIndex, uv, dx, dy, size);
    case 4: return localHardwareGrad(gTextures[4], samplerIndex, uv, dx, dy, size);
    case 5: return localHardwareGrad(gTextures[5], samplerIndex, uv, dx, dy, size);
    case 6: return localHardwareGrad(gTextures[6], samplerIndex, uv, dx, dy, size);
    case 7: return localHardwareGrad(gTextures[7], samplerIndex, uv, dx, dy, size);
    default: return 0.0f;
    }
}

