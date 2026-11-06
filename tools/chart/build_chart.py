#!/usr/bin/env python3
"""Sea chart files for the Skipper's Watch microSD card, from Traficom's open WMTS service.

The watch never fetches anything: this tool downloads the chart once on a Mac, converts it
to the watch's format (ChartFormat.h) and copies it onto the card.

    fetch     download the source tiles into a cache (resumes where it stopped)
    build     resample to the watch's scale, build the palette, write the block files
    simulate  compose a view with the watch's own formulas, as a PNG
    install   copy the files onto a FAT32 card and compare them. Never formats anything.

A small test area first, the demo position +- 3 km:

    build_chart.py fetch --center 60.0833 24.8333 --radius-km 3
    build_chart.py build --center 60.0833 24.8333 --radius-km 3 --out /tmp/chart
    build_chart.py simulate --out /tmp/chart --lat 60.0833 --lon 24.8333 --png /tmp/view.png

The whole series with no area arguments. Scale and source level are experiments that need no
firmware change: --m-per-px and --source-level (the watch reads m/px from meta.bin).

Needs: Pillow, numpy, pyproj, and curl. Tiles go through curl, because a python.org Python
may not trust the system's certificates. Source: (c) Traficom, CC BY 4.0. Not for navigation:
the watch is an aid and does not replace the official chart.
"""

import argparse
import filecmp
import functools
import json
import math
import os
import plistlib
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET
from pathlib import Path
from urllib.parse import quote

import numpy as np
import pyproj
from PIL import Image, ImageDraw

# --- Service ---
WMTS_BASE = "https://julkinen.traficom.fi/rasteripalvelu/wmts"
DEFAULT_LAYER = "Traficom:Merikarttasarja A public"
TILE_MATRIX_SET = "ETRS89_TM35-FIN"
USER_AGENT = "SkippersWatch-chart-tool"
REQUESTS_PER_S = 5.0
RETRIES = 3

# --- The service's TM35FIN grid: level L is 8192 / 2^L m/px, tiles 256 px ---
ORIGIN_E = -548576.0
ORIGIN_N = 8388608.0
SOURCE_TILE_PX = 256
LIMITS_LEVEL = 11    # emptiness is checked here before going to a finer level
DEFAULT_SOURCE_LEVEL = 12
DEFAULT_M_PER_PX = 4.0

# --- Watch format, keep in step with ChartFormat.h ---
FORMAT_VERSION = 1
TILE_PX = 256
BLOCK_TILES = 16
TILE_BYTES = TILE_PX * TILE_PX
BLOCK_BYTES = TILE_BYTES * BLOCK_TILES * BLOCK_TILES
NO_DATA_INDEX = 255
META_FORMAT = "<4sHHHBBIddd256H256H"
META_SIZE = 1064

# --- Palette ---
PALETTE_SAMPLE_TILES = 200
BASE_COLOR_MIN_SHARE = 0.0001   # a flat source color this common keeps its exact value
NIGHT_LEVEL = 0.85

# --- Simulated view, as drawn on the watch ---
VIEW_W = 410
VIEW_H = 502
TOP_BAR_H = 24
RING_RADIUS_NM = 0.25
COG_VECTOR_MIN = 3.0
METERS_PER_NM = 1852.0
LINE_W_PX = 4
DASH_PX = 10
DASH_GAP_PX = 6
BOAT_R_PX = 8
SYMBOL_DAY = (235, 0, 0)
SYMBOL_NIGHT = (190, 190, 190)

CACHE_ROOT = Path.home() / ".cache" / "skippers-watch-chart"

_to_tm35 = pyproj.Transformer.from_crs("EPSG:4326", "EPSG:3067", always_xy=True)


def source_m_per_px(level):
    return 8192.0 / (2 ** level)


def cache_dir(layer):
    return CACHE_ROOT / layer.replace(":", "_").replace(" ", "_")


def tile_path(layer, level, row, col):
    return cache_dir(layer) / str(level) / str(row) / f"{col}.png"


