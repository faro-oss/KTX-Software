// SPDX-License-Identifier: Apache-2.0
// ----------------------------------------------------------------------------
// Copyright 2011-2021 Arm Limited
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License. You may obtain a copy
// of the License at:
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations
// under the License.
// ----------------------------------------------------------------------------

/**
 * @brief Functions for the library entrypoint.
 */

#include <array>
#include <cstring>
#include <new>

#include "astcenc.h"
#include "astcenc_internal.h"
#include "astcenc_diagnostic_trace.h"

/**
 * @brief Record of the quality tuning parameter values.
 *
 * See the @c astcenc_config structure for detailed parameter documentation.
 *
 * Note that the mse_overshoot entries are scaling factors relative to the base MSE to hit db_limit.
 * A 20% overshoot is harder to hit for a higher base db_limit, so we may actually use lower ratios
 * for the more through search presets because the underlying db_limit is so much higher.
 */
struct astcenc_preset_config {
	float quality;
	unsigned int tune_partition_count_limit;
	unsigned int tune_partition_index_limit;
	unsigned int tune_block_mode_limit;
	unsigned int tune_refinement_limit;
	unsigned int tune_candidate_limit;
	float tune_db_limit_a_base;
	float tune_db_limit_b_base;
	float tune_mode0_mse_overshoot;
	float tune_refinement_mse_overshoot;
	float tune_2_partition_early_out_limit_factor;
	float tune_3_partition_early_out_limit_factor;
	float tune_2_plane_early_out_limit_factor;
	float tune_2_plane_early_out_limit_correlation;
};


/**
 * @brief The static quality presets that are built-in for high bandwidth
 * presets (x < 25 texels per block).
 */
static const std::array<astcenc_preset_config, 5> preset_configs_high {{
	{
		ASTCENC_PRE_FASTEST,
		3, 2, 30, 1, 1, 79.0f, 57.0f, 2.0f, 2.0f, 1.0f, 1.0f, 1.05f, 0.5f
	}, {
		ASTCENC_PRE_FAST,
		3, 12, 55, 3, 2, 85.2f, 63.2f, 3.5f, 3.5f, 1.0f, 1.1f, 1.05f, 0.5f
	}, {
		ASTCENC_PRE_MEDIUM,
		4, 26, 76, 2, 2, 95.0f, 70.0f, 2.5f, 2.5f, 1.2f, 1.25f, 1.05f, 0.75f
	}, {
		ASTCENC_PRE_THOROUGH,
		4, 76, 93, 4, 4, 105.0f, 77.0f, 10.0f, 10.0f, 2.5f, 1.25f, 1.05f, 0.95f
	}, {
		ASTCENC_PRE_EXHAUSTIVE,
		4, 1024, 100, 4, 4, 200.0f, 200.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 0.99f
	}
}};


/**
 * @brief The static quality presets that are built-in for medium bandwidth
 * presets (25 <= x < 64 texels per block).
 */
static const std::array<astcenc_preset_config, 5> preset_configs_mid {{
	{
		ASTCENC_PRE_FASTEST,
		3, 2, 30, 1, 1, 79.0f, 57.0f, 2.0f, 2.0f, 1.0f, 1.0f, 1.05f, 0.5f
	}, {
		ASTCENC_PRE_FAST,
		3, 12, 55, 3, 2, 85.2f, 63.2f, 3.5f, 3.5f, 1.0f, 1.1f, 1.05f, 0.5f
	}, {
		ASTCENC_PRE_MEDIUM,
		4, 26, 76, 2, 2, 95.0f, 70.0f, 3.0f, 3.0f, 1.2f, 1.25f, 1.05f, 0.75f
	}, {
		ASTCENC_PRE_THOROUGH,
		4, 76, 93, 4, 4, 105.0f, 77.0f, 10.0f, 10.0f, 2.5f, 1.25f, 1.05f, 0.95f
	}, {
		ASTCENC_PRE_EXHAUSTIVE,
		4, 1024, 100, 4, 4, 200.0f, 200.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 0.99f
	}
}};


/**
 * @brief The static quality presets that are built-in for low bandwidth
 * presets (64 <= x texels per block).
 */
static const std::array<astcenc_preset_config, 5> preset_configs_low {{
	{
		ASTCENC_PRE_FASTEST,
		3, 2, 30, 1, 1, 79.0f, 57.0f, 2.0f, 2.0f, 1.0f, 1.0f, 1.05f, 0.5f
	}, {
		ASTCENC_PRE_FAST,
		3, 10, 53, 3, 2, 85.0f, 63.0f, 3.5f, 3.5f, 1.0f, 1.1f, 1.05f, 0.5f
	}, {
		ASTCENC_PRE_MEDIUM,
		3, 26, 76, 3, 2, 95.0f, 70.0f, 3.5f, 3.5f, 1.2f, 1.25f, 1.05f, 0.75f
	}, {
		ASTCENC_PRE_THOROUGH,
		4, 75, 92, 4, 4, 105.0f, 77.0f, 10.0f, 10.0f, 2.5f, 1.25f, 1.05f, 0.95f
	}, {
		ASTCENC_PRE_EXHAUSTIVE,
		4, 1024, 100, 4, 4, 200.0f, 200.0f, 10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 0.99f
	}
}};

/**
 * @brief Validate CPU floating point meets assumptions made in the codec.
 *
 * The codec is written with the assumption that a float threaded through the @c if32 union will be
 * stored and reloaded as a 32-bit IEEE-754 float with round-to-nearest rounding. This is always the
 * case in an IEEE-754 compliant system, however not every system or compilation mode is actually
 * IEEE-754 compliant. This normally fails if the code is compiled with fast math enabled.
 *
 * @return Return @c ASTCENC_SUCCESS if validated, otherwise an error on failure.
 */
static astcenc_error validate_cpu_float()
{
	if32 p;
	volatile float xprec_testval = 2.51f;
	p.f = xprec_testval + 12582912.0f;
	float q = p.f - 12582912.0f;

	if (q != 3.0f)
	{
		return ASTCENC_ERR_BAD_CPU_FLOAT;
	}

	return ASTCENC_SUCCESS;
}

