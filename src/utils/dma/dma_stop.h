/**
 * @file dma_stop.h
 * @brief Stop of a librpitx DMA transmission that plays the queued samples when the input has ended.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 10.11.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

#include <librpitx/librpitx.h>

#include <complex>
#include <cstdint>
#include <vector>

/**
 * @brief Queue one DMA ring of idle samples, in two halves, through push.
 *
 * The librpitx Set*Samples() methods return as soon as their samples are in
 * the ring, not when the engine has played them; they wait until a quarter of
 * the ring is left to play and then write the next block, so a quarter ring
 * plus one block stays queued ahead of the engine. Once the engine runs, a
 * slot can only be rewritten after it has been played, so after a whole ring
 * of idle samples every earlier sample has been transmitted. The tail goes in
 * two halves: the engine starts once a quarter of the ring is queued, and
 * before that the whole ring counts as free, so a single write of a whole
 * ring would wrap onto an input shorter than that quarter.
 *
 * @param fifoSize   Number of samples in the DMA ring.
 * @param idleSample Sample that transmits nothing of interest (carrier off or unmodulated).
 * @param push       Callable taking std::vector<Sample>& that blocks like Set*Samples() does.
 */
template <typename Sample, typename Push>
void queueDrainTail(uint32_t fifoSize, const Sample& idleSample, Push&& push) {
    std::vector<Sample> tail(fifoSize / 2, idleSample);
    push(tail);
    push(tail);
}

/**
 * @brief Stop the DMA engine of an IQ transmission.
 *
 * When the input ended by itself, the samples still queued in the ring are
 * transmitted first; on a stop request they are discarded so the stop is immediate.
 *
 * @param dma        iqdmasync engine, started or not.
 * @param idleSample Sample the drain tail is filled with ({0, 0} switches the carrier off).
 * @param harmonic   Harmonic passed to SetIQSamples().
 * @param inputEnded True when the input ended by itself, false on a stop request.
 */
void stopTransmission(iqdmasync& dma, std::complex<float> idleSample, int harmonic, bool inputEnded);

/**
 * @brief Stop the DMA engine of an FM transmission.
 *
 * When the input ended by itself, the samples still queued in the ring are
 * transmitted first; on a stop request they are discarded so the stop is immediate.
 *
 * @param dma        ngfmdmasync engine, started or not.
 * @param idleSample Frequency deviation of the drain tail in Hz (0 keeps the unmodulated carrier).
 * @param inputEnded True when the input ended by itself, false on a stop request.
 */
void stopTransmission(ngfmdmasync& dma, float idleSample, bool inputEnded);

/**
 * @brief Stop the DMA engine of an AM transmission.
 *
 * When the input ended by itself, the samples still queued in the ring are
 * transmitted first; on a stop request they are discarded so the stop is immediate.
 *
 * @param dma        amdmasync engine, started or not.
 * @param idleSample Envelope of the drain tail (the unmodulated carrier level for AM).
 * @param inputEnded True when the input ended by itself, false on a stop request.
 */
void stopTransmission(amdmasync& dma, float idleSample, bool inputEnded);
