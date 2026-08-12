#include "pages/PageBase.h"

#include <base/base.h>

#include "managers/LightWaveOverlay.h"

Mutex PageBase::mSpiMutex;

PageBase::~PageBase() {
}

void PageBase::sendLedData(const std::vector<uint8_t>& rgbData) {
    std::vector<uint8_t> outputData = rgbData;
    LightWaveOverlay::getInstance().updateBaseFrame(rgbData);
    LightWaveOverlay::getInstance().apply(outputData);
    sendRawLedData(outputData);
}

void PageBase::sendRawLedData(const std::vector<uint8_t>& rgbData) {
    Mutex::Autolock l_(mSpiMutex);
    static SpiHelper spi(0, SPI_MODE_0, 10 * 1000 * 1000, 8, false);
    uint8_t* copyData = new uint8_t[64 * 16 * 3]();

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 52 * 3; x++) {
            copyData[y * 64 * 3 + x] = rgbData[y * 52 * 3 + x];
        }
    }

    const int maxSz = 64 * 16 * 3;
    GpioHelper::output("GPIO_35", 0);
    usleep(1 * 1000);
    if (!spi.write(copyData, maxSz)) {
        LOGE_TRACE("spi write error");
    }
    GpioHelper::output("GPIO_35", 1);
    delete[] copyData;

    usleep(15 * 1000);
}