/**
 * @brief Validate CPU ISA support meets the requirements of this build of the library.
 *
 * Each library build is statically compiled for a particular set of CPU ISA features, such as the
 * SIMD support or other ISA extensions such as POPCNT. This function checks that the host CPU
 * actually supports everything this build needs.
 *
 * @return Return @c ASTCENC_SUCCESS if validated, otherwise an error on failure.
 */
static astcenc_error validate_cpu_isa()
{
	#if ASTCENC_SSE >= 41
		if (!cpu_supports_sse41())
		{
			return ASTCENC_ERR_BAD_CPU_ISA;
		}
	#endif

	#if ASTCENC_POPCNT >= 1
		if (!cpu_supports_popcnt())
		{
			return ASTCENC_ERR_BAD_CPU_ISA;
		}
	#endif

	#if ASTCENC_F16C >= 1
		if (!cpu_supports_f16c())
		{
			return ASTCENC_ERR_BAD_CPU_ISA;
		}
	#endif

	#if ASTCENC_AVX >= 2
		if (!cpu_supports_avx2())
		{
			return ASTCENC_ERR_BAD_CPU_ISA;
		}
	#endif

	return ASTCENC_SUCCESS;
}

/**
 * @brief Validate config profile.
 *
 * @param profile   The profile to check.
 *
 * @return Return @c ASTCENC_SUCCESS if validated, otherwise an error on failure.
 */
static astcenc_error validate_profile(
	astcenc_profile profile
) {
	// Values in this enum are from an external user, so not guaranteed to be
	// bounded to the enum values
	switch(static_cast<int>(profile))
	{
	case ASTCENC_PRF_LDR_SRGB:
	case ASTCENC_PRF_LDR:
	case ASTCENC_PRF_HDR_RGB_LDR_A:
	case ASTCENC_PRF_HDR:
		return ASTCENC_SUCCESS;
	default:
		return ASTCENC_ERR_BAD_PROFILE;
	}
}

/**
 * @brief Validate block size.
 *
 * @param block_x   The block x dimensions.
 * @param block_y   The block y dimensions.
 * @param block_z   The block z dimensions.
 *
 * @return Return @c ASTCENC_SUCCESS if validated, otherwise an error on failure.
 */
static astcenc_error validate_block_size(
	unsigned int block_x,
	unsigned int block_y,
	unsigned int block_z
) {
	if (((block_z <= 1) && is_legal_2d_block_size(block_x, block_y)) ||
	    ((block_z >= 2) && is_legal_3d_block_size(block_x, block_y, block_z)))
	{
		return ASTCENC_SUCCESS;
	}

	return ASTCENC_ERR_BAD_BLOCK_SIZE;
}

/**
 * @brief Validate flags.
 *
 * @param flags   The flags to check.
 *
 * @return Return @c ASTCENC_SUCCESS if validated, otherwise an error on failure.
 */
static astcenc_error validate_flags(
	unsigned int flags
) {
	// Flags field must not contain any unknown flag bits
	unsigned int exMask = ~ASTCENC_ALL_FLAGS;
	if (astc::popcount(flags & exMask) != 0)
	{
		return ASTCENC_ERR_BAD_FLAGS;
	}

	// Flags field must only contain at most a single map type
	exMask = ASTCENC_FLG_MAP_MASK
	       | ASTCENC_FLG_MAP_NORMAL
	       | ASTCENC_FLG_MAP_RGBM;
	if (astc::popcount(flags & exMask) > 1)
	{
		return ASTCENC_ERR_BAD_FLAGS;
	}

	return ASTCENC_SUCCESS;
}

#if !defined(ASTCENC_DECOMPRESS_ONLY)

/**
 * @brief Validate single channel compression swizzle.
 *
 * @param swizzle   The swizzle to check.
 *
 * @return Return @c ASTCENC_SUCCESS if validated, otherwise an error on failure.
 */
static astcenc_error validate_compression_swz(
	astcenc_swz swizzle
) {
	// Not all enum values are handled; SWZ_Z is invalid for compression
	switch(static_cast<int>(swizzle))
	{
	case ASTCENC_SWZ_R:
	case ASTCENC_SWZ_G:
	case ASTCENC_SWZ_B:
	case ASTCENC_SWZ_A:
	case ASTCENC_SWZ_0:
	case ASTCENC_SWZ_1:
		return ASTCENC_SUCCESS;
	default:
		return ASTCENC_ERR_BAD_SWIZZLE;
	}
}

/**
 * @brief Validate overall compression swizzle.
 *
 * @param swizzle   The swizzle to check.
 *
 * @return Return @c ASTCENC_SUCCESS if validated, otherwise an error on failure.
 */
static astcenc_error validate_compression_swizzle(
	const astcenc_swizzle& swizzle
) {
	if (validate_compression_swz(swizzle.r) ||
	    validate_compression_swz(swizzle.g) ||
	    validate_compression_swz(swizzle.b) ||
	    validate_compression_swz(swizzle.a))
	{
		return ASTCENC_ERR_BAD_SWIZZLE;
	}

	return ASTCENC_SUCCESS;
}
#endif

/**
 * @brief Validate single channel decompression swizzle.
 *
 * @param swizzle   The swizzle to check.
 *
 * @return Return @c ASTCENC_SUCCESS if validated, otherwise an error on failure.
 */
static astcenc_error validate_decompression_swz(
	astcenc_swz swizzle
) {
	// Values in this enum are from an external user, so not guaranteed to be
	// bounded to the enum values
	switch(static_cast<int>(swizzle))
	{
	case ASTCENC_SWZ_R:
	case ASTCENC_SWZ_G:
	case ASTCENC_SWZ_B:
	case ASTCENC_SWZ_A:
	case ASTCENC_SWZ_0:
	case ASTCENC_SWZ_1:
	case ASTCENC_SWZ_Z:
		return ASTCENC_SUCCESS;
	default:
		return ASTCENC_ERR_BAD_SWIZZLE;
	}
}

