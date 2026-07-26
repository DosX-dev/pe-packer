#include "stub.hpp"

#include "../pe_raw/pe_error.hpp"

PackResult inject_stub(
    pe_raw::PeView& view,
    const std::string& section_name,
    const std::vector<std::uint8_t>& stub_bytes,
    std::uint32_t stub_entry_offset,
    const PackOptions& options
) {
    if (options.remove_aslr) {
        pe_raw::clear_aslr(view);
    }

    if (stub_entry_offset >= stub_bytes.size()) {
        throw pe_raw::PeError("stub entry offset is outside stub bytes");
    }

    PackResult result;
    result.stub_section = pe_raw::add_executable_section(
        view,
        section_name,
        stub_bytes,
        true,
        stub_entry_offset
    );

    result.entry_point_rva = view.entry_point_rva;
    result.entry_point_va = pe_raw::rva_to_va(view, result.entry_point_rva);

    if ((view.dll_characteristics & IMAGE_DLLCHARACTERISTICS_GUARD_CF) != 0) {
        pe_raw::add_guard_cf_target(view, result.entry_point_rva);
        if (options.guard_cf_oep_rva != 0) {
            pe_raw::add_guard_cf_target(view, options.guard_cf_oep_rva);
        }
    }

    return result;
}
