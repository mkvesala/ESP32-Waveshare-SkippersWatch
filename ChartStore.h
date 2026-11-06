#pragma once

#include <Arduino.h>
#include <FS.h>
#include "ChartFormat.h"

// === C L A S S  C H A R T S T O R E ===
//
// - The chart files on the microSD card, over SD_MMC in 1-bit mode. Measured on the watch: a
//   view reads in 152 ms at 40 MHz, about twice as fast as SPI. 20 MHz is the fallback when
//   a mount at 40 MHz fails.
// - Reads straight into PSRAM. An internal DMA buffer would save only about 11 ms per view.
// - unmount() leaves the card idle. It cannot be powered off: the card shares DCDC1 with the
//   ESP32. Its pins become plain inputs, because driving one against its 10k pull-up would
//   cost about 0.33 mA, and the external pull-up keeps CS (D3) high.
// - Never formats the card.
// - Not thread safe: used only from ChartEngine's task.
// - Owned by: ChartEngine

class ChartStore {

public:

    enum class ReadResult : uint8_t { OK, NO_BLOCK, FAILED };

    bool mount();
    void unmount();
    bool isMounted() const { return _mounted; }

    bool readMeta(ChartFormat::Meta& out);

    // The span's whole tile rows into out, s.h * TILE_PX bytes. NO_BLOCK: the chart has no
    // data there, which is normal at its edges. FAILED: the card did not deliver.
    ReadResult readSpan(const ChartFormat::Span& s, uint8_t* out);

private:

    static constexpr int8_t  PIN_SD_CMD = 1;
    static constexpr int8_t  PIN_SD_CLK = 2;
    static constexpr int8_t  PIN_SD_D0  = 3;
    static constexpr int8_t  PIN_SD_D3  = 17;   // CS in SPI mode. Only pulled up in 1-bit SD mode.
    static constexpr uint8_t MAX_OPEN_FILES = 2;

    bool _mounted = false;
    File _block;
    char _block_path[ChartFormat::BLOCK_PATH_LEN] = "";
    bool _block_missing = false;

    void closeBlock();

};