/**
 * @brief Validate overall decompression swizzle.
 *
 * @param swizzle   The swizzle to check.
 *
 * @return Return @c ASTCENC_SUCCESS if validated, otherwise an error on failure.
 */
static astcenc_error validate_decompression_swizzle(
	const astcenc_swizzle& swizzle
) {
	if (validate_decompression_swz(swizzle.r) ||
	    validate_decompression_swz(swizzle.g) ||
	    validate_decompression_swz(swizzle.b) ||
	    validate_decompression_swz(swizzle.a))
	{
		return ASTCENC_ERR_BAD_SWIZZLE;
	}

	return ASTCENC_SUCCESS;
}

/**
 * Validate that an incoming configuration is in-spec.
 *
 * This function can respond in two ways:
 *
 *   * Numerical inputs that have valid ranges are clamped to those valid ranges. No error is thrown
 *     for out-of-range inputs in this case.
 *   * Numerical inputs and logic inputs are are logically invalid and which make no sense
 *     algorithmically will return an error.
 *
 * @param[in,out] config   The input compressor configuration.
 *
 * @return Return @c ASTCENC_SUCCESS if validated, otherwise an error on failure.
 */
static astcenc_error validate_config(
	astcenc_config &config
) {
	astcenc_error status;

	status = validate_profile(config.profile);
	if (status != ASTCENC_SUCCESS)
	{
		return status;
	}

	status = validate_flags(config.flags);
	if (status != ASTCENC_SUCCESS)
	{
		return status;
	}

	status = validate_block_size(config.block_x, config.block_y, config.block_z);
	if (status != ASTCENC_SUCCESS)
	{
		return status;
	}

#if defined(ASTCENC_DECOMPRESS_ONLY)
	// Decompress-only builds only support decompress-only contexts
	if (!(config.flags & ASTCENC_FLG_DECOMPRESS_ONLY))
	{
		return ASTCENC_ERR_BAD_PARAM;
	}
#endif

	config.v_rgba_mean_stdev_mix = astc::max(config.v_rgba_mean_stdev_mix, 0.0f);
	config.v_rgb_power = astc::max(config.v_rgb_power, 0.0f);
	config.v_rgb_base = astc::max(config.v_rgb_base, 0.0f);
	config.v_rgb_mean = astc::max(config.v_rgb_mean, 0.0f);
	config.v_rgb_stdev = astc::max(config.v_rgb_stdev, 0.0f);
	config.v_a_power = astc::max(config.v_a_power, 0.0f);
	config.v_a_base = astc::max(config.v_a_base, 0.0f);
	config.v_a_mean = astc::max(config.v_a_mean, 0.0f);
	config.v_a_stdev = astc::max(config.v_a_stdev, 0.0f);

	config.b_deblock_weight = astc::max(config.b_deblock_weight, 0.0f);

	config.rgbm_m_scale = astc::max(config.rgbm_m_scale, 1.0f);

	config.tune_partition_count_limit = astc::clamp(config.tune_partition_count_limit, 1u, 4u);
	config.tune_partition_index_limit = astc::clamp(config.tune_partition_index_limit, 1u, (unsigned int)BLOCK_MAX_PARTITIONINGS);
	config.tune_block_mode_limit = astc::clamp(config.tune_block_mode_limit, 1u, 100u);
	config.tune_refinement_limit = astc::max(config.tune_refinement_limit, 1u);
	config.tune_candidate_limit = astc::clamp(config.tune_candidate_limit, 1u, TUNE_MAX_TRIAL_CANDIDATES);
	config.tune_db_limit = astc::max(config.tune_db_limit, 0.0f);
	config.tune_mode0_mse_overshoot = astc::max(config.tune_mode0_mse_overshoot, 1.0f);
	config.tune_refinement_mse_overshoot = astc::max(config.tune_refinement_mse_overshoot, 1.0f);
	config.tune_2_partition_early_out_limit_factor = astc::max(config.tune_2_partition_early_out_limit_factor, 0.0f);
	config.tune_3_partition_early_out_limit_factor = astc::max(config.tune_3_partition_early_out_limit_factor, 0.0f);
	config.tune_2_plane_early_out_limit_factor = astc::max(config.tune_2_plane_early_out_limit_factor, 0.0f);
	config.tune_2_plane_early_out_limit_correlation = astc::max(config.tune_2_plane_early_out_limit_correlation, 0.0f);

	// Specifying a zero weight color component is not allowed; force to small value
	float max_weight = astc::max(astc::max(config.cw_r_weight, config.cw_g_weight),
	                             astc::max(config.cw_b_weight, config.cw_a_weight));
	if (max_weight > 0.0f)
	{
		max_weight /= 1000.0f;
		config.cw_r_weight = astc::max(config.cw_r_weight, max_weight);
		config.cw_g_weight = astc::max(config.cw_g_weight, max_weight);
		config.cw_b_weight = astc::max(config.cw_b_weight, max_weight);
		config.cw_a_weight = astc::max(config.cw_a_weight, max_weight);
	}
	// If all color components error weights are zero then return an error
	else
	{
		return ASTCENC_ERR_BAD_PARAM;
	}

	return ASTCENC_SUCCESS;
}

