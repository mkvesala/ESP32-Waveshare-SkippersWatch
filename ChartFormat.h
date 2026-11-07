#pragma once

#include <stdint.h>
#include <stdio.h>
#include <cmath>
#include <cstring>

// === C H A R T  F O R M A T ===
//
// - The chart files on the microSD card, written by tools/chart/build_chart.py. Pure (no
//   Arduino), so host unit tests can exercise the addressing.
// - /chart/meta.bin: ChartMeta below. /chart/blocks/cCCCCrRRRR.bin: 16 x 16 tiles of
//   256 x 256 one-byte palette indices, tiles row by row, each tile row by row.
// - The tile grid is global (same origin everywhere), so a block file exists only where there
//   is chart data and a new chart area needs nothing but new block files.
// - Why blocks and not one big file: the card is FAT32 (exFAT is off in the Arduino core,
//   so 4 GB per file at most), and a seek walks the file's cluster chain, so its cost
//   grows with the file size.
// - Index NO_DATA_INDEX means no chart data. It is black in both palettes, so a hole in the
//   chart can never be mistaken for open water.
// - Keep in step with write_meta() in tools/chart/build_chart.py.

struct ChartFormat {

    static constexpr uint16_t VERSION        = 1;
    static constexpr int32_t  TILE_PX        = 256;
    static constexpr int32_t  BLOCK_TILES    = 16;
    static constexpr uint32_t TILE_BYTES     = (uint32_t)TILE_PX * TILE_PX;
    static constexpr uint32_t BLOCK_BYTES    = TILE_BYTES * BLOCK_TILES * BLOCK_TILES;
    static constexpr uint8_t  NO_DATA_INDEX  = 255;
    static constexpr size_t   PALETTE_COUNT  = 256;
    static constexpr size_t   BLOCK_PATH_LEN = 40;

    static constexpr const char* META_PATH = "/chart/meta.bin";

    // Little-endian on the card and in the ESP32-S3, read straight into this struct
    struct Meta {
        char     magic[4];       // "SWCH"
        uint16_t version;
        uint16_t tile_px;
        uint16_t block_tiles;
        uint8_t  no_data_index;
        uint8_t  reserved0;
        uint32_t reserved1;
        double   m_per_px;
        double   origin_e;       // grid easting of pixel (0, 0), m
        double   origin_n;       // grid northing of pixel (0, 0), m
        uint16_t palette_day[PALETTE_COUNT];     // RGB565
        uint16_t palette_night[PALETTE_COUNT];   // RGB565
    };
    static_assert(sizeof(Meta) == 1064, "must match write_meta() in build_chart.py");

    static bool isValid(const Meta& m) {
        return std::memcmp(m.magic, "SWCH", 4) == 0 && m.version == VERSION &&
               m.tile_px == TILE_PX && m.block_tiles == BLOCK_TILES &&
               m.no_data_index == NO_DATA_INDEX && m.m_per_px > 0.0;
    }

    // The rectangle of chart pixels on view. Any size, so a bow-up view can later compose a
    // larger square and rotate it.
    struct View {
        int32_t  x0;
        int32_t  y0;
        uint16_t w;
        uint16_t h;
    };

    // One tile's part of a view
    struct Span {
        int32_t  col;
        int32_t  row;
        uint16_t tile_x;
        uint16_t tile_y;
        uint16_t view_x;
        uint16_t view_y;
        uint16_t w;
        uint16_t h;
    };

    // Chart pixel coordinates of a grid point
    static void toPixel(const Meta& m, double e, double n, double& px, double& py) {
        px = (e - m.origin_e) / m.m_per_px;
        py = (m.origin_n - n) / m.m_per_px;
    }

    static View centeredView(double px, double py, uint16_t w, uint16_t h) {
        return { (int32_t)std::floor(px) - w / 2, (int32_t)std::floor(py) - h / 2, w, h };
    }

    static int32_t floorDiv(int32_t a, int32_t b) {
        const int32_t q = a / b;
        return (a % b != 0 && a < 0) ? q - 1 : q;
    }

    // Calls fn(const Span&) for each tile the view touches, row by row
    template <typename F>
    static void forEachSpan(const View& v, F fn) {
        const int32_t last_row = floorDiv(v.y0 + v.h - 1, TILE_PX);
        const int32_t last_col = floorDiv(v.x0 + v.w - 1, TILE_PX);
        for (int32_t row = floorDiv(v.y0, TILE_PX); row <= last_row; ++row) {
            const int32_t top = row * TILE_PX;
            const int32_t y1 = top > v.y0 ? top : v.y0;
            const int32_t y2 = (top + TILE_PX) < (v.y0 + v.h) ? (top + TILE_PX) : (v.y0 + v.h);
            for (int32_t col = floorDiv(v.x0, TILE_PX); col <= last_col; ++col) {
                const int32_t left = col * TILE_PX;
                const int32_t x1 = left > v.x0 ? left : v.x0;
                const int32_t x2 = (left + TILE_PX) < (v.x0 + v.w) ? (left + TILE_PX) : (v.x0 + v.w);
                const Span s = { col, row,
                                 (uint16_t)(x1 - left), (uint16_t)(y1 - top),
                                 (uint16_t)(x1 - v.x0), (uint16_t)(y1 - v.y0),
                                 (uint16_t)(x2 - x1), (uint16_t)(y2 - y1) };
                fn(s);
            }
        }
    }