def empty_marker(path):
    return path.with_suffix(".empty")


# === Area ===

def areas_from_args(args):
    """A list of (e_min, n_min, e_max, n_max) in metres; [None] means the whole layer."""
    areas = []
    if args.center:
        lat, lon = args.center
        e, n = _to_tm35.transform(lon, lat)
        r = args.radius_km * 1000.0
        areas.append((e - r, n - r, e + r, n + r))
    for lat0, lon0, lat1, lon1 in args.bbox or []:
        corners = [_to_tm35.transform(lon, lat) for lat in (lat0, lat1) for lon in (lon0, lon1)]
        es, ns = zip(*corners)
        areas.append((min(es), min(ns), max(es), max(ns)))
    return areas or [None]


def add_area_args(parser):
    parser.add_argument("--layer", default=DEFAULT_LAYER)
    parser.add_argument("--source-level", type=int, default=DEFAULT_SOURCE_LEVEL)
    parser.add_argument("--center", type=float, nargs=2, metavar=("LAT", "LON"))
    parser.add_argument("--radius-km", type=float, default=3.0)
    parser.add_argument("--bbox", type=float, nargs=4, action="append",
                        metavar=("LAT0", "LON0", "LAT1", "LON1"),
                        help="opposite corners; can be given several times, also with --center")


def layer_limits(layer, level):
    """(min_row, max_row, min_col, max_col) of a level, from GetCapabilities (cached)."""
    path = CACHE_ROOT / "capabilities.xml"
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        tmp = path.with_suffix(".tmp")
        for attempt in range(RETRIES):
            code = curl(f"{WMTS_BASE}?request=getcapabilities", tmp)
            if code == 200:
                tmp.rename(path)
                break
            time.sleep(2 ** attempt)
        else:
            sys.exit(f"GetCapabilities failed: HTTP {code}")
    ns = {"w": "http://www.opengis.net/wmts/1.0", "o": "http://www.opengis.net/ows/1.1"}
    root = ET.parse(path).getroot()
    for lyr in root.findall(".//w:Contents/w:Layer", ns):
        if lyr.find("o:Identifier", ns).text != layer:
            continue
        for link in lyr.findall("w:TileMatrixSetLink", ns):
            if link.find("w:TileMatrixSet", ns).text != TILE_MATRIX_SET:
                continue
            for m in link.findall(".//w:TileMatrixLimits", ns):
                if m.find("w:TileMatrix", ns).text == f"{TILE_MATRIX_SET}:{level}":
                    return tuple(int(m.find(f"w:{k}", ns).text) for k in
                                 ("MinTileRow", "MaxTileRow", "MinTileCol", "MaxTileCol"))
    sys.exit(f"Layer {layer!r} has no level {level} in {TILE_MATRIX_SET}")


def tiles_for_area(layer, level, area):
    """Rows and columns of a level, the layer's limits cut down to the area."""
    r0, r1, c0, c1 = layer_limits(layer, level)
    if area is not None:
        size = source_m_per_px(level) * SOURCE_TILE_PX
        e0, n0, e1, n1 = area
        c0 = max(c0, math.floor((e0 - ORIGIN_E) / size))
        c1 = min(c1, math.floor((e1 - ORIGIN_E) / size))
        r0 = max(r0, math.floor((ORIGIN_N - n1) / size))
        r1 = min(r1, math.floor((ORIGIN_N - n0) / size))
    return range(r0, r1 + 1), range(c0, c1 + 1)


# === fetch ===

def curl(url, out_path):
    """HTTP status of a download into out_path; 0 when curl itself failed."""
    result = subprocess.run(
        ["curl", "-sS", "-m", "30", "-A", USER_AGENT, "-w", "%{http_code}", "-o", str(out_path), url],
        capture_output=True, text=True)
    if result.stderr:
        print(f"  curl: {result.stderr.strip()}")
    try:
        return int(result.stdout or 0)
    except ValueError:
        return 0