/* See header for documentation. */
astcenc_error astcenc_config_init(
	astcenc_profile profile,
	unsigned int block_x,
	unsigned int block_y,
	unsigned int block_z,
	float quality,
	unsigned int flags,
	astcenc_config* configp
) {
	astcenc_error status;
	astcenc_config& config = *configp;

	// Zero init all config fields; although most of will be over written
	std::memset(&config, 0, sizeof(config));

	// Process the block size
	block_z = astc::max(block_z, 1u); // For 2D blocks Z==0 is accepted, but convert to 1
	status = validate_block_size(block_x, block_y, block_z);
	if (status != ASTCENC_SUCCESS)
	{
		return status;
	}

	config.block_x = block_x;
	config.block_y = block_y;
	config.block_z = block_z;

	float texels = static_cast<float>(block_x * block_y * block_z);
	float ltexels = logf(texels) / logf(10.0f);

	// Process the performance quality level or preset; note that this must be done before we
	// process any additional settings, such as color profile and flags, which may replace some of
	// these settings with more use case tuned values
	if (quality < ASTCENC_PRE_FASTEST ||
	    quality > ASTCENC_PRE_EXHAUSTIVE)
	{
		return ASTCENC_ERR_BAD_QUALITY;
	}

	static const std::array<astcenc_preset_config, 5>* preset_configs;
	int texels_int = block_x * block_y * block_z;
	if (texels_int < 25)
	{
		preset_configs = &preset_configs_high;
	}
	else if (texels_int < 64)
	{
		preset_configs = &preset_configs_mid;
	}
	else
	{
		preset_configs = &preset_configs_low;
	}

	// Determine which preset to use, or which pair to interpolate
	size_t start;
	size_t end;
	for (end = 0; end < preset_configs->size(); end++)
	{
		if ((*preset_configs)[end].quality >= quality)
		{
			break;
		}
	}

	start = end == 0 ? 0 : end - 1;

	// Start and end node are the same - so just transfer the values.
	if (start == end)
	{
		config.tune_partition_count_limit = (*preset_configs)[start].tune_partition_count_limit;
		config.tune_partition_index_limit = (*preset_configs)[start].tune_partition_index_limit;
		config.tune_block_mode_limit = (*preset_configs)[start].tune_block_mode_limit;
		config.tune_refinement_limit = (*preset_configs)[start].tune_refinement_limit;
		config.tune_candidate_limit = astc::min((*preset_configs)[start].tune_candidate_limit,
		                                        TUNE_MAX_TRIAL_CANDIDATES);
		config.tune_db_limit = astc::max((*preset_configs)[start].tune_db_limit_a_base - 35 * ltexels,
		                                 (*preset_configs)[start].tune_db_limit_b_base - 19 * ltexels);

		config.tune_mode0_mse_overshoot = (*preset_configs)[start].tune_mode0_mse_overshoot;
		config.tune_refinement_mse_overshoot = (*preset_configs)[start].tune_refinement_mse_overshoot;

		config.tune_2_partition_early_out_limit_factor = (*preset_configs)[start].tune_2_partition_early_out_limit_factor;
		config.tune_3_partition_early_out_limit_factor =(*preset_configs)[start].tune_3_partition_early_out_limit_factor;
		config.tune_2_plane_early_out_limit_factor = (*preset_configs)[start].tune_2_plane_early_out_limit_factor;
		config.tune_2_plane_early_out_limit_correlation = (*preset_configs)[start].tune_2_plane_early_out_limit_correlation;
	}
	// Start and end node are not the same - so interpolate between them
	else
	{
		auto& node_a = (*preset_configs)[start];
		auto& node_b = (*preset_configs)[end];

		float wt_range = node_b.quality - node_a.quality;
		assert(wt_range > 0);

		// Compute interpolation factors
		float wt_node_a = (node_b.quality - quality) / wt_range;
		float wt_node_b = (quality - node_a.quality) / wt_range;

		#define LERP(param) ((node_a.param * wt_node_a) + (node_b.param * wt_node_b))
		#define LERPI(param) astc::flt2int_rtn(\
		                         (((float)node_a.param) * wt_node_a) + \
		                         (((float)node_b.param) * wt_node_b))
		#define LERPUI(param) (unsigned int)LERPI(param)

		config.tune_partition_count_limit = LERPI(tune_partition_count_limit);
		config.tune_partition_index_limit = LERPI(tune_partition_index_limit);
		config.tune_block_mode_limit = LERPI(tune_block_mode_limit);
		config.tune_refinement_limit = LERPI(tune_refinement_limit);
		config.tune_candidate_limit = astc::min(LERPUI(tune_candidate_limit),
		                                        TUNE_MAX_TRIAL_CANDIDATES);
		config.tune_db_limit = astc::max(LERP(tune_db_limit_a_base) - 35 * ltexels,
		                                 LERP(tune_db_limit_b_base) - 19 * ltexels);

		config.tune_mode0_mse_overshoot = LERP(tune_mode0_mse_overshoot);
		config.tune_refinement_mse_overshoot = LERP(tune_refinement_mse_overshoot);

		config.tune_2_partition_early_out_limit_factor = LERP(tune_2_partition_early_out_limit_factor);
		config.tune_3_partition_early_out_limit_factor = LERP(tune_3_partition_early_out_limit_factor);
		config.tune_2_plane_early_out_limit_factor = LERP(tune_2_plane_early_out_limit_factor);
		config.tune_2_plane_early_out_limit_correlation = LERP(tune_2_plane_early_out_limit_correlation);

		#undef LERP
		#undef LERPI
		#undef LERPUI
	}

	// Set heuristics to the defaults for each color profile
	config.v_rgba_radius = 0;
	config.v_rgba_mean_stdev_mix = 0.0f;

	config.cw_r_weight = 1.0f;
	config.cw_g_weight = 1.0f;
	config.cw_b_weight = 1.0f;
	config.cw_a_weight = 1.0f;

	config.a_scale_radius = 0;

	config.rgbm_m_scale = 0.0f;

	config.profile = profile;

	// Values in this enum are from an external user, so not guaranteed to be
	// bounded to the enum values
	switch(static_cast<int>(profile))
	{
	case ASTCENC_PRF_LDR:
	case ASTCENC_PRF_LDR_SRGB:
		config.v_rgb_power = 1.0f;
		config.v_rgb_base = 1.0f;
		config.v_rgb_mean = 0.0f;
		config.v_rgb_stdev = 0.0f;

		config.v_a_power = 1.0f;
		config.v_a_base = 1.0f;
		config.v_a_mean = 0.0f;
		config.v_a_stdev = 0.0f;
		break;
	case ASTCENC_PRF_HDR_RGB_LDR_A:
		config.v_rgb_power = 0.75f;
		config.v_rgb_base = 0.0f;
		config.v_rgb_mean = 1.0f;
		config.v_rgb_stdev = 0.0f;

		config.v_a_power = 1.0f;
		config.v_a_base = 0.05f;
		config.v_a_mean = 0.0f;
		config.v_a_stdev = 0.0f;

		config.tune_db_limit = 999.0f;
		break;
	case ASTCENC_PRF_HDR:
		config.v_rgb_power = 0.75f;
		config.v_rgb_base = 0.0f;
		config.v_rgb_mean = 1.0f;
		config.v_rgb_stdev = 0.0f;

		config.v_a_power = 0.75f;
		config.v_a_base = 0.0f;
		config.v_a_mean = 1.0f;
		config.v_a_stdev = 0.0f;

		config.tune_db_limit = 999.0f;
		break;
	default:
		return ASTCENC_ERR_BAD_PROFILE;
	}

	// Flags field must not contain any unknown flag bits
	status = validate_flags(flags);
	if (status != ASTCENC_SUCCESS)
	{
		return status;
	}

	if (flags & ASTCENC_FLG_MAP_NORMAL)
	{
		config.cw_g_weight = 0.0f;
		config.cw_b_weight = 0.0f;
		config.tune_2_partition_early_out_limit_factor *= 1.5f;
		config.tune_3_partition_early_out_limit_factor *= 1.5f;
		config.tune_2_plane_early_out_limit_correlation = 0.99f;

		if (flags & ASTCENC_FLG_USE_PERCEPTUAL)
		{
			config.b_deblock_weight = 1.8f;
			config.v_rgba_radius = 3;
			config.v_rgba_mean_stdev_mix = 0.0f;
			config.v_rgb_mean = 0.0f;
			config.v_rgb_stdev = 50.0f;
			config.v_a_mean = 0.0f;
			config.v_a_stdev = 50.0f;
		}
	}

	if (flags & ASTCENC_FLG_MAP_MASK)
	{
		config.v_rgba_radius = 3;
		config.v_rgba_mean_stdev_mix = 0.03f;
		config.v_rgb_mean = 0.0f;
		config.v_rgb_stdev = 25.0f;
		config.v_a_mean = 0.0f;
		config.v_a_stdev = 25.0f;
	}

	if (flags & ASTCENC_FLG_MAP_RGBM)
	{
		config.rgbm_m_scale = 5.0f;
		config.cw_a_weight = 2.0f * config.rgbm_m_scale;
	}

	config.flags = flags;

	return ASTCENC_SUCCESS;
}

