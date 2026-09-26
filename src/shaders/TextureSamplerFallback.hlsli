// Explicit-LOD replacement for SampleGrad on drivers that reject the dynamic
// texture array + gradient sampling combination. Only compiled into the retry
// shader; ordinary textures still use the original N64 sampling paths.
#pragma once

float4 sampleNativeMipLevel(Texture2D texture, uint nativeSampler, float2 uv, float mip) {
    switch (nativeSampler) {
        case NATIVE_SAMPLER_WRAP_WRAP:     return texture.SampleLevel(gLinearWrapWrapSampler, uv, mip);
        case NATIVE_SAMPLER_WRAP_MIRROR:   return texture.SampleLevel(gLinearWrapMirrorSampler, uv, mip);
        case NATIVE_SAMPLER_WRAP_CLAMP:    return texture.SampleLevel(gLinearWrapClampSampler, uv, mip);
        case NATIVE_SAMPLER_MIRROR_WRAP:   return texture.SampleLevel(gLinearMirrorWrapSampler, uv, mip);
        case NATIVE_SAMPLER_MIRROR_MIRROR: return texture.SampleLevel(gLinearMirrorMirrorSampler, uv, mip);
        case NATIVE_SAMPLER_MIRROR_CLAMP:  return texture.SampleLevel(gLinearMirrorClampSampler, uv, mip);
        case NATIVE_SAMPLER_CLAMP_WRAP:    return texture.SampleLevel(gLinearClampWrapSampler, uv, mip);
        case NATIVE_SAMPLER_CLAMP_MIRROR:  return texture.SampleLevel(gLinearClampMirrorSampler, uv, mip);
        default:                         return texture.SampleLevel(gLinearClampClampSampler, uv, mip);
    }
}

float4 sampleNativeGradFallback(Texture2D texture, uint nativeSampler, float2 uv,
        float2 ddxUV, float2 ddyUV, float2 textureSize) {
    // Principal axes of the texel-space footprint (J * transpose(J)). This
    // also handles skewed gradients, where choosing just ddx or ddy loses detail.
    float2 dx = ddxUV * textureSize;
    float2 dy = ddyUV * textureSize;
    float a = dx.x * dx.x + dy.x * dy.x;
    float b = dx.x * dx.y + dy.x * dy.y;
    float c = dx.y * dx.y + dy.y * dy.y;
    float discriminant = sqrt(max((a - c) * (a - c) + 4.0f * b * b, 0.0f));
    float majorSquared = max(0.5f * (a + c + discriminant), 1.0e-12f);
    // det(J)^2 / lambdaMax is more stable than subtracting close eigenvalues.
    float determinant = dx.x * dy.y - dx.y * dy.x;
    float minorSquared = max(determinant * determinant / majorSquared, 0.0f);
    float major = sqrt(majorSquared);
    float minor = sqrt(minorSquared);

    // Match RT64's maximum 16x anisotropy. Widen the minor axis at the cap
    // instead of undersampling, and avoid extra taps for magnified textures.
    float footprintWidth = max(minor, major / 16.0f);
    uint taps = uint(clamp(ceil(major / max(footprintWidth, 1.0f)), 1.0f, 16.0f));
    float mip = log2(max(footprintWidth, 1.0e-6f));
    // Vulkan applies the existing sampler's -0.25 LOD bias to explicit LOD too.
    // Do not add it here a second time. SampleLevel keeps trilinear mip filtering.
    if (taps == 1) {
        return sampleNativeMipLevel(texture, nativeSampler, uv, mip);
    }

    float2 axis = (a >= c) ? float2(majorSquared - c, b) : float2(b, majorSquared - a);
    axis *= rsqrt(max(dot(axis, axis), 1.0e-12f));
    float2 span = axis * major / textureSize;
    float4 color = 0.0f;
#if defined(DYNAMIC_RENDER_PARAMS)
    [loop]
    for (uint i = 0; i < taps; i++) {
        float offset = (float(i) + 0.5f) / float(taps) - 0.5f;
        color += sampleNativeMipLevel(texture, nativeSampler, uv + span * offset, mip);
    }
#else
    // re-spirv cannot optimize loop constructs. Keep identical sampling, but
    // expose a fixed set of conditional taps for material specialization.
    [unroll]
    for (uint i = 0; i < 16; i++) {
        if (i < taps) {
            float offset = (float(i) + 0.5f) / float(taps) - 0.5f;
            color += sampleNativeMipLevel(texture, nativeSampler, uv + span * offset, mip);
        }
    }
#endif
    return color / float(taps);
}
