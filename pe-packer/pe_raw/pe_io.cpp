#include "pe_io.hpp"

#include <algorithm>

namespace pe_raw {

    PackResult packer_stub(
        PeView& view,
        const std::string& section_name,
        const std::vector<std::uint8_t>& stub_bytes,
        std::uint32_t stub_entry_offset,
        const PackOptions& options
    ) {
        if (options.remove_aslr) {
            clear_aslr(view);
        }

        if (stub_entry_offset >= stub_bytes.size()) {
            throw PeError("stub entry offset is outside stub bytes");
        }

        PackResult result;
        result.stub_section = add_executable_section(
            view,
            section_name,
            stub_bytes,
            true,
            stub_entry_offset
        );

        result.entry_point_rva = view.entry_point_rva;
        result.entry_point_va = rva_to_va(view, result.entry_point_rva);

        if ((view.dll_characteristics & IMAGE_DLLCHARACTERISTICS_GUARD_CF) != 0) {
            add_guard_cf_target(view, result.entry_point_rva);
            if (options.guard_cf_oep_rva != 0) {
                add_guard_cf_target(view, options.guard_cf_oep_rva);
            }
        }

        return result;
    }

    void xor_region_by_va(
        PeView& view,
        std::uint64_t start_va,
        std::uint64_t end_va,
        std::uint8_t key
    ) {
        if (start_va >= end_va) {
            return;
        }

        const std::uint32_t start_rva = va_to_rva(view, start_va);
        const std::uint32_t end_rva = va_to_rva(view, end_va);
        const std::uint32_t size = end_rva - start_rva;

        const auto* section = section_for_rva(view, start_rva);
        if (!section) {
            throw PeError("xor region start is not inside any section");
        }

        const std::uint32_t section_end_rva = section->VirtualAddress +
            max_u32(section->Misc.VirtualSize, section->SizeOfRawData);

        if (end_rva > section_end_rva) {
            throw PeError("xor region crosses section boundary");
        }

        const std::size_t file_off = rva_to_file_off(*section, start_rva);
        if (file_off + size > view.bytes.size()) {
            throw PeError("xor region exceeds file bounds");
        }

        for (std::uint32_t i = 0; i < size; ++i) {
            view.bytes[file_off + i] ^= key;
        }
    }

    void xor_section_by_name(PeView& view, const char* section_name, std::uint8_t key) {
        const auto* section = section_by_name(view, section_name);
        if (!section) {
            throw PeError(std::string("section not found: ") + section_name);
        }

        const std::size_t file_off = section->PointerToRawData;
        const std::size_t size = section->SizeOfRawData;

        if (file_off + size > view.bytes.size()) {
            throw PeError("section raw data exceeds file bounds");
        }

        for (std::size_t i = 0; i < size; ++i) {
            view.bytes[file_off + i] ^= key;
        }
    }

}