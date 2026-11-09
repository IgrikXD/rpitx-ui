/**
 * @file dma_drain.h
 * @brief Clean end of a librpitx DMA transmission: play the queued samples, then stop the engine.
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
 * the ring, not when the engine has played them, and the ring holds up to
 * three quarters of its size ahead of the engine. Once the engine runs, a
 * slot can only be rewritten after it has been played, so after a whole ring
 * of idle samples every earlier sample has been transmitted. The tail goes
 * in two halves: the engine starts once a quarter of the ring is queued, and
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
 * @brief Transmit every queued IQ sample, then stop the DMA engine.
 * @param dma        iqdmasync engine, started or not.
 * @param fifoSize   Ring size the engine was constructed with.
 * @param idleSample Sample the tail is filled with ({0, 0} switches the carrier off).
 * @param harmonic   Harmonic passed to SetIQSamples().
 */
void drainAndStop(iqdmasync& dma, uint32_t fifoSize, std::complex<float> idleSample, int harmonic);

/**
 * @brief Transmit every queued frequency sample, then stop the DMA engine.
 * @param dma        ngfmdmasync engine, started or not.
 * @param fifoSize   Ring size the engine was constructed with.
 * @param idleSample Frequency deviation of the tail in Hz (0 keeps the unmodulated carrier).
 */
void drainAndStop(ngfmdmasync& dma, uint32_t fifoSize, float idleSample);

/**
 * @brief Transmit every queued envelope sample, then stop the DMA engine.
 * @param dma        amdmasync engine, started or not.
 * @param fifoSize   Ring size the engine was constructed with.
 * @param idleSample Envelope of the tail (the unmodulated carrier level for AM).
 */
void drainAndStop(amdmasync& dma, uint32_t fifoSize, float idleSample);