def tile_url(layer, level, row, col):
    return (f"{WMTS_BASE}/rest/{quote(layer)}//{TILE_MATRIX_SET}/{TILE_MATRIX_SET}:{level}"
            f"/{row}/{col}?format=image/png")


def is_transparent(path):
    with Image.open(path) as im:
        extrema = im.convert("RGBA").getchannel("A").getextrema()
    return extrema[1] == 0


class Fetcher:

    def __init__(self, layer):
        self.layer = layer
        self.last_request = 0.0
        self.fetched = 0
        self.failed = 0

    def get(self, level, row, col):
        """True when the tile has chart data. A fully transparent one leaves only a marker,
        and a failed one nothing, so the next run tries it again."""
        path = tile_path(self.layer, level, row, col)
        if path.exists():
            return True
        if empty_marker(path).exists():
            return False
        path.parent.mkdir(parents=True, exist_ok=True)
        tmp = path.with_suffix(".tmp")
        for attempt in range(RETRIES):
            wait = self.last_request + 1.0 / REQUESTS_PER_S - time.monotonic()
            if wait > 0:
                time.sleep(wait)
            self.last_request = time.monotonic()
            code = curl(tile_url(self.layer, level, row, col), tmp)
            if code == 200:
                self.fetched += 1
                if is_transparent(tmp):
                    tmp.unlink()
                    empty_marker(path).touch()
                    return False
                tmp.rename(path)
                return True
            time.sleep(2 ** attempt)
        tmp.unlink(missing_ok=True)
        self.failed += 1
        print(f"  failed: level {level} row {row} col {col}, last HTTP {code}")
        return False


def cmd_fetch(args):
    if args.source_level < LIMITS_LEVEL:
        sys.exit(f"--source-level must be at least {LIMITS_LEVEL}")
    fetcher = Fetcher(args.layer)
    cells = set()
    for area in areas_from_args(args):
        rows, cols = tiles_for_area(args.layer, LIMITS_LEVEL, area)
        cells.update((row, col) for row in rows for col in cols)
    total = len(cells)
    scale = 2 ** (args.source_level - LIMITS_LEVEL)
    print(f"{args.layer}: {total} level {LIMITS_LEVEL} tiles to check, source level {args.source_level}")

    done = kept = 0
    started = time.monotonic()
    for row, col in sorted(cells):
        done += 1
        if fetcher.get(LIMITS_LEVEL, row, col):
            kept += 1
            if scale > 1:
                for r in range(row * scale, (row + 1) * scale):
                    for c in range(col * scale, (col + 1) * scale):
                        fetcher.get(args.source_level, r, c)
        if done % 50 == 0 or done == total:
            elapsed = time.monotonic() - started
            print(f"  {done}/{total} checked, {kept} with data, {fetcher.fetched} downloaded, "
                  f"{fetcher.failed} failed, {elapsed:.0f} s", flush=True)
    if fetcher.failed:
        sys.exit(f"{fetcher.failed} tiles failed. Run the same command again to fetch only those.")
    print("Done.")


# === build ===

@functools.lru_cache(maxsize=1500)
def load_source(layer, level, row, col):
    """A source tile as an RGBA array, None when it has no data or was not fetched."""
    path = tile_path(layer, level, row, col)
    if not path.exists():
        return None
    with Image.open(path) as im:
        return np.asarray(im.convert("RGBA"))


