// Doraemon's final textured overlay covers the original 288x216 safe area.
#pragma once

#include <cstdint>

namespace RT64::ScreenTransition {
    inline bool matches(int32_t left, int32_t top, int32_t right, int32_t bottom,
        int32_t scissorLeft, int32_t scissorRight, uint32_t combineL, uint32_t combineH) {
        // RT64 may have clipped the last source pixel. A full-screen scissor
        // is valid too: the overlay geometry need not cover that larger area.
        // Keep intentionally narrow horizontal reveal windows out of this rule.
        return (left == 64) && (top == 48) &&
            ((right == 1212) || (right == 1216)) &&
            ((bottom == 908) || (bottom == 912)) &&
            (scissorLeft <= 64) && (scissorRight >= 1212) &&
            ((combineL & 0x00FFFFFFU) == 0x00119623U) && (combineH == 0xFF2FFFFFU);
    }
}
