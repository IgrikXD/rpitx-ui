/**
 * @file iq_sample.h
 * @brief In-phase / quadrature sample pair used throughout the DSP pipeline.
 */

#pragma once

/**
 * @brief In-phase / quadrature sample pair.
 */
struct IqSample {
    float i{};  ///< In-phase component.
    float q{};  ///< Quadrature component.
};