/* See header for documentation. */
astcenc_error astcenc_context_alloc(
	const astcenc_config* configp,
	unsigned int thread_count,
	astcenc_context** context
) {
	astcenc_error status;
	astcenc_context* ctx = nullptr;
	block_size_descriptor* bsd = nullptr;
	const astcenc_config& config = *configp;

	status = validate_cpu_float();
	if (status != ASTCENC_SUCCESS)
	{
		return status;
	}

	status = validate_cpu_isa();
	if (status != ASTCENC_SUCCESS)
	{
		return status;
	}

	if (thread_count == 0)
	{
		return ASTCENC_ERR_BAD_PARAM;
	}

#if defined(ASTCENC_DIAGNOSTICS)
	// Force single threaded compressor use in diagnostic mode.
	if (thread_count != 1)
	{
		return ASTCENC_ERR_BAD_PARAM;
	}
#endif

	ctx = new astcenc_context;
	ctx->thread_count = thread_count;
	ctx->config = config;
	ctx->working_buffers = nullptr;

	// These are allocated per-compress, as they depend on image size
	ctx->input_averages = nullptr;
	ctx->input_variances = nullptr;
	ctx->input_alpha_averages = nullptr;

	// Copy the config first and validate the copy (we may modify it)
	status = validate_config(ctx->config);
	if (status != ASTCENC_SUCCESS)
	{
		delete ctx;
		return status;
	}

	bsd = new block_size_descriptor;
	bool can_omit_modes = config.flags & ASTCENC_FLG_SELF_DECOMPRESS_ONLY;
	init_block_size_descriptor(config.block_x, config.block_y, config.block_z,
	                           can_omit_modes, static_cast<float>(config.tune_block_mode_limit) / 100.0f, *bsd);
	ctx->bsd = bsd;

#if !defined(ASTCENC_DECOMPRESS_ONLY)
	// Do setup only needed by compression
	if (!(status & ASTCENC_FLG_DECOMPRESS_ONLY))
	{
		// Expand deblock supression into a weight scale per texel in the block
		expand_deblock_weights(*ctx);

		// Turn a dB limit into a per-texel error for faster use later
		if ((ctx->config.profile == ASTCENC_PRF_LDR) || (ctx->config.profile == ASTCENC_PRF_LDR_SRGB))
		{
			ctx->config.tune_db_limit = astc::pow(0.1f, ctx->config.tune_db_limit * 0.1f) * 65535.0f * 65535.0f;
		}
		else
		{
			ctx->config.tune_db_limit = 0.0f;
		}

		size_t worksize = sizeof(compress_symbolic_block_buffers) * thread_count;
		ctx->working_buffers = aligned_malloc<compress_symbolic_block_buffers>(worksize, ASTCENC_VECALIGN);
		static_assert((sizeof(compress_symbolic_block_buffers) % ASTCENC_VECALIGN) == 0,
		              "compress_symbolic_block_buffers size must be multiple of vector alignment");
		if (!ctx->working_buffers)
		{
			term_block_size_descriptor(*bsd);
			delete bsd;
			delete ctx;
			*context = nullptr;
			return ASTCENC_ERR_OUT_OF_MEM;
		}
	}
#endif

#if defined(ASTCENC_DIAGNOSTICS)
	ctx->trace_log = new TraceLog(ctx->config.trace_file_path);
	if(!ctx->trace_log->m_file)
	{
		return ASTCENC_ERR_DTRACE_FAILURE;
	}

	trace_add_data("block_x", config.block_x);
	trace_add_data("block_y", config.block_y);
	trace_add_data("block_z", config.block_z);
#endif

	*context = ctx;

	// TODO: Currently static memory; should move to context memory
#if !defined(ASTCENC_DECOMPRESS_ONLY)
	prepare_angular_tables();
#endif
	init_quant_mode_table();

	return ASTCENC_SUCCESS;
}