    // A span's pixels into an RGB565 view. src holds the span's whole tile rows (TILE_PX bytes
    // each), first the row at s.tile_y; out is the view's top left pixel.
    static void expandSpan(const Span& s, const uint8_t* src, const uint16_t* palette,
                           uint16_t* out, uint16_t view_w) {
        for (uint16_t y = 0; y < s.h; ++y) {
            const uint8_t* line = src + (size_t)y * TILE_PX + s.tile_x;
            uint16_t* dst = out + (size_t)(s.view_y + y) * view_w + s.view_x;
            for (uint16_t x = 0; x < s.w; ++x) dst[x] = palette[line[x]];
        }
    }

    // A span with no chart data: black, never something that could pass for open water
    static void blankSpan(const Span& s, uint16_t* out, uint16_t view_w) {
        for (uint16_t y = 0; y < s.h; ++y) {
            uint16_t* dst = out + (size_t)(s.view_y + y) * view_w + s.view_x;
            for (uint16_t x = 0; x < s.w; ++x) dst[x] = 0;
        }
    }

    // --- Bow-up: a square of palette indices around the boat, rotated into the view ---

    // A span's palette indices into an index buffer of out_w columns, no palette applied
    static void copySpanIndexed(const Span& s, const uint8_t* src, uint8_t* out, uint16_t out_w) {
        for (uint16_t y = 0; y < s.h; ++y) {
            std::memcpy(out + (size_t)(s.view_y + y) * out_w + s.view_x,
                        src + (size_t)y * TILE_PX + s.tile_x, s.w);
        }
    }

    static void blankSpanIndexed(const Span& s, uint8_t* out, uint16_t out_w) {
        for (uint16_t y = 0; y < s.h; ++y) {
            std::memset(out + (size_t)(s.view_y + y) * out_w + s.view_x, NO_DATA_INDEX, s.w);
        }
    }

    // Fills the RGB565 view out (out_w x out_h) from the index buffer src so that grid bearing
    // rot_deg points up, nearest neighbour. The view's center (out_w / 2, out_h / 2) shows the
    // source point (src_cx, src_cy), in source pixels. Outside the source is black, like a
    // hole in the chart. Fixed point 16.16, stepped along each row: two adds per pixel.
    // True when any pixel on view has chart data.
    static bool rotateIndexed(const uint8_t* src, uint16_t src_w, uint16_t src_h,
                              double src_cx, double src_cy, double rot_deg,
                              const uint16_t* palette, uint16_t* out, uint16_t out_w, uint16_t out_h) {
        constexpr double ONE = 65536.0;
        const double r = rot_deg * 0.017453292519943295;
        const double c = std::cos(r);
        const double s = std::sin(r);
        // Screen offset (u, v) → source offset: (u c - v s, u s + v c)
        const int32_t step_x_u = (int32_t)std::lround(c * ONE);
        const int32_t step_y_u = (int32_t)std::lround(s * ONE);
        const int32_t step_x_v = (int32_t)std::lround(-s * ONE);
        const int32_t step_y_v = (int32_t)std::lround(c * ONE);
        const int32_t lim_x = (int32_t)src_w << 16;
        const int32_t lim_y = (int32_t)src_h << 16;

        // Pixel centers: the first one is half a pixel in from the view's corner
        const double u0 = 0.5 - out_w / 2;
        const double v0 = 0.5 - out_h / 2;
        int32_t row_x = (int32_t)std::lround((src_cx + u0 * c - v0 * s) * ONE);
        int32_t row_y = (int32_t)std::lround((src_cy + u0 * s + v0 * c) * ONE);

        bool any_data = false;
        for (uint16_t y = 0; y < out_h; ++y) {
            int32_t fx = row_x;
            int32_t fy = row_y;
            uint16_t* dst = out + (size_t)y * out_w;
            for (uint16_t x = 0; x < out_w; ++x) {
                if (fx >= 0 && fy >= 0 && fx < lim_x && fy < lim_y) {
                    const uint8_t idx = src[(size_t)(fy >> 16) * src_w + (size_t)(fx >> 16)];
                    any_data |= idx != NO_DATA_INDEX;
                    dst[x] = palette[idx];
                } else {
                    dst[x] = 0;
                }
                fx += step_x_u;
                fy += step_y_u;
            }
            row_x += step_x_v;
            row_y += step_y_v;
        }
        return any_data;
    }

    // False for a tile outside the grid: no such file can exist
    static bool blockPath(int32_t col, int32_t row, char* out, size_t len) {
        if (col < 0 || row < 0) return false;
        snprintf(out, len, "/chart/blocks/c%04ldr%04ld.bin",
                 (long)(col / BLOCK_TILES), (long)(row / BLOCK_TILES));
        return true;
    }

    // Byte offset of a tile's pixel row inside its block file
    static uint32_t blockOffset(int32_t col, int32_t row, int32_t tile_y) {
        const uint32_t tile_index = (uint32_t)(row % BLOCK_TILES) * BLOCK_TILES + (uint32_t)(col % BLOCK_TILES);
        return tile_index * TILE_BYTES + (uint32_t)tile_y * TILE_PX;
    }

};
