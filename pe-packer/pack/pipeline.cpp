#include "pipeline.hpp"

#include "encrypt.hpp"
#include "stub.hpp"

#include "../core/adasm.hpp"
#include "../core/core.hpp"
#include "../handler/handler.hpp"
#include "../utils/utils.hpp"

void pack_run(c_core& core)
{
    auto& emitter = core.get_emitter();
    auto& pe_view = core.get_pe_view();

    const std::uint32_t section_rva = pe_raw::peek_next_section_rva(pe_view);
    emitter.set_section_rva(section_rva);

    const std::size_t ep_offset = emitter.offset();
    const std::uint32_t idx_oep = random_value(0x1000, 0xFFFFFFFF);
    const std::uint32_t original_oep_rva = pe_view.entry_point_rva;

    const arch_utils::arch_regs regs = core.get_arch_regs();
    emitter.push(regs.base_ptr);
    emitter.mov(regs.base_ptr, regs.stack_ptr);

    // need to restore it after obfuscation
    const stub_emit::Reg save_rsi(stub_emit::Reg(6));
    const stub_emit::Reg save_rdi(stub_emit::Reg(7));
    emitter.push(regs.base);
    emitter.push(regs.counter);
    emitter.push(regs.temp2);
    emitter.push(save_rsi);
    emitter.push(save_rdi);

    std::uint64_t oep = pe_view.entry_point_rva;
    const std::uint32_t oepvl_xor_key = static_cast<std::uint32_t>(random_xor_key());

    c_adasm adasm_obj(core);

    if (core.obf_mba) {
        for (std::uint32_t i = 0; i < core.m_profile.mba_entry_passes; ++i) {
            core.emit_mba_block();
        }
    }

    core.obfuscation_process();

    if (core.obf_mba) {
        for (std::uint32_t i = 0; i < core.m_profile.mba_exit_passes; ++i) {
            core.emit_mba_block();
        }
    }

    if (core.obf_func_pack) {
        pe_raw::set_section_characteristics(
            pe_view,
            ".text",
            IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE
        );
        print_info("Section %s flags changed to RWX\n", ".text");

        for (const XorTarget& target : core.obf_xor_targets) {
            encrypt_function_range(core, target);
            emit_function_decrypt_stub(core, target);
        }
    }

    // Section decrypt must run before OEP transfer
    const std::vector<std::string> section_to_xor = { ".reloc" };
    for (const std::string& section_name : section_to_xor) {
        encrypt_section(core, section_name);
    }

    if (core.obf_anti_disasm) {
        adasm_obj.jmp_label_skip();
    }

    emitter.pop(save_rdi);
    emitter.pop(save_rsi);
    emitter.pop(regs.temp2);
    emitter.pop(regs.counter);
    emitter.pop(regs.base);
    emitter.pop(regs.base_ptr);

    // Full frame restore leaves RSP as at process entry (RSP % 16 == 8 on x64)
    if (core.obf_call_oep) {
        const arch_utils::oep_scratch_regs scratch = arch_utils::get_oep_scratch_regs(core.is_x64());

        switch (rand() % 2) {
        case 0:
            oep -= idx_oep;
            emitter.load_image_base(scratch.base);
            emitter.mov(scratch.tmp, oep);
            emitter.add(scratch.tmp, idx_oep);
            emitter.add(scratch.base, scratch.tmp);
            emitter.jmp(scratch.base);
            break;

        case 1:
            oep -= idx_oep;
            {
                const std::uint32_t xored = idx_oep ^ oepvl_xor_key;
                emitter.load_image_base(scratch.base);
                emitter.mov(scratch.idx, xored);
                emitter.xor_(scratch.idx, oepvl_xor_key);
                emitter.mov(scratch.tmp, oep);
                emitter.add(scratch.tmp, scratch.idx);
                emitter.add(scratch.base, scratch.tmp);
                emitter.jmp(scratch.base);
            }
            break;

        default:
            break;
        }

        if (core.obf_anti_disasm) {
            adasm_obj.jmp_label_skip();
        }
        if (core.obf_fake_instr) {
            const int fake_count = core.random_in_profile_range(
                static_cast<int>(core.m_profile.fake_instr_min),
                static_cast<int>(core.m_profile.fake_instr_max)
            );
            for (int i = 0; i < fake_count; ++i) {
                emitter.db(static_cast<std::uint8_t>(random_value(0x00, 0xFF)));
            }
        }
    } else {
        emitter.jmp_rva(pe_view.entry_point_rva);

        if (core.obf_fake_instr) {
            const int fake_count = core.random_in_profile_range(
                static_cast<int>(core.m_profile.fake_instr_min),
                static_cast<int>(core.m_profile.fake_instr_max)
            );
            for (int i = 0; i < fake_count; ++i) {
                emitter.db(static_cast<std::uint8_t>(random_value(0x00, 0xFF)));
            }
        }
    }

    PackOptions pack_options{};
    if (core.obf_call_oep) {
        pack_options.guard_cf_oep_rva = original_oep_rva;
    }

    const PackResult packed = inject_stub(
        pe_view,
        ".ptext",
        emitter.bytes(),
        static_cast<std::uint32_t>(ep_offset),
        pack_options
    );

    print_info(
        "Address of entry point 0x%llx\n",
        static_cast<unsigned long long>(packed.entry_point_va)
    );
    print_info(
        "New section characteristics 0x%x\n",
        IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE
    );

    pe_raw::write_file_bytes(core.m_output, pe_view.bytes);
    print_info("File successfully packed and saved in %s", core.m_output.c_str());
}
