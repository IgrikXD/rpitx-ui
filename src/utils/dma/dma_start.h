/**
 * @file dma_start.h
 * @brief Start of a librpitx DMA transmission whose first sample is the first one on the air.
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
#include <cstddef>

/**
 * @brief Depth of the PCM FIFO that paces the DMA engine, in samples.
 *
 * librpitx requests DMA transfers while the FIFO is not full, so the
 * samples that fill it after the start are played back to back.
 */
inline constexpr std::size_t PCM_FIFO_DEPTH{64};

/**
 * @brief GPIO that carries the carrier (GPCLK0), the one librpitx enables.
 */
inline constexpr int CARRIER_GPIO{4};

/**
 * @brief Prepare an IQ transmission so that its first sample is the first one on the air.
 *
 * Slot 0 of the DMA ring is executed first but never written by librpitx
 * before the start, and the engine plays the PCM_FIFO_DEPTH samples that fill
 * its empty pacing FIFO back to back: both get the idle sample, so the first
 * sample of the transmission is the first one held for a whole sample period.
 * The engine rewrites the function of the carrier GPIO with every sample, so
 * the carrier that the constructor switched on is switched off here and
 * returns with the first carrier-on sample.
 *
 * @param dma        iqdmasync engine before its first sample.
 * @param idleSample Sample of the lead ({0, 0} keeps the carrier off in both modes).
 * @param harmonic   Harmonic passed to SetIQSamples().
 */
void startTransmission(iqdmasync& dma, std::complex<float> idleSample, int harmonic);

/**
 * @brief Prepare an FM transmission so that its first sample is the first one on the air.
 *
 * Slot 0 and the start-up burst are handled as for iqdmasync. The engine writes
 * only the PLL fraction, so the carrier stays on from the constructor: a zero
 * deviation keeps it unmodulated, which is silence in FM.
 *
 * @param dma        ngfmdmasync engine before its first sample.
 * @param idleSample Frequency deviation of the lead in Hz.
 */
void startTransmission(ngfmdmasync& dma, float idleSample);

/**
 * @brief Prepare an AM transmission so that its first sample is the first one on the air.
 *
 * Slot 0, the start-up burst and the carrier are handled as for iqdmasync.
 *
 * @param dma        amdmasync engine before its first sample.
 * @param idleSample Envelope of the lead (the unmodulated carrier level for AM).
 */
void startTransmission(amdmasync& dma, float idleSample);
