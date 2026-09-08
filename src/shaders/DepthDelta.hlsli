//
// RT64
//

#pragma once

// Return the RDP's effective comparison tolerance for interpolated depth.
// Triangle derivatives use 15-bit integer depth units. The RDP truncates each
// magnitude and normalizes their sum upward to a power of two before comparing.
// The final << 3 converts to 18-bit Z units; after normalization to [0, 1],
// this is the same as dividing by 32768, not multiplying the float slope by 8.
float RasterDepthDelta(float dzdx, float dzdy) {
    const float DepthScale = 32768.0f;
    const uint dx = uint(min(abs(dzdx) * DepthScale, 32767.0f));
    const uint dy = uint(min(abs(dzdy) * DepthScale, 32767.0f));
    const uint sum = dx + dy;
    // Zero normalizes to 1. The special sum=1 result (3) becomes 2 in the
    // RDP comparator's highest-set-bit selection. Large slopes saturate.
    const uint exponent = (sum > 0) ? min(uint(firstbithigh(sum)) + 1U, 15U) : 0U;
    return float(1U << exponent) / DepthScale;
}

// HLE depth targets do not store the RDP's per-pixel dz exponent. Recover a
// conservative slope from the depth image. Two consecutive samples on a plane
// extrapolate back to its center; a silhouette generally fails this check.
float RasterSurfaceDepthDerivative(float center, float negative1, float negative2,
    float positive1, float positive2) {
    const float negativeError = abs(2.0f * negative1 - negative2 - center);
    const float positiveError = abs(2.0f * positive1 - positive2 - center);
    // Allow float depth roundoff (16 ULPs at 1), not a geometric depth gap.
    const float ExtrapolationRoundoff = 1.0f / 1048576.0f;
    if (min(negativeError, positiveError) > ExtrapolationRoundoff) {
        return 0.0f;
    }

    // The smaller one-sided difference avoids inflating dz across an edge even
    // if samples on the other side happen to extrapolate to the center.
    return min(abs(center - negative1), abs(positive1 - center));
}

float RasterDecalDepthTolerance(float decalDelta, float surfaceDelta, uint depthExponent) {
    // RDP z_compare adjusts the stored delta for the three coarsest Z encodings,
    // then compares against the larger incoming/stored power-of-two delta.
    // Both arguments are normalized 15-bit dz units (not 18-bit Z integers).
    if (depthExponent < 3U) {
        surfaceDelta = min(1.0f, max(surfaceDelta * 2.0f,
            float(16U >> depthExponent) / 32768.0f));
    }
    return max(decalDelta, surfaceDelta);
}