def resample_tile(layer, level, m_per_px, col, row):
    """One watch tile as (RGB image, data mask), or None when it has no chart data.

    The source mosaic reaches past the tile on every side and only `box` selects the tile,
    so the Lanczos filter sees the neighbours and no seams appear between tiles."""
    src_m = source_m_per_px(level)
    tile_m = TILE_PX * m_per_px
    x0 = (col * tile_m) / src_m
    y0 = (row * tile_m) / src_m
    size = tile_m / src_m
    pad = math.ceil(max(3.0, 3.0 * m_per_px / src_m))

    c0 = math.floor((x0 - pad) / SOURCE_TILE_PX)
    c1 = math.floor((x0 + size + pad) / SOURCE_TILE_PX)
    r0 = math.floor((y0 - pad) / SOURCE_TILE_PX)
    r1 = math.floor((y0 + size + pad) / SOURCE_TILE_PX)

    # Only the tile's own source tiles decide whether it exists; the neighbours are just pad
    c0_own = math.floor(x0 / SOURCE_TILE_PX)
    c1_own = math.ceil((x0 + size) / SOURCE_TILE_PX) - 1
    r0_own = math.floor(y0 / SOURCE_TILE_PX)
    r1_own = math.ceil((y0 + size) / SOURCE_TILE_PX) - 1
    if not any(load_source(layer, level, r, c) is not None
               for r in range(r0_own, r1_own + 1) for c in range(c0_own, c1_own + 1)):
        return None

    mosaic = np.zeros(((r1 - r0 + 1) * SOURCE_TILE_PX, (c1 - c0 + 1) * SOURCE_TILE_PX, 4), dtype=np.uint8)
    for r in range(r0, r1 + 1):
        for c in range(c0, c1 + 1):
            tile = load_source(layer, level, r, c)
            if tile is not None:
                top = (r - r0) * SOURCE_TILE_PX
                left = (c - c0) * SOURCE_TILE_PX
                mosaic[top:top + SOURCE_TILE_PX, left:left + SOURCE_TILE_PX] = tile

    # Transparent pixels over white, so no dark fringe bleeds in along the edge of the data
    alpha = mosaic[..., 3:4].astype(np.float32) / 255.0
    rgb = (mosaic[..., :3] * alpha + 255.0 * (1.0 - alpha)).astype(np.uint8)
    left = x0 - c0 * SOURCE_TILE_PX
    top = y0 - r0 * SOURCE_TILE_PX
    box = (left, top, left + size, top + size)
    out = (TILE_PX, TILE_PX)
    image = Image.fromarray(rgb).resize(out, Image.LANCZOS, box=box)
    mask = Image.fromarray(mosaic[..., 3]).resize(out, Image.BOX, box=box)
    return image, np.asarray(mask) >= 128


def watch_tiles(layer, level, m_per_px, area):
    """Every watch tile the source's area touches, row by row."""
    rows, cols = tiles_for_area(layer, level, area)
    src_size = source_m_per_px(level) * SOURCE_TILE_PX
    tile_m = TILE_PX * m_per_px
    w0 = math.floor(cols.start * src_size / tile_m)
    w1 = math.ceil(cols.stop * src_size / tile_m)
    h0 = math.floor(rows.start * src_size / tile_m)
    h1 = math.ceil(rows.stop * src_size / tile_m)
    return [(c, r) for r in range(h0, h1) for c in range(w0, w1)]


def has_own_data(layer, level, m_per_px, col, row):
    """Cheap check before decoding: does any of the tile's source files exist?"""
    src_px = source_m_per_px(level) * SOURCE_TILE_PX
    tile_m = TILE_PX * m_per_px
    return any(tile_path(layer, level, r, c).exists()
               for r in range(math.floor(row * tile_m / src_px), math.ceil((row + 1) * tile_m / src_px))
               for c in range(math.floor(col * tile_m / src_px), math.ceil((col + 1) * tile_m / src_px)))


def rgb565(rgb):
    r, g, b = int(rgb[0]), int(rgb[1]), int(rgb[2])
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def night_color(rgb):
    """Inverted red: light paper goes black, ink goes red, and color (land, shallows) keeps
    a dim red of its own so that it stays apart from the water."""
    r, g, b = float(rgb[0]), float(rgb[1]), float(rgb[2])
    luma = 0.299 * r + 0.587 * g + 0.114 * b
    chroma = max(r, g, b) - min(r, g, b)
    return (int(min(255.0, (255.0 - luma) + chroma) * NIGHT_LEVEL), 0, 0)


