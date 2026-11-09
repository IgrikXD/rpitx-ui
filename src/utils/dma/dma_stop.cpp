/**
 * @file dma_stop.cpp
 * @brief Stop of a librpitx DMA transmission for the three sample-setter APIs.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 10.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "dma_stop.h"

void stopTransmission(iqdmasync& dma, std::complex<float> idleSample, int harmonic, bool inputEnded) {
    if (inputEnded) {
        queueDrainTail(dma.buffersize, idleSample, [&dma, harmonic](std::vector<std::complex<float>>& tail) {
            dma.SetIQSamples(tail.data(), tail.size(), harmonic);
        });
    }
    dma.stop();
}

void stopTransmission(ngfmdmasync& dma, float idleSample, bool inputEnded) {
    if (inputEnded) {
        queueDrainTail(dma.buffersize, idleSample, [&dma](std::vector<float>& tail) {
            dma.SetFrequencySamples(tail.data(), tail.size());
        });
    }
    dma.stop();
}

void stopTransmission(amdmasync& dma, float idleSample, bool inputEnded) {
    if (inputEnded) {
        queueDrainTail(dma.buffersize, idleSample, [&dma](std::vector<float>& tail) {
            dma.SetAmSamples(tail.data(), tail.size());
        });
    }
    dma.stop();
}
