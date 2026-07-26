#pragma once

#include "types.hpp"

#include <cstdint>
#include <string>
#include <vector>

PackResult inject_stub(
    pe_raw::PeView& view,
    const std::string& section_name,
    const std::vector<std::uint8_t>& stub_bytes,
    std::uint32_t stub_entry_offset,
    const PackOptions& options = {}
);
