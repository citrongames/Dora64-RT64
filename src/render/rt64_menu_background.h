// Doraemon's menu backdrop is two 48x72 halves repeated in a 3x3 grid.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace RT64::MenuBackground {
    constexpr int TileWidth = 48;
    constexpr int TileHeight = 72;
    constexpr int Columns = 3;
    constexpr int Rows = 3;
    constexpr int TileCount = 2 * Columns * Rows;
    constexpr int RepeatWidth = 2 * TileWidth;
    constexpr int MosaicWidth = RepeatWidth * Columns;

    inline bool isRepeatSource(int tileIndex) { return (tileIndex % Columns) == 0; }

    // Rectangle coordinates are RDP quarter-pixel units, not output pixels.
    struct Tile {
        int32_t left, top, right, bottom;
        uint64_t textureHash;
    };

    inline bool matches(const std::array<Tile, TileCount>& tiles,
        int32_t scissorLeft, int32_t scissorTop, int32_t scissorRight, int32_t scissorBottom) {
        const int32_t left = tiles[0].left;
        const int32_t top = tiles[0].top;
        if ((tiles[0].textureHash == 0) || (tiles[Rows * Columns].textureHash == 0) ||
            (tiles[0].textureHash == tiles[Rows * Columns].textureHash)) {
            return false;
        }
        // File select uses a full-screen scissor. In-game menus crop the
        // last pixel, and RT64 has already clipped their rectangle edges.
        if ((scissorLeft > left) || (scissorTop > top) ||
            (scissorRight < left + MosaicWidth * 4 - 4) ||
            (scissorBottom < top + Rows * TileHeight * 4 - 4)) return false;
        for (int half = 0; half < 2; half++) {
            for (int row = 0; row < Rows; row++) {
                for (int col = 0; col < Columns; col++) {
                    const auto& tile = tiles[half * Rows * Columns + row * Columns + col];
                    const int32_t x = left + (half + col * 2) * TileWidth * 4;
                    const int32_t y = top + row * TileHeight * 4;
                    if ((tile.left != x) || (tile.top != y) ||
                        ((tile.right != x + TileWidth * 4) &&
                         (tile.right != std::min(x + TileWidth * 4, scissorRight))) ||
                        ((tile.bottom != y + TileHeight * 4) &&
                         (tile.bottom != std::min(y + TileHeight * 4, scissorBottom))) ||
                        (tile.textureHash != tiles[half * Rows * Columns].textureHash)) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    struct Placement { float left, right; };

    inline Placement placeRepeat(float nativeLeft, float nativeRight, float nativePeriod,
        float scale, float center, int32_t repeat) {
        // Translate in native coordinates, then round both edges exactly once.
        // Rounding each tile's width and multiplying it creates visible seams.
        return { std::round(center + (nativeLeft + repeat * nativePeriod) * scale),
            std::round(center + (nativeRight + repeat * nativePeriod) * scale) };
    }

    struct Repeats { int32_t first = 0, last = 0; };

    inline Repeats visibleRepeats(float left, float width, float period, float outputWidth) {
        if (!(period > 0.0f) || !(width > 0.0f) || !(outputWidth > 0.0f)) {
            return {};
        }
        // Retain only copies intersecting [0, outputWidth); touching an edge
        // without covering a pixel does not require a draw.
        return { int32_t(std::floor((-left - width) / period)) + 1,
            int32_t(std::ceil((outputWidth - left) / period)) - 1 };
    }
}
