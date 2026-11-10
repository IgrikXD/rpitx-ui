/**
 * @file dma_transmission.cpp
 * @brief Start and stop of a librpitx DMA transmission for the three sample-setter APIs.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 10.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#include "dma_transmission.h"

void startTransmission(iqdmasync& dma, std::complex<float> idleSample, int harmonic) {
    dma.disableclk(CARRIER_GPIO);
    if (dma.ModeIQ == MODE_IQ) {
        dma.SetIQSample(0, idleSample, harmonic);
    } else {
        dma.SetFreqAmplitudeSample(0, idleSample, harmonic);
    }
    std::vector<std::complex<float>> lead(PCM_FIFO_DEPTH, idleSample);
    dma.SetIQSamples(lead.data(), lead.size(), harmonic);
}

void startTransmission(ngfmdmasync& dma, float idleSample) {
    dma.SetFrequencySample(0, idleSample);
    std::vector<float> lead(PCM_FIFO_DEPTH, idleSample);
    dma.SetFrequencySamples(lead.data(), lead.size());
}

void startTransmission(amdmasync& dma, float idleSample) {
    dma.disableclk(CARRIER_GPIO);
    dma.SetAmSample(0, idleSample);
    std::vector<float> lead(PCM_FIFO_DEPTH, idleSample);
    dma.SetAmSamples(lead.data(), lead.size());
}

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
