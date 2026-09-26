// Restore N64 coverage in primary alpha before resolve/readback/feedback.
#if defined(MULTISAMPLING)
Texture2DMS<float4> gInput : register(t1);
float4 PSMain(float4 pos : SV_Position, uint sampleIndex : SV_SampleIndex) : SV_TARGET {
    return gInput.Load(int2(pos.xy), sampleIndex);
}
#else
Texture2D<float4> gInput : register(t1);
float4 PSMain(float4 pos : SV_Position) : SV_TARGET {
    return gInput.Load(int3(int2(pos.xy), 0));
}
#endif