def build_palette(layer, level, m_per_px, tiles):
    """255 colors (the 256th is no data): the source's flat colors exactly, the anti-aliased
    blends of the Lanczos filter by median cut over a sample of the tiles."""
    step = max(1, len(tiles) // PALETTE_SAMPLE_TILES)
    sample = []
    for col, row in tiles[::step]:
        tile = resample_tile(layer, level, m_per_px, col, row)
        if tile is not None:
            sample.append(np.asarray(tile[0])[tile[1]])
    if not sample:
        sys.exit("No chart data in this area. Fetched already? Same area arguments as in fetch?")
    pixels = np.concatenate(sample)

    colors, counts = np.unique(pixels, axis=0, return_counts=True)
    base = [tuple(int(v) for v in c) for c, n in zip(colors, counts)
            if n >= BASE_COLOR_MIN_SHARE * len(pixels) and is_flat_source_color(c)]

    side = math.ceil(math.sqrt(len(pixels)))
    canvas = np.full((side, side, 3), 255, dtype=np.uint8)
    canvas.reshape(-1, 3)[:len(pixels)] = pixels
    cut = Image.fromarray(canvas).quantize(255 - len(base), method=Image.MEDIANCUT, dither=Image.NONE)
    blends = np.array(cut.getpalette()[:(255 - len(base)) * 3]).reshape(-1, 3)

    palette = base + [tuple(int(v) for v in c) for c in blends]
    palette = (palette + [(0, 0, 0)] * 255)[:255]
    print(f"Palette: {len(base)} exact source colors + {len(palette) - len(base)} blends")
    return palette


def is_flat_source_color(color):
    return tuple(int(v) for v in color) in SOURCE_FLAT_COLORS


SOURCE_FLAT_COLORS = set()


def collect_flat_colors(layer, level, tiles, m_per_px):
    """The colors of the source tiles themselves: a small set, there is no anti-aliasing."""
    src_px = source_m_per_px(level) * SOURCE_TILE_PX
    tile_m = TILE_PX * m_per_px
    step = max(1, len(tiles) // PALETTE_SAMPLE_TILES)
    for col, row in tiles[::step]:
        r = math.floor(row * tile_m / src_px)
        c = math.floor(col * tile_m / src_px)
        tile = load_source(layer, level, r, c)
        if tile is not None:
            opaque = tile[tile[..., 3] == 255][:, :3]
            SOURCE_FLAT_COLORS.update(tuple(int(v) for v in u) for u in np.unique(opaque, axis=0))


def write_meta(out_dir, palette, m_per_px, layer, level, tile_count, block_count):
    day = [rgb565(c) for c in palette] + [0]
    night = [rgb565(night_color(c)) for c in palette] + [0]
    data = struct.pack(META_FORMAT, b"SWCH", FORMAT_VERSION, TILE_PX, BLOCK_TILES, NO_DATA_INDEX,
                       0, 0, m_per_px, ORIGIN_E, ORIGIN_N, *day, *night)
    assert len(data) == META_SIZE
    (out_dir / "meta.bin").write_bytes(data)
    (out_dir / "meta.json").write_text(json.dumps({
        "format_version": FORMAT_VERSION,
        "source": f"{layer}, {TILE_MATRIX_SET} level {level}",
        "attribution": "(c) Traficom, CC BY 4.0. Not for navigation.",
        "m_per_px": m_per_px,
        "origin_e": ORIGIN_E,
        "origin_n": ORIGIN_N,
        "tile_px": TILE_PX,
        "block_tiles": BLOCK_TILES,
        "tiles_with_data": tile_count,
        "block_files": block_count,
        "built": time.strftime("%Y-%m-%d %H:%M:%S"),
    }, indent=2) + "\n")


def cmd_build(args):
    out_dir = Path(args.out)
    blocks_dir = out_dir / "blocks"
    if blocks_dir.exists():
        sys.exit(f"{blocks_dir} exists already. Build into an empty directory.")
    blocks_dir.mkdir(parents=True)

    level, m = args.source_level, args.m_per_px
    all_tiles = sorted({t for area in areas_from_args(args) for t in watch_tiles(args.layer, level, m, area)},
                       key=lambda t: (t[1], t[0]))
    tiles = [t for t in all_tiles if has_own_data(args.layer, level, m, *t)]
    print(f"{len(tiles)} of {len(all_tiles)} watch tiles have source data")
    if not tiles:
        sys.exit("No chart data in this area. Fetched already? Same area arguments as in fetch?")

    collect_flat_colors(args.layer, level, tiles, m)
    palette = build_palette(args.layer, level, m, tiles)
    palette_image = Image.new("P", (1, 1))
    # 256 entries, so that nothing can map to a stray one. No-data repeats the first color:
    # quantize finds the first match, and index 255 is only ever written by the mask below.
    palette_image.putpalette([v for c in palette + [palette[0]] for v in c])

    # Block by block in file order, so each block is built and written once
    by_block = {}
    for col, row in tiles:
        by_block.setdefault((col // BLOCK_TILES, row // BLOCK_TILES), []).append((col, row))

    written = 0
    started = time.monotonic()
    for i, ((block_col, block_row), members) in enumerate(sorted(by_block.items())):
        block = np.full(BLOCK_BYTES, NO_DATA_INDEX, dtype=np.uint8)
        tiles_in_block = 0
        for col, row in members:
            tile = resample_tile(args.layer, level, m, col, row)
            if tile is None:
                continue
            image, mask = tile
            indices = np.array(image.quantize(palette=palette_image, dither=Image.NONE), dtype=np.uint8)
            indices[~mask] = NO_DATA_INDEX
            offset = ((row % BLOCK_TILES) * BLOCK_TILES + (col % BLOCK_TILES)) * TILE_BYTES
            block[offset:offset + TILE_BYTES] = indices.reshape(-1)
            tiles_in_block += 1
        if tiles_in_block:
            (blocks_dir / f"c{block_col:04d}r{block_row:04d}.bin").write_bytes(block.tobytes())
            written += tiles_in_block
        print(f"  block {i + 1}/{len(by_block)}: {tiles_in_block} tiles, "
              f"{time.monotonic() - started:.0f} s")

    block_count = len(list(blocks_dir.glob("*.bin")))
    write_meta(out_dir, palette, m, args.layer, level, written, block_count)
    size_mb = block_count * BLOCK_BYTES / 1e6
    print(f"{written} tiles in {block_count} block files, {size_mb:.0f} MB, in {out_dir}")


# === simulate ===

def read_meta(out_dir):
    fields = struct.unpack(META_FORMAT, (out_dir / "meta.bin").read_bytes())
    if fields[0] != b"SWCH" or fields[1] != FORMAT_VERSION:
        sys.exit("meta.bin: wrong magic or version")
    return {"m_per_px": fields[7], "origin_e": fields[8], "origin_n": fields[9],
            "day": fields[10:266], "night": fields[266:522]}


def palette_to_rgb(values):
    """RGB565 → RGB888 table, as the panel shows it."""
    table = np.zeros((256, 3), dtype=np.uint8)
    for i, v in enumerate(values):
        r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
        table[i] = ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))
    return table


def compose_indices(out_dir, x0, y0, w, h):
    """The watch's composer: same spans, same file offsets (ChartFormat.h)."""
    view = np.full((h, w), NO_DATA_INDEX, dtype=np.uint8)
    reads = 0
    for row in range(y0 // TILE_PX, (y0 + h - 1) // TILE_PX + 1):
        top = row * TILE_PX
        y1, y2 = max(top, y0), min(top + TILE_PX, y0 + h)
        for col in range(x0 // TILE_PX, (x0 + w - 1) // TILE_PX + 1):
            left = col * TILE_PX
            x1, x2 = max(left, x0), min(left + TILE_PX, x0 + w)
            if col < 0 or row < 0:
                continue
            path = out_dir / "blocks" / f"c{col // BLOCK_TILES:04d}r{row // BLOCK_TILES:04d}.bin"
            if not path.exists():
                continue
            tile_index = (row % BLOCK_TILES) * BLOCK_TILES + (col % BLOCK_TILES)
            with open(path, "rb") as f:
                f.seek(tile_index * TILE_BYTES + (y1 - top) * TILE_PX)
                data = np.frombuffer(f.read((y2 - y1) * TILE_PX), dtype=np.uint8)
            reads += data.size
            block = data.reshape(y2 - y1, TILE_PX)
            view[y1 - y0:y2 - y0, x1 - x0:x2 - x0] = block[:, x1 - left:x2 - left]
    return view, reads


def convergence_deg(lat, lon):
    return math.degrees(math.atan(math.tan(math.radians(lon - 27.0)) * math.sin(math.radians(lat))))


def draw_symbols(image, sx, sy, args, m_per_px, color):
    """Ring, HDG line, dashed COG vector and boat, at the watch's sizes (ChartGeometry.h)."""
    factor = 4
    big = image.resize((image.width * factor, image.height * factor), Image.NEAREST)
    draw = ImageDraw.Draw(big)
    cx, cy = sx * factor, sy * factor
    conv = convergence_deg(args.lat, args.lon)

    def end(bearing_deg, length):
        angle = math.radians(bearing_deg - conv)
        return cx + length * factor * math.sin(angle), cy - length * factor * math.cos(angle)

    ring = RING_RADIUS_NM * METERS_PER_NM / m_per_px * factor
    draw.ellipse([cx - ring, cy - ring, cx + ring, cy + ring], outline=color, width=LINE_W_PX * factor)
    if args.hdg is not None:
        draw.line([cx, cy, *end(args.hdg, ring / factor)], fill=color, width=LINE_W_PX * factor)
    if args.cog is not None and args.sog >= 0.5:
        length = args.sog * METERS_PER_NM * COG_VECTOR_MIN / 60.0 / m_per_px
        for start in range(0, int(length), DASH_PX + DASH_GAP_PX):
            draw.line([*end(args.cog, start), *end(args.cog, min(start + DASH_PX, length))],
                      fill=color, width=LINE_W_PX * factor)
    dot = BOAT_R_PX * factor
    draw.ellipse([cx - dot, cy - dot, cx + dot, cy + dot], fill=color)
    return big.resize((image.width, image.height), Image.LANCZOS)


def cmd_simulate(args):
    out_dir = Path(args.out)
    meta = read_meta(out_dir)
    e, n = _to_tm35.transform(args.lon, args.lat)
    px = (e - meta["origin_e"]) / meta["m_per_px"]
    py = (meta["origin_n"] - n) / meta["m_per_px"]
    x0 = math.floor(px) - VIEW_W // 2
    y0 = math.floor(py) - VIEW_H // 2

    indices, read_bytes = compose_indices(out_dir, x0, y0, VIEW_W, VIEW_H)
    print(f"Pixel ({px:.1f}, {py:.1f}), view at ({x0}, {y0}), would read {read_bytes / 1024:.0f} KB")

    panels = []
    for night in (False, True):
        table = palette_to_rgb(meta["night"] if night else meta["day"])
        image = Image.fromarray(table[indices])
        if not args.no_symbols:
            image = draw_symbols(image, px - x0, py - y0, args, meta["m_per_px"],
                                 SYMBOL_NIGHT if night else SYMBOL_DAY)
        draw = ImageDraw.Draw(image)
        draw.rectangle([0, 0, VIEW_W - 1, TOP_BAR_H - 1], fill=(0, 0, 0))
        panels.append(image)

    gap = 20
    sheet = Image.new("RGB", (VIEW_W * 2 + gap, VIEW_H), (60, 60, 60))
    sheet.paste(panels[0], (0, 0))
    sheet.paste(panels[1], (VIEW_W + gap, 0))
    sheet.save(args.png)
    print(f"Day and night side by side: {args.png}")


# === install ===

def cmd_install(args):
    source = Path(args.out)
    volume = Path(args.volume)
    if not (source / "meta.bin").exists():
        sys.exit(f"{source} has no meta.bin. Run build first.")
    if volume.parent != Path("/Volumes") or not volume.is_dir():
        sys.exit(f"{volume} is not a mounted volume under /Volumes.")

    info = plistlib.loads(subprocess.run(["diskutil", "info", "-plist", str(volume)],
                                         capture_output=True, check=True).stdout)
    fs = info.get("FilesystemName", "?")
    if info.get("Internal", True):
        sys.exit(f"{volume} is an internal disk. Refusing.")
    if "FAT32" not in fs:
        sys.exit(f"{volume} is {fs}, the watch can only read FAT32 (exFAT is off in the Arduino core).\n"
                 f"Format the card yourself if you are sure; this erases it:\n"
                 f"  diskutil eraseDisk FAT32 CHART MBRFormat /dev/{info.get('ParentWholeDisk', 'diskN')}")
    whole = plistlib.loads(subprocess.run(["diskutil", "info", "-plist", info["ParentWholeDisk"]],
                                          capture_output=True, check=True).stdout)
    if whole.get("Content") != "FDisk_partition_scheme":
        sys.exit(f"{volume}: partition map is {whole.get('Content')}, the watch needs MBR.\n"
                 f"Format the card yourself if you are sure; this erases it:\n"
                 f"  diskutil eraseDisk FAT32 CHART MBRFormat /dev/{info['ParentWholeDisk']}")

    files = sorted((source / "blocks").glob("*.bin"))
    needed = sum(f.stat().st_size for f in files) + (source / "meta.bin").stat().st_size
    free = shutil.disk_usage(volume).free
    print(f"{len(files)} block files, {needed / 1e6:.0f} MB, {free / 1e9:.1f} GB free on {volume}")
    if needed > free:
        sys.exit("Not enough room on the card.")

    target = volume / "chart"
    (target / "blocks").mkdir(parents=True, exist_ok=True)
    pairs = [(source / "meta.bin", target / "meta.bin"), (source / "meta.json", target / "meta.json")]
    pairs += [(f, target / "blocks" / f.name) for f in files]
    for i, (src, dst) in enumerate(pairs):
        shutil.copyfile(src, dst)
        print(f"  copied {i + 1}/{len(pairs)}: {dst.name}")

    subprocess.run(["sync"], check=True)
    subprocess.run(["dot_clean", "-m", str(target)], check=False)
    print("Comparing the copy against the source...")
    bad = [dst.name for src, dst in pairs if not filecmp.cmp(src, dst, shallow=False)]
    if bad:
        sys.exit(f"{len(bad)} files differ on the card: {', '.join(bad[:5])}")
    print(f"All {len(pairs)} files match. Eject the card from Finder before removing it.")


# === Command line ===

def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)

    fetch = sub.add_parser("fetch", help="download source tiles into the cache")
    add_area_args(fetch)
    fetch.set_defaults(func=cmd_fetch)

    build = sub.add_parser("build", help="write palette and block files")
    add_area_args(build)
    build.add_argument("--m-per-px", type=float, default=DEFAULT_M_PER_PX)
    build.add_argument("--out", required=True, help="an empty directory, outside the repo")
    build.set_defaults(func=cmd_build)

    simulate = sub.add_parser("simulate", help="compose a view as a PNG, day and night")
    simulate.add_argument("--out", required=True, help="the directory given to build")
    simulate.add_argument("--lat", type=float, required=True)
    simulate.add_argument("--lon", type=float, required=True)
    simulate.add_argument("--hdg", type=float, default=195.0, help="true heading, degrees")
    simulate.add_argument("--cog", type=float, default=180.0, help="true COG, degrees")
    simulate.add_argument("--sog", type=float, default=5.0, help="SOG, knots")
    simulate.add_argument("--no-symbols", action="store_true")
    simulate.add_argument("--png", required=True)
    simulate.set_defaults(func=cmd_simulate)

    install = sub.add_parser("install", help="copy onto a FAT32 card and compare")
    install.add_argument("--out", required=True, help="the directory given to build")
    install.add_argument("--volume", required=True, help="the card, for example /Volumes/CHART")
    install.set_defaults(func=cmd_install)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