/* See header dor documentation. */
void astcenc_context_free(
	astcenc_context* ctx
) {
	if (ctx)
	{
		aligned_free<compress_symbolic_block_buffers>(ctx->working_buffers);
		term_block_size_descriptor(*(ctx->bsd));
#if defined(ASTCENC_DIAGNOSTICS)
		delete ctx->trace_log;
#endif
		delete ctx->bsd;
		delete ctx;
	}
}

#if !defined(ASTCENC_DECOMPRESS_ONLY)

/**
 * @brief Compress an image, after any preflight has completed.
 *
 * @param[out] ctx            The compressor context.
 * @param      thread_index   The thread index.
 * @param      image          The intput image.
 * @param      swizzle        The input swizzle.
 * @param[out] buffer         The output array for the compressed data.
 */
static void compress_image(
	astcenc_context& ctx,
	unsigned int thread_index,
	const astcenc_image& image,
	const astcenc_swizzle& swizzle,
	uint8_t* buffer
) {
	const block_size_descriptor *bsd = ctx.bsd;
	astcenc_profile decode_mode = ctx.config.profile;
	imageblock blk;

	int block_x = bsd->xdim;
	int block_y = bsd->ydim;
	int block_z = bsd->zdim;

	int dim_x = image.dim_x;
	int dim_y = image.dim_y;
	int dim_z = image.dim_z;

	int xblocks = (dim_x + block_x - 1) / block_x;
	int yblocks = (dim_y + block_y - 1) / block_y;
	int zblocks = (dim_z + block_z - 1) / block_z;

	int row_blocks = xblocks;
	int plane_blocks = xblocks * yblocks;

	// Use preallocated scratch buffer
	auto& temp_buffers = ctx.working_buffers[thread_index];

	// Only the first thread actually runs the initializer
	ctx.manage_compress.init(zblocks * yblocks * xblocks);

	// All threads run this processing loop until there is no work remaining
	while (true)
	{
		unsigned int count;
		unsigned int base = ctx.manage_compress.get_task_assignment(16, count);
		if (!count)
		{
			break;
		}

		for (unsigned int i = base; i < base + count; i++)
		{
			// Decode i into x, y, z block indices
			int z = i / plane_blocks;
			unsigned int rem = i - (z * plane_blocks);
			int y = rem / row_blocks;
			int x = rem - (y * row_blocks);

			// Test if we can apply some basic alpha-scale RDO
			bool use_full_block = true;
			if (ctx.config.a_scale_radius != 0 && block_z == 1)
			{
				int start_x = x * block_x;
				int end_x = astc::min(dim_x, start_x + block_x);

				int start_y = y * block_y;
				int end_y = astc::min(dim_y, start_y + block_y);

				// SATs accumulate error, so don't test exactly zero. Test for
				// less than 1 alpha in the expanded block footprint that
				// includes the alpha radius.
				int x_footprint = block_x + 2 * (ctx.config.a_scale_radius - 1);

				int y_footprint = block_y + 2 * (ctx.config.a_scale_radius - 1);

				float footprint = (float)(x_footprint * y_footprint);
				float threshold = 0.9f / (255.0f * footprint);

				// Do we have any alpha values?
				use_full_block = false;
				for (int ay = start_y; ay < end_y; ay++)
				{
					for (int ax = start_x; ax < end_x; ax++)
					{
						float a_avg = ctx.input_alpha_averages[ay * dim_x + ax];
						if (a_avg > threshold)
						{
							use_full_block = true;
							ax = end_x;
							ay = end_y;
						}
					}
				}
			}

			// Fetch the full block for compression
			if (use_full_block)
			{
				fetch_imageblock(decode_mode, image, blk, *bsd, x * block_x, y * block_y, z * block_z, swizzle);
			}
			// Apply alpha scale RDO - substitute constant color block
			else
			{
				blk.origin_texel = vfloat4::zero();
				blk.data_min = vfloat4::zero();
				blk.data_max = blk.data_min;
				blk.grayscale = false;
			}

			int offset = ((z * yblocks + y) * xblocks + x) * 16;
			uint8_t *bp = buffer + offset;
			physical_compressed_block* pcb = reinterpret_cast<physical_compressed_block*>(bp);
			compress_block(ctx, image, blk, *pcb, temp_buffers);
		}

		ctx.manage_compress.complete_task_assignment(count);
	}
}

#endif

