// Native RAM is packed into words, but texel-buffer views are limited to
// maxTexelBufferElements (often 65536, less than a 320x240 framebuffer).
// Storage buffers preserve the same bytes without that texel-count limit.
#ifdef NATIVE_FB_WORDS
#define NATIVE_FB_INPUT StructuredBuffer<uint>
#else
#define NATIVE_FB_INPUT Buffer<uint>
#endif

uint loadNativePixel(NATIVE_FB_INPUT inputBuffer, uint pixelIndex, uint siz) {
#ifdef NATIVE_FB_WORDS
    const uint bits = 4u << siz;
    const uint pixelsPerWord = 32u / bits;
    const uint word = inputBuffer[pixelIndex / pixelsPerWord];
    const uint shift = (pixelIndex % pixelsPerWord) * bits;
    return (word >> shift) & (0xffffffffu >> (32u - bits));
#else
    return inputBuffer[pixelIndex];
#endif
}
