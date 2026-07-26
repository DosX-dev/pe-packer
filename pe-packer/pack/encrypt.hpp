#pragma once

#include "types.hpp"

#include <cstdint>
#include <string>

class c_core;

void xor_region_by_va(
    pe_raw::PeView& view,
    std::uint64_t start_va,
    std::uint64_t end_va,
    std::uint8_t key
);

void xor_section_by_name(
    pe_raw::PeView& view,
    const char* section_name,
    std::uint8_t key
);

void encrypt_function_range(c_core& core, const XorTarget& target);
void emit_function_decrypt_stub(c_core& core, const XorTarget& target);
void encrypt_section(c_core& core, const std::string& section_name);
