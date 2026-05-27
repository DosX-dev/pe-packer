#pragma once

#include <cstdint>

namespace mutation_profile {

    inline std::uint32_t clamp_u32(std::uint32_t value, std::uint32_t lo, std::uint32_t hi) {
        if (value < lo) {
            return lo;
        }
        if (value > hi) {
            return hi;
        }
        return value;
    }

    // complexity profile such as (1...10)
    struct Profile {
        std::uint32_t level{ 1 };

        std::uint32_t obfuscation_passes{ 0 };
        std::uint32_t jump_junk_min{ 0 };
        std::uint32_t jump_junk_max{ 0 };
        std::uint32_t call_depth{ 0 };
        std::uint32_t push_pop_rounds{ 0 };
        std::uint32_t condition_actions_min{ 0 };
        std::uint32_t condition_actions_max{ 0 };
        std::uint32_t mba_weight_percent{ 0 };
        std::uint32_t mba_inner_ops{ 0 };
        std::uint32_t fake_instr_min{ 0 };
        std::uint32_t fake_instr_max{ 0 };
        std::uint32_t adasm_junk_min{ 0 };
        std::uint32_t adasm_junk_max{ 0 };
    };

    inline std::uint32_t clamp_level(std::uint32_t level) {
        return clamp_u32(level, 1u, 10u);
    }

    inline std::uint32_t level_from_slider(std::uint32_t slider_1_to_100) {
        const std::uint32_t clamped = clamp_u32(slider_1_to_100, 1u, 100u);
        return clamp_level((clamped - 1) / 10 + 1);
    }

    inline std::uint32_t lerp_u32(std::uint32_t lo, std::uint32_t hi, std::uint32_t level) {
        const std::uint32_t t = level - 1;
        return lo + (hi - lo) * t / 9;
    }

    inline Profile profile_for_level(std::uint32_t raw_level) {
        const std::uint32_t level = clamp_level(raw_level);

        Profile profile{};
        profile.level = level;

        profile.obfuscation_passes = lerp_u32(5, 200, level);
        profile.jump_junk_min = lerp_u32(0, 20, level);
        profile.jump_junk_max = lerp_u32(5, 80, level);
        profile.call_depth = lerp_u32(0, 3, level);
        profile.push_pop_rounds = lerp_u32(1, 8, level);
        profile.condition_actions_min = lerp_u32(2, 15, level);
        profile.condition_actions_max = lerp_u32(5, 80, level);
        profile.mba_weight_percent = lerp_u32(10, 50, level);
        profile.mba_inner_ops = lerp_u32(2, 15, level);
        profile.fake_instr_min = 1;
        profile.fake_instr_max = lerp_u32(16, 128, level);
        profile.adasm_junk_min = 1;
        profile.adasm_junk_max = lerp_u32(16, 128, level);

        return profile;
    }

}
