#pragma once

#include "../pe_raw/pe_view.hpp"

#include <cstdint>
#include <string>

struct XorTarget {
    std::uint64_t func_start{0};
    std::uint64_t func_end{0};
    std::uint8_t  xor_key{0};
};

struct PackOptions {
    bool          remove_aslr{false};
    std::uint32_t guard_cf_oep_rva{0};
};

struct PackResult {
    pe_raw::AddedSection stub_section;
    std::uint32_t        entry_point_rva{0};
    std::uint64_t        entry_point_va{0};
};