/* See header for documentation. */
astcenc_error astcenc_compress_image(
	astcenc_context* ctx,
	astcenc_image* imagep,
	const astcenc_swizzle* swizzle,
	uint8_t* data_out,
	size_t data_len,
	unsigned int thread_index
) {
#if defined(ASTCENC_DECOMPRESS_ONLY)
	(void)ctx;
	(void)imagep;
	(void)swizzle;
	(void)data_out;
	(void)data_len;
	(void)thread_index;
	return ASTCENC_ERR_BAD_CONTEXT;
#else
	astcenc_error status;
	astcenc_image& image = *imagep;

	if (ctx->config.flags & ASTCENC_FLG_DECOMPRESS_ONLY)
	{
		return ASTCENC_ERR_BAD_CONTEXT;
	}

	status = validate_compression_swizzle(*swizzle);
	if (status != ASTCENC_SUCCESS)
	{
		return status;
	}

	if (thread_index >= ctx->thread_count)
	{
		return ASTCENC_ERR_BAD_PARAM;
	}

	unsigned int block_x = ctx->config.block_x;
	unsigned int block_y = ctx->config.block_y;
	unsigned int block_z = ctx->config.block_z;

	unsigned int xblocks = (image.dim_x + block_x - 1) / block_x;
	unsigned int yblocks = (image.dim_y + block_y - 1) / block_y;
	unsigned int zblocks = (image.dim_z + block_z - 1) / block_z;

	// Check we have enough output space (16 bytes per block)
	size_t size_needed = xblocks * yblocks * zblocks * 16;
	if (data_len < size_needed)
	{
		return ASTCENC_ERR_OUT_OF_MEM;
	}

	// If context thread count is one then implicitly reset
	if (ctx->thread_count == 1)
	{
		astcenc_compress_reset(ctx);
	}

	if (ctx->config.v_rgb_mean != 0.0f || ctx->config.v_rgb_stdev != 0.0f ||
	    ctx->config.v_a_mean != 0.0f || ctx->config.v_a_stdev != 0.0f ||
	    ctx->config.a_scale_radius != 0)
	{
		// First thread to enter will do setup, other threads will subsequently
		// enter the critical section but simply skip over the initialization
		auto init_avg_var = [ctx, &image, swizzle]() {
			// Perform memory allocations for the destination buffers
			size_t texel_count = image.dim_x * image.dim_y * image.dim_z;
			ctx->input_averages = new vfloat4[texel_count];
			ctx->input_variances = new vfloat4[texel_count];
			ctx->input_alpha_averages = new float[texel_count];

			return init_compute_averages_and_variances(
				image, ctx->config.v_rgb_power, ctx->config.v_a_power,
				ctx->config.v_rgba_radius, ctx->config.a_scale_radius, *swizzle,
				ctx->avg_var_preprocess_args);
		};

		// Only the first thread actually runs the initializer
		ctx->manage_avg_var.init(init_avg_var);

		// All threads will enter this function and dynamically grab work
		compute_averages_and_variances(*ctx, ctx->avg_var_preprocess_args);
	}

	// Wait for compute_averages_and_variances to complete before compressing
	ctx->manage_avg_var.wait();

	compress_image(*ctx, thread_index, image, *swizzle, data_out);

	// Wait for compress to complete before freeing memory
	ctx->manage_compress.wait();

	auto term_compress = [ctx]() {
		delete[] ctx->input_averages;
		ctx->input_averages = nullptr;

		delete[] ctx->input_variances;
		ctx->input_variances = nullptr;

		delete[] ctx->input_alpha_averages;
		ctx->input_alpha_averages = nullptr;
	};

	// Only the first thread to arrive actually runs the term
	ctx->manage_compress.term(term_compress);

	return ASTCENC_SUCCESS;
#endif
}

/* See header for documentation. */
astcenc_error astcenc_compress_reset(
	astcenc_context* ctx
) {
#if defined(ASTCENC_DECOMPRESS_ONLY)
	(void)ctx;
	return ASTCENC_ERR_BAD_CONTEXT;
#else
	if (ctx->config.flags & ASTCENC_FLG_DECOMPRESS_ONLY)
	{
		return ASTCENC_ERR_BAD_CONTEXT;
	}

	ctx->manage_avg_var.reset();
	ctx->manage_compress.reset();
	return ASTCENC_SUCCESS;
#endif
}

/* See header for documentation. */
astcenc_error astcenc_decompress_image(
	astcenc_context* ctx,
	const uint8_t* data,
	size_t data_len,
	astcenc_image* image_outp,
	const astcenc_swizzle* swizzle,
	unsigned int thread_index
) {
	astcenc_error status;
	astcenc_image& image_out = *image_outp;

	// Today this doesn't matter (working set on stack) but might in future ...
	if (thread_index >= ctx->thread_count)
	{
		return ASTCENC_ERR_BAD_PARAM;
	}

	status = validate_decompression_swizzle(*swizzle);
	if (status != ASTCENC_SUCCESS)
	{
		return status;
	}

	unsigned int block_x = ctx->config.block_x;
	unsigned int block_y = ctx->config.block_y;
	unsigned int block_z = ctx->config.block_z;

	unsigned int xblocks = (image_out.dim_x + block_x - 1) / block_x;
	unsigned int yblocks = (image_out.dim_y + block_y - 1) / block_y;
	unsigned int zblocks = (image_out.dim_z + block_z - 1) / block_z;

	int row_blocks = xblocks;
	int plane_blocks = xblocks * yblocks;

	// Check we have enough output space (16 bytes per block)
	size_t size_needed = xblocks * yblocks * zblocks * 16;
	if (data_len < size_needed)
	{
		return ASTCENC_ERR_OUT_OF_MEM;
	}

	imageblock blk;

	// If context thread count is one then implicitly reset
	if (ctx->thread_count == 1)
	{
		astcenc_decompress_reset(ctx);
	}

	// Only the first thread actually runs the initializer
	ctx->manage_decompress.init(zblocks * yblocks * xblocks);

	// All threads run this processing loop until there is no work remaining
	while (true)
	{
		unsigned int count;
		unsigned int base = ctx->manage_decompress.get_task_assignment(128, count);
		if (!count)
		{
			break;
		}

		for (unsigned int i = base; i < base + count; i++)
		{
			// Decode i into x, y, z block indices
			int z = i / plane_blocks;
			unsigned int rem = i - (z * plane_blocks);
			int y = rem / row_blocks;
			int x = rem - (y * row_blocks);

			unsigned int offset = (((z * yblocks + y) * xblocks) + x) * 16;
			const uint8_t* bp = data + offset;
			physical_compressed_block pcb = *(const physical_compressed_block*)bp;
			symbolic_compressed_block scb;

			physical_to_symbolic(*ctx->bsd, pcb, scb);

			decompress_symbolic_block(ctx->config.profile, *ctx->bsd,
			                          x * block_x, y * block_y, z * block_z,
			                          scb, blk);

			write_imageblock(image_out, blk, *ctx->bsd,
			                 x * block_x, y * block_y, z * block_z, *swizzle);
		}

		ctx->manage_decompress.complete_task_assignment(count);
	}

	return ASTCENC_SUCCESS;
}

/* See header for documentation. */
astcenc_error astcenc_decompress_reset(
	astcenc_context* ctx
) {
	ctx->manage_decompress.reset();
	return ASTCENC_SUCCESS;
}

