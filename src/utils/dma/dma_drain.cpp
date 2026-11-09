/**
 * @file dma_drain.cpp
 * @brief Clean end of a librpitx DMA transmission for the three sample-setter APIs.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 10.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "dma_drain.h"

void drainAndStop(iqdmasync& dma, uint32_t fifoSize, std::complex<float> idleSample, int harmonic) {
    queueDrainTail(fifoSize, idleSample, [&dma, harmonic](std::vector<std::complex<float>>& tail) {
        dma.SetIQSamples(tail.data(), tail.size(), harmonic);
    });
    dma.stop();
}

void drainAndStop(ngfmdmasync& dma, uint32_t fifoSize, float idleSample) {
    queueDrainTail(
        fifoSize, idleSample, [&dma](std::vector<float>& tail) { dma.SetFrequencySamples(tail.data(), tail.size()); });
    dma.stop();
}

void drainAndStop(amdmasync& dma, uint32_t fifoSize, float idleSample) {
    queueDrainTail(
        fifoSize, idleSample, [&dma](std::vector<float>& tail) { dma.SetAmSamples(tail.data(), tail.size()); });
    dma.stop();
}
