#include "ChartStore.h"
#include <SD_MMC.h>

// === P U B L I C ===

bool ChartStore::mount() {
    if (_mounted) return true;
    constexpr bool ONE_BIT = true;
    constexpr bool FORMAT_IF_FAILED = false;

    if (!SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0)) return false;
    _mounted = SD_MMC.begin("/sdcard", ONE_BIT, FORMAT_IF_FAILED, SDMMC_FREQ_HIGHSPEED, MAX_OPEN_FILES);
    if (!_mounted) {
        SD_MMC.end();
        if (SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0)) {
            _mounted = SD_MMC.begin("/sdcard", ONE_BIT, FORMAT_IF_FAILED, SDMMC_FREQ_DEFAULT, MAX_OPEN_FILES);
            if (_mounted) Serial.println("[chart] card mounted at 20 MHz");
        }
    }
    if (!_mounted) this->unmount();
    return _mounted;
}

void ChartStore::unmount() {
    this->closeBlock();
    SD_MMC.end();
    _mounted = false;
    pinMode(PIN_SD_CMD, INPUT);
    pinMode(PIN_SD_CLK, INPUT);
    pinMode(PIN_SD_D0, INPUT);
    pinMode(PIN_SD_D3, INPUT);
}

bool ChartStore::readMeta(ChartFormat::Meta& out) {
    if (!_mounted) return false;
    File f = SD_MMC.open(ChartFormat::META_PATH, FILE_READ);
    if (!f) return false;
    const bool ok = f.read(reinterpret_cast<uint8_t*>(&out), sizeof(out)) == sizeof(out);
    f.close();
    return ok && ChartFormat::isValid(out);
}

ChartStore::ReadResult ChartStore::readSpan(const ChartFormat::Span& s, uint8_t* out) {
    if (!_mounted) return ReadResult::FAILED;

    char path[ChartFormat::BLOCK_PATH_LEN];
    if (!ChartFormat::blockPath(s.col, s.row, path, sizeof(path))) return ReadResult::NO_BLOCK;

    // A view usually stays in one block, so its file stays open between spans. A missing block
    // is remembered too: open() on a missing file logs an error each time.
    if (strcmp(path, _block_path) != 0) {
        this->closeBlock();
        strlcpy(_block_path, path, sizeof(_block_path));
        _block_missing = !SD_MMC.exists(path);
        if (!_block_missing) _block = SD_MMC.open(path, FILE_READ);
    }
    if (_block_missing) return ReadResult::NO_BLOCK;
    if (!_block) return ReadResult::FAILED;

    if (!_block.seek(ChartFormat::blockOffset(s.col, s.row, s.tile_y), SeekSet)) return ReadResult::FAILED;
    const size_t want = (size_t)s.h * ChartFormat::TILE_PX;
    return _block.read(out, want) == want ? ReadResult::OK : ReadResult::FAILED;
}

// === P R I V A T E ===

void ChartStore::closeBlock() {
    if (_block) _block.close();
    _block_path[0] = '\0';
    _block_missing = false;
}
