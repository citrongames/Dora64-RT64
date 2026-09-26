// Native framebuffer stores through 32-bit storage buffers.
// One invocation owns a full word: adjacent 8/16-bit pixels never race.
#include "Depth.hlsli"
#include "FbCommon.hlsli"
#include "Random.hlsli"

[[vk::push_constant]] ConstantBuffer<FbCommonCB> gConstants : register(b0, space0);
RWStructuredBuffer<uint> gOutput : register(u1, space0);
#if defined(PACKED_DEPTH) && defined(MULTISAMPLING)
Texture2DMS<float> gInput : register(t0, space1);
#elif defined(PACKED_DEPTH)
Texture2D<float> gInput : register(t0, space1);
#else
Texture2D<float4> gInput : register(t0, space1);
#endif

uint nativePixel(uint pixelIndex) {
    const uint2 pixelCoord = uint2(pixelIndex % gConstants.resolution.x, pixelIndex / gConstants.resolution.x);
#ifdef PACKED_DEPTH
#ifdef MULTISAMPLING
    float depth = gInput.Load(pixelCoord, 0);
#else
    float depth = gInput.Load(uint3(pixelCoord, 0));
#endif
    return EndianSwapUINT16(FloatToDepth16(clamp(depth, 0.0f, 1.0f), 0.0f));
#else
    const float4 color = gInput.Load(uint3(pixelCoord, 0));
    uint seed = initRand(gConstants.ditherRandomSeed, pixelIndex, 16);
    const uint dither = DitherPatternValue(gConstants.ditherPattern, pixelCoord, seed);
    const uint value = Float4ToUINT(color, gConstants.siz, gConstants.fmt, (pixelCoord.x & 1) != 0,
        dither, gConstants.usesHDR);
    return EndianSwapUINT(value, gConstants.siz);
#endif
}

[numthreads(FB_COMMON_WORKGROUP_SIZE, FB_COMMON_WORKGROUP_SIZE, 1)]
void CSMain(uint2 coord : SV_DispatchThreadID) {
    if (coord.x >= gConstants.resolution.x || coord.y >= gConstants.resolution.y) return;
    // NativeTarget submits complete rows, with offset.x == 0.
    const uint bits = 4u << gConstants.siz;
    const uint pixelsPerWord = 32u / bits;
    const uint firstPixel = gConstants.offset.y * gConstants.resolution.x;
    const uint endPixel = firstPixel + gConstants.resolution.x * gConstants.resolution.y;
    const uint word = firstPixel / pixelsPerWord + coord.y * gConstants.resolution.x + coord.x;
    if (word >= (endPixel + pixelsPerWord - 1) / pixelsPerWord) return;
    const uint basePixel = word * pixelsPerWord;
    const uint mask = 0xffffffffu >> (32u - bits);
    // Preserve bytes outside a partially updated first/last word.
    uint packed = (basePixel < firstPixel || basePixel + pixelsPerWord > endPixel) ? gOutput[word] : 0;
    for (uint lane = 0; lane < pixelsPerWord; ++lane) {
        const uint pixel = basePixel + lane;
        if (pixel >= firstPixel && pixel < endPixel) {
            const uint shift = lane * bits;
            packed = (packed & ~(mask << shift)) | ((nativePixel(pixel) & mask) << shift);
        }
    }
    gOutput[word] = packed;
}
