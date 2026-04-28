/**
 * @file iq_sample.h
 * @brief In-phase / quadrature sample pair used throughout the DSP pipeline.
 *
 * @author Ihar Yatsevich <igor.nikolaevich.96@gmail.com>
 * @date 27.03.2026
 * @copyright GPL-3.0
 * @see https://github.com/IgrikXD/rpitx-ui
 * @note RF transmitter for Raspberry Pi with improved UI functionality, built with CMake.
 */

#pragma once

/**
 * @brief In-phase / quadrature sample pair.
 */
struct IqSample {
    float i{};  ///< In-phase component.
    float q{};  ///< Quadrature component.
};