/* See header for documentation. */
astcenc_error astcenc_get_block_info(
	astcenc_context* ctx,
	const uint8_t data[16],
	astcenc_block_info* info
) {
#if defined(ASTCENC_DECOMPRESS_ONLY)
	(void)ctx;
	(void)data;
	(void)info;
	return ASTCENC_ERR_BAD_CONTEXT;
#else
	// Decode the compressed data into a symbolic form
	physical_compressed_block pcb = *(const physical_compressed_block*)data;
	symbolic_compressed_block scb;
	physical_to_symbolic(*ctx->bsd, pcb, scb);

	// Fetch the appropriate partition and decimation tables
	block_size_descriptor& bsd = *ctx->bsd;

	// Start from a clean slate
	memset(info, 0, sizeof(*info));

	// Basic info we can always populate
	info->profile = ctx->config.profile;

	info->block_x = ctx->config.block_x;
	info->block_y = ctx->config.block_y;
	info->block_z = ctx->config.block_z;
	info->texel_count = bsd.texel_count;

	// Check for error blocks first
	info->is_error_block = scb.block_type == SYM_BTYPE_ERROR;
	if (info->is_error_block)
	{
		return ASTCENC_SUCCESS;
	}

	// Check for constant color blocks second
	info->is_constant_block = scb.block_type == SYM_BTYPE_CONST_F16 ||
	                          scb.block_type == SYM_BTYPE_CONST_U16;
	if (info->is_constant_block)
	{
		return ASTCENC_SUCCESS;
	}

	// Otherwise handle a full block ; known to be valid after conditions above have been checked
	int partition_count = scb.partition_count;
	const auto& pi = bsd.get_partition_info(partition_count, scb.partition_index);

	const block_mode& bm = bsd.get_block_mode(scb.block_mode);
	const decimation_info& di = *bsd.decimation_tables[bm.decimation_mode];

	info->weight_x = di.weight_x;
	info->weight_y = di.weight_y;
	info->weight_z = di.weight_z;

	info->is_dual_plane_block = bm.is_dual_plane != 0;

	info->partition_count = scb.partition_count;
	info->partition_index = scb.partition_index;
	info->dual_plane_component = scb.plane2_component;

	info->color_level_count = get_quant_level(scb.get_color_quant_mode());
	info->weight_level_count = get_quant_level(bm.get_weight_quant_mode());

	// Unpack color endpoints for each active partition
	for (unsigned int i = 0; i < scb.partition_count; i++)
	{
		bool rgb_hdr;
		bool a_hdr;
		vint4 endpnt[2];

		unpack_color_endpoints(ctx->config.profile,
		                       scb.color_formats[i],
		                       scb.get_color_quant_mode(),
		                       scb.color_values[i],
		                       rgb_hdr, a_hdr,
		                       endpnt[0], endpnt[1]);

		// Store the color endpoint mode info
		info->color_endpoint_modes[i] = scb.color_formats[i];
		info->is_hdr_block = info->is_hdr_block || rgb_hdr || a_hdr;

		// Store the unpacked and decoded color endpoint
		vmask4 hdr_mask(rgb_hdr, rgb_hdr, rgb_hdr, a_hdr);
		for (int j = 0; j < 2; j++)
		{
			vint4 color_lns = lns_to_sf16(endpnt[j]);
			vint4 color_unorm = unorm16_to_sf16(endpnt[j]);
			vint4 datai = select(color_unorm, color_lns, hdr_mask);
			store(float16_to_float(datai), info->color_endpoints[i][j]);
		}
	}

	// Unpack weights for each texel
	int weight_plane1[BLOCK_MAX_TEXELS];
	int weight_plane2[BLOCK_MAX_TEXELS];

	unpack_weights(bsd, scb, di, bm.is_dual_plane, bm.get_weight_quant_mode(), weight_plane1, weight_plane2);
	for (unsigned int i = 0; i < bsd.texel_count; i++)
	{
		info->weight_values_plane1[i] = (float)weight_plane1[i] * (1.0f / WEIGHTS_TEXEL_SUM);
		if (info->is_dual_plane_block)
		{
			info->weight_values_plane2[i] = (float)weight_plane2[i] * (1.0f / WEIGHTS_TEXEL_SUM);
		}
	}

	// Unpack partition assignments for each texel
	for (unsigned int i = 0; i < bsd.texel_count; i++)
	{
		info->partition_assignment[i] = pi.partition_of_texel[i];
	}

	return ASTCENC_SUCCESS;
#endif
}

/* See header for documentation. */
const char* astcenc_get_error_string(
	astcenc_error status
) {
	// Values in this enum are from an external user, so not guaranteed to be
	// bounded to the enum values
	switch(static_cast<int>(status))
	{
	case ASTCENC_SUCCESS:
		return "ASTCENC_SUCCESS";
	case ASTCENC_ERR_OUT_OF_MEM:
		return "ASTCENC_ERR_OUT_OF_MEM";
	case ASTCENC_ERR_BAD_CPU_FLOAT:
		return "ASTCENC_ERR_BAD_CPU_FLOAT";
	case ASTCENC_ERR_BAD_CPU_ISA:
		return "ASTCENC_ERR_BAD_CPU_ISA";
	case ASTCENC_ERR_BAD_PARAM:
		return "ASTCENC_ERR_BAD_PARAM";
	case ASTCENC_ERR_BAD_BLOCK_SIZE:
		return "ASTCENC_ERR_BAD_BLOCK_SIZE";
	case ASTCENC_ERR_BAD_PROFILE:
		return "ASTCENC_ERR_BAD_PROFILE";
	case ASTCENC_ERR_BAD_QUALITY:
		return "ASTCENC_ERR_BAD_QUALITY";
	case ASTCENC_ERR_BAD_FLAGS:
		return "ASTCENC_ERR_BAD_FLAGS";
	case ASTCENC_ERR_BAD_SWIZZLE:
		return "ASTCENC_ERR_BAD_SWIZZLE";
	case ASTCENC_ERR_BAD_CONTEXT:
		return "ASTCENC_ERR_BAD_CONTEXT";
	case ASTCENC_ERR_NOT_IMPLEMENTED:
		return "ASTCENC_ERR_NOT_IMPLEMENTED";
#if defined(ASTCENC_DIAGNOSTICS)
	case ASTCENC_ERR_DTRACE_FAILURE:
		return "ASTCENC_ERR_DTRACE_FAILURE";
#endif
	default:
		return nullptr;
	}
}
