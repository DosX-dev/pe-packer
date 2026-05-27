#pragma once

#include "pe_view.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pe_raw {

    struct PackOptions {
        bool remove_aslr{ false };
        std::uint32_t guard_cf_oep_rva{ 0 };
    };

    struct PackResult {
        AddedSection stub_section;
        std::uint32_t entry_point_rva{ 0 };
        std::uint64_t entry_point_va{ 0 };
    };

    PackResult packer_stub(
        PeView& view,
        const std::string& section_name,
        const std::vector<std::uint8_t>& stub_bytes,
        std::uint32_t stub_entry_offset,
        const PackOptions& options = {}
    );

    void xor_region_by_va(
        PeView& view,
        std::uint64_t start_va,
        std::uint64_t end_va,
        std::uint8_t key
    );

    void xor_section_by_name(PeView& view, const char* section_name, std::uint8_t key);

}
