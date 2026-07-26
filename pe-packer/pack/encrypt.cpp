#include "encrypt.hpp"

#include "../core/core.hpp"
#include "../handler/handler.hpp"
#include "../utils/utils.hpp"

std::uint32_t section_payload_size(const IMAGE_SECTION_HEADER& section)
{
    const std::uint32_t virtual_size = section.Misc.VirtualSize;
    const std::uint32_t raw_size = section.SizeOfRawData;
    if (virtual_size == 0) {
        return raw_size;
    }
    if (raw_size == 0) {
        return virtual_size;
    }
    return virtual_size < raw_size ? virtual_size : raw_size;
}

void emit_xor_decrypt_loop(
    stub_emit::c_stub_emitter& emitter,
    const arch_utils::arch_regs& regs,
    std::uint32_t section_rva,
    std::uint32_t size,
    std::uint8_t key
)
{
    // image base — preferred ImageBase breaks under ASLR
    emitter.load_image_base(regs.base);
    emitter.add(regs.base, section_rva);
    emitter.mov(regs.counter, size);
    emitter.mov(regs.key, key);

    const stub_emit::Label loop_start = emitter.new_label();
    const stub_emit::Label loop_end = emitter.new_label();

    emitter.bind(loop_start);
    emitter.cmp(regs.counter, 0);
    emitter.jz(loop_end);
    emitter.xor_byte_ptr(regs.base, stub_emit::Reg(emit::rx::rax));
    emitter.inc(regs.base);
    emitter.dec(regs.counter);
    emitter.jmp(loop_start);
    emitter.bind(loop_end);
}


void xor_region_by_va(
    pe_raw::PeView& view,
    std::uint64_t start_va,
    std::uint64_t end_va,
    std::uint8_t key
) {
    if (start_va >= end_va) {
        return;
    }

    const std::uint32_t start_rva = pe_raw::va_to_rva(view, start_va);
    const std::uint32_t end_rva = pe_raw::va_to_rva(view, end_va);
    const std::uint32_t size = end_rva - start_rva;

    const auto* section = pe_raw::section_for_rva(view, start_rva);
    if (!section) {
        throw pe_raw::PeError("xor region start is not inside any section");
    }

    const std::uint32_t section_end_rva = section->VirtualAddress +
        max_u32(section->Misc.VirtualSize, section->SizeOfRawData);

    if (end_rva > section_end_rva) {
        throw pe_raw::PeError("xor region crosses section boundary");
    }

    const std::size_t file_off = pe_raw::rva_to_file_off(*section, start_rva);
    if (file_off + size > view.bytes.size()) {
        throw pe_raw::PeError("xor region exceeds file bounds");
    }

    for (std::uint32_t i = 0; i < size; ++i) {
        view.bytes[file_off + i] ^= key;
    }
}

void xor_section_by_name(
    pe_raw::PeView& view,
    const char* section_name,
    std::uint8_t key
) {
    const auto* section = pe_raw::section_by_name(view, section_name);
    if (!section) {
        throw pe_raw::PeError(std::string("section not found: ") + section_name);
    }

    const std::size_t file_off = section->PointerToRawData;
    const std::uint32_t size = section_payload_size(*section);

    if (file_off + size > view.bytes.size()) {
        throw pe_raw::PeError("section raw data exceeds file bounds");
    }

    for (std::uint32_t i = 0; i < size; ++i) {
        view.bytes[file_off + i] ^= key;
    }
}

void encrypt_function_range(c_core& core, const XorTarget& target)
{
    if (!core.obf_func_pack || target.func_start >= target.func_end) {
        return;
    }

    xor_region_by_va(
        core.get_pe_view(),
        target.func_start,
        target.func_end,
        target.xor_key
    );
}

void emit_function_decrypt_stub(c_core& core, const XorTarget& target)
{
    if (!core.obf_func_pack) {
        return;
    }

    auto& emitter = core.get_emitter();
    const arch_utils::arch_regs regs = core.get_arch_regs();
    auto& pe_view = core.get_pe_view();

    const std::uint32_t start_rva = pe_raw::va_to_rva(pe_view, target.func_start);
    const std::uint32_t size = static_cast<std::uint32_t>(target.func_end - target.func_start);

    emit_xor_decrypt_loop(emitter, regs, start_rva, size, target.xor_key);
    print_info("Stub for func decryption has been inserted\n");
}

void encrypt_section(c_core& core, const std::string& section_name)
{
    if (!core.obf_xor_sections) {
        return;
    }

    auto& pe_view = core.get_pe_view();
    const auto* section = pe_raw::section_by_name(pe_view, section_name.c_str());
    if (!section) {
        print_warning("Section %s not found, skip encryption\n", section_name.c_str());
        return;
    }

    const std::uint32_t section_rva = section->VirtualAddress;
    const std::uint32_t payload_size = section_payload_size(*section);
    if (payload_size == 0) {
        print_warning("Section %s is empty, skip encryption\n", section_name.c_str());
        return;
    }

    pe_raw::set_section_characteristics(
        pe_view,
        section_name.c_str(),
        section->Characteristics | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE
    );

    // Encrypting .reloc before the loader runs
    if (section_name == ".reloc") {
        pe_raw::clear_basereloc_directory(pe_view);
        pe_raw::clear_aslr(pe_view);
        print_info("ASLR/basereloc disabled because .reloc is encrypted\n");
    }

    const std::uint8_t xor_key = static_cast<std::uint8_t>(random_value(1, 255));
    xor_section_by_name(pe_view, section_name.c_str(), xor_key);
    print_info("Section %s has been encrypted with key 0x%02x\n", section_name.c_str(), xor_key);

    emit_xor_decrypt_loop(
        core.get_emitter(),
        core.get_arch_regs(),
        section_rva,
        payload_size,
        xor_key
    );
    print_info("Stub for %s section decryption has been inserted\n", section_name.c_str());
}