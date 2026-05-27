#pragma once

#include "pe_error.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

#define align_up(value, alignment) (value + alignment - 1) & ~(alignment - 1)
#define max_u32(a, b) ((a) > (b) ? (a) : (b))
#define min_size(a, b) ((a) < (b) ? (a) : (b))

constexpr std::uint16_t kDosMagic = 0x5A4D;
constexpr std::uint32_t kPeMagic = 0x00004550;
constexpr std::uint16_t kOpt32 = 0x010B;
constexpr std::uint16_t kOpt64 = 0x020B;

namespace pe_raw {

struct PeView {
    std::vector<std::uint8_t> bytes;
    bool                      is64{false};
    std::size_t               nt_off{0};
    std::size_t               file_hdr_off{0};
    std::size_t               opt_hdr_off{0};
    std::size_t               section_tbl_off{0};
    std::uint16_t             num_sections{0};
    std::uint32_t             file_alignment{0};
    std::uint32_t             section_alignment{0};
    std::uint32_t             size_of_headers{0};
    std::uint32_t             size_of_image{0};
    std::uint16_t             dll_characteristics{0};
    std::uint64_t             image_base{0};
    std::uint32_t             entry_point_rva{0};
    std::uint32_t             old_reloc_rva{0};
    std::uint32_t             old_reloc_size{0};
};

struct ImportLookup {
    std::uint32_t iat_rva{0};
    std::size_t   iat_file_off{0};
    bool          found{false};
};

struct AddedSection {
    std::uint32_t rva{0};
    std::uint32_t raw_offset{0};
    std::uint32_t raw_size{0};
    std::uint32_t virtual_size{0};
    std::string   name;
};

std::uint32_t peek_next_section_rva(const PeView& view);

bool has_clr_directory(const PeView& view);

void set_section_characteristics(PeView& view, const char* section_name, std::uint32_t characteristics);

std::vector<std::uint8_t> read_file_bytes(const std::string& path);
void write_file_bytes(const std::string& path, const std::vector<std::uint8_t>& data);

PeView parse_pe(std::vector<std::uint8_t> raw);

struct ParseFileResult {
    std::optional<PeView> view;
    std::string           error_message;

    bool ok() const { return view.has_value(); }
};

// validate pe 
ParseFileResult parse_pe_file(const std::string& path);

IMAGE_SECTION_HEADER& section_at(PeView& view, std::size_t index);
const IMAGE_SECTION_HEADER* section_for_rva(PeView& view, std::uint32_t rva);
const IMAGE_SECTION_HEADER* section_by_name(PeView& view, const char* name);

std::size_t rva_to_file_off(const IMAGE_SECTION_HEADER& section, std::uint32_t rva);
std::size_t any_rva_to_file_off(PeView& view, std::uint32_t rva);

std::uint32_t va_to_rva(const PeView& view, std::uint64_t va);
std::uint64_t rva_to_va(const PeView& view, std::uint32_t rva);

ImportLookup find_iat_slot(PeView& view, const char* module, const char* symbol);

AddedSection add_executable_section(
    PeView& view,
    const std::string& section_name,
    const std::vector<std::uint8_t>& section_data,
    bool set_entry_point,
    std::uint32_t entry_offset_in_section
);

void set_dll_characteristics(PeView& view, std::uint16_t characteristics);
void clear_aslr(PeView& view);

// IMAGE_DLLCHARACTERISTICS_GUARD_CF
void add_guard_cf_target(PeView& view, std::uint32_t target_rva);

// patch bytes at rva
void patch_at_rva(PeView& view, std::uint32_t rva, const std::uint8_t* data, std::size_t size);

}
