#include "core.hpp"

#include "../utils/utils.hpp"
#include "adasm.hpp"
#include "mba.hpp"

c_core::c_core(std::string input_file, std::string output_file, std::uint32_t obfuscation_level)
{
    m_input = std::move(input_file);
    m_output = std::move(output_file);
    m_level = mutation_profile::clamp_level(obfuscation_level);
    m_profile = mutation_profile::profile_for_level(m_level);

    const pe_raw::ParseFileResult parsed = pe_raw::parse_pe_file(m_input);
    if (!parsed.ok()) {
        throw std::runtime_error(parsed.error_message);
    }
    m_peView = std::move(*parsed.view);

    if (pe_raw::has_clr_directory(m_peView)) {
        throw std::runtime_error(".NET binaries are not supported");
    }

    // init emmiter
    m_emitter = std::make_unique<stub_emit::c_stub_emitter>(m_peView.is64);

    if (m_peView.is64) {
        print_info("Detected x64 architecture\n");
    } else {
        print_info("Detected x86 architecture\n");
    }

    print_custom("emit", "Stub emitter initialized\n");

    xor_target_t xor_target{};

    if (arguments::has("-noaslr")) {
        if (m_peView.dll_characteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE) {
            pe_raw::clear_aslr(m_peView);
            print_info("ASLR flag has been removed\n");
        } else {
            print_warning("ASLR flag not find yet\n");
        }
    }

    if (arguments::has("-oep_call")) {
        print_info("OEP call obfuscation is enable\n");
        obf_call_oep = true;
    }

    if (arguments::has("-adasm")) {
        print_info("Anti-disassembly obfuscation is enabled\n");
        obf_anti_disasm = true;
    }

    if (arguments::has("-mba")) {
        print_info("Mixed Boolean Arithmetic obfuscation is enabled\n");
        obf_mba = true;
    }

    if (arguments::has("-senc")) {
        print_info("Sections encryption is enabled\n");
        obf_xor_sections = true;
    }

    if (arguments::has("-finstr")) {
        print_info("Fake instructions is enabled\n");
        obf_fake_instr = true;
    }

    if (arguments::has("-fpack")) {
        const std::string addr_start = arguments::get_after("-fpack", 0);
        const std::string addr_end = arguments::get_after("-fpack", 1);
        const std::uint8_t key = static_cast<std::uint8_t>(random_value(0x10, 0xFF));

        if (!addr_start.empty() && !addr_end.empty()) {
            print_info("Packing functions from %s to %s enabled\n", addr_start.c_str(), addr_end.c_str());
            obf_func_pack = true;
            xor_target.func_start = std::stoul(addr_start, nullptr, 16);
            xor_target.func_end = static_cast<std::uint32_t>(std::stoul(addr_end, nullptr, 16));
            obf_xor_targets.push_back({ xor_target.func_start, xor_target.func_end, key });
        } else {
            print_warning("Argument -fpack must be followed by two addresses [START_ADDR] [END_ADDR]\n");
        }
    }
}

void c_core::xor_function_range(xor_target_t xor_target)
{
    if (!obf_func_pack || xor_target.func_start >= xor_target.func_end) {
        return;
    }

    // xor by va range
    pe_raw::xor_region_by_va(
        m_peView,
        xor_target.func_start,
        xor_target.func_end,
        xor_target.xor_key
    );
}

void c_core::insert_runtime_xor_stub(xor_target_t xor_target)
{
    if (!obf_func_pack) {
        return;
    }

    auto& emitter = get_emitter();
    const arch_utils::arch_regs regs = get_arch_regs();

    emitter.mov(regs.base, xor_target.func_start);
    emitter.mov(regs.counter, xor_target.func_end - xor_target.func_start);
    emitter.mov(regs.key, xor_target.xor_key);

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

    print_info("Stub for func decryption has been inserted\n");
}

void c_core::xor_sections(const std::string sec_to_xor)
{
    if (!obf_xor_sections) {
        return;
    }

    const std::uint8_t reloc_xor_key = static_cast<std::uint8_t>(random_value(1, 255));
    pe_raw::xor_section_by_name(m_peView, sec_to_xor.c_str(), reloc_xor_key);
    print_info("Section %s has been encrypted with key 0x%02x\n", sec_to_xor.c_str(), reloc_xor_key);

    const auto* section = pe_raw::section_by_name(m_peView, sec_to_xor.c_str());
    if (!section) {
        return;
    }

    const std::uint64_t reloc_va = get_image_base() + section->VirtualAddress;
    const std::uint32_t reloc_size = section->SizeOfRawData;

    auto& emitter = get_emitter();
    const arch_utils::arch_regs regs = get_arch_regs();

    emitter.mov(regs.base, reloc_va);
    emitter.mov(regs.counter, reloc_size);
    emitter.mov(regs.key, reloc_xor_key);

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

    print_info("Stub for %s section decryption has been inserted\n", sec_to_xor.c_str());
}

void c_core::process()
{
    auto& emitter = get_emitter();
    const std::uint32_t section_rva = pe_raw::peek_next_section_rva(m_peView);
    emitter.set_section_rva(section_rva);

    const std::size_t ep_offset = emitter.offset();
    const std::uint32_t idx_oep = random_value(0x1000, 0xFFFFFFFF);
    const std::uint32_t original_oep_rva = m_peView.entry_point_rva;

    const arch_utils::arch_regs regs = get_arch_regs();
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

    std::uint64_t oep = m_peView.entry_point_rva;
    const std::uint32_t oepvl_xor_key = (uint32_t)random_xor_key();

    c_adasm adasm_obj(*this);

    if (obf_mba) {
        for (std::uint32_t i = 0; i < m_profile.mba_entry_passes; ++i) {
            emit_mba_block();
        }
    }

    obfuscation_process();

    if (obf_mba) {
        for (std::uint32_t i = 0; i < m_profile.mba_exit_passes; ++i) {
            emit_mba_block();
        }
    }

    if (obf_func_pack) {
        pe_raw::set_section_characteristics(
            m_peView,
            ".text",
            IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE
        );
        print_info("Section %s flags changed to RWX\n", ".text");

        for (const xor_target_t& target : obf_xor_targets) {
            xor_function_range(target);
            insert_runtime_xor_stub(target);
        }
    }

    if (obf_anti_disasm) {
        adasm_obj.jmp_label_skip();
    }

    emitter.pop(save_rdi);
    emitter.pop(save_rsi);
    emitter.pop(regs.temp2);
    emitter.pop(regs.counter);
    emitter.pop(regs.base);

    if (obf_call_oep) {
        const arch_utils::oep_scratch_regs scratch = arch_utils::get_oep_scratch_regs(is_x64());

        switch (rand() % 2) {
        case 0:
            oep -= idx_oep;
            emitter.load_image_base(scratch.base);
            emitter.mov(scratch.tmp, oep);
            emitter.add(scratch.tmp, idx_oep);
            emitter.add(scratch.base, scratch.tmp);
            emitter.call(scratch.base);
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
                emitter.call(scratch.base);
            }
            break;

        default:
            break;
        }

        if (obf_anti_disasm) {
            adasm_obj.jmp_label_skip();
        }
        if (obf_fake_instr) {
            const int fake_count = random_in_profile_range(
                static_cast<int>(m_profile.fake_instr_min),
                static_cast<int>(m_profile.fake_instr_max)
            );
            for (int i = 0; i < fake_count; ++i) {
                emitter.db(static_cast<std::uint8_t>(random_value(0x10, 0xFF)));
            }
        }
    } else {
        emitter.call_rva(m_peView.entry_point_rva);

        if (obf_fake_instr) {
            const int fake_count = random_in_profile_range(
                static_cast<int>(m_profile.fake_instr_min),
                static_cast<int>(m_profile.fake_instr_max)
            );
            for (int i = 0; i < fake_count; ++i) {
                emitter.db(static_cast<std::uint8_t>(random_value(0x10, 0xFF)));
            }
        }
    }

    const std::vector<std::string> section_to_xor = { ".reloc" };
    for (const std::string& section_name : section_to_xor) {
        xor_sections(section_name);
    }

    pe_raw::PackOptions pack_options{};
    if (obf_call_oep) {
        pack_options.guard_cf_oep_rva = original_oep_rva;
    }

    const pe_raw::PackResult packed = pe_raw::packer_stub(
        m_peView,
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

    pe_raw::write_file_bytes(m_output, m_peView.bytes);
    print_info("File successfully packed and saved in %s", m_output.c_str());
}

void c_core::simple_jump_obfuscation()
{
    auto& emitter = get_emitter();

    const int first_value = random_value(0x10, 0x100);
    const int second_value = random_value(0x10, 0x100);
    int third_value = 0;

    if (first_value < second_value) {
        third_value = second_value - first_value;
    }
    else if (first_value > second_value) {
        third_value = first_value - second_value;
    }

    const stub_emit::Label label = emitter.new_label();
    const stub_emit::Reg rand_reg = get_rand_reg();
    const arch_utils::arch_regs regs = get_arch_regs();

    emitter.xor_(rand_reg, static_cast<std::uint32_t>(random_value(0x10, 0x100)));
    emitter.mov(regs.temp1, static_cast<std::uint32_t>(first_value));
    emitter.mov(regs.counter, static_cast<std::uint32_t>(second_value));
    emitter.add(regs.temp1, static_cast<std::uint32_t>(third_value));
    emitter.cmp(regs.temp1, regs.counter);

    switch (rand() % 4) {
    case 0: emitter.jz(label); break;
    case 1: emitter.jnz(label); break;
    case 2:
        if (is_x86()) {
            emitter.jecxz(label);
        }
        else {
            emitter.jz(label);
        }
        break;
    case 3: emitter.jg(label); break;
    default: break;
    }

    const int junk_bytes = random_in_profile_range(
        static_cast<int>(m_profile.jump_junk_min),
        static_cast<int>(m_profile.jump_junk_max)
    );
    for (int j = 0; j < junk_bytes; ++j) {
        generate_junk_code();
    }

    emitter.bind(label);
    generate_junk_code();
}

void c_core::call_obfuscation()
{
    auto& emitter = get_emitter();
    const arch_utils::arch_regs regs = get_arch_regs();

    if (m_profile.call_depth == 0) {
        generate_junk_code();
        return;
    }

    const stub_emit::Label call_label = emitter.new_label();
    emitter.push(regs.base_ptr);
    emitter.mov(regs.base_ptr, regs.stack_ptr);

    for (std::uint32_t i = 0; i < m_profile.call_depth; ++i) {
        generate_junk_code();
    }

    emitter.call(call_label);

    for (std::uint32_t i = 0; i < m_profile.call_depth; ++i) {
        generate_junk_code();
    }

    emitter.bind(call_label);
}

void c_core::generate_junk_code()
{
    if (obf_mba && static_cast<std::uint32_t>(rand() % 100) < m_profile.mba_junk_weight_percent) {
        emit_mba_block();
        return;
    }

    switch (rand() % 3) {
    case 0: push_pop_junk(); break;
    case 1: big_conditions_junk(); break;
    default: get_emitter().nop(); break;
    }
}

void c_core::push_pop_junk()
{
    auto& emitter = get_emitter();

    const bool use_reg = rand() % 2;
    const stub_emit::Reg dst = get_rand_reg();

    emitter.push(get_rand_reg());

    if (use_reg) {
        const stub_emit::Reg src = get_rand_reg();
        switch (rand() % 3) {
        case 0: emitter.add(dst, src); break;
        case 1: emitter.imul(dst, rand() % 100); break;
        case 2: emitter.sub(dst, src); break;
        default: emitter.nop(); break;
        }
    }
    else {
        switch (rand() % 3) {
        case 0: emitter.add(dst, static_cast<std::uint32_t>(rand() % 100)); break;
        case 1: emitter.imul(dst, rand() % 100); break;
        case 2: emitter.sub(dst, static_cast<std::uint32_t>(rand() % 100)); break;
        default: emitter.nop(); break;
        }
    }

    emitter.pop(get_rand_reg());

    for (std::uint32_t round = 0; round < m_profile.push_pop_rounds; ++round) {
    switch (rand() % 3) {
    case 0:
        emitter.imul(get_rand_reg(), rand() % 100);
        emitter.imul(get_rand_reg(), rand() % 100);
        emitter.add(get_rand_reg(), static_cast<std::uint32_t>(rand() % 100));
        emitter.cpuid();
        emitter.nop();
        emitter.cpuid();
        emitter.push(get_rand_reg());
        emitter.pop(get_rand_reg());
        break;
    case 1:
        emitter.imul(get_rand_reg(), rand() % 100);
        emitter.add(get_rand_reg(), static_cast<std::uint32_t>(rand() % 100));
        emitter.push(get_rand_reg());
        emitter.add(get_rand_reg(), static_cast<std::uint32_t>(rand() % 100));
        emitter.cpuid();
        emitter.nop();
        emitter.nop();
        emitter.pop(get_rand_reg());
        break;
    case 2:
        emitter.sub(get_rand_reg(), static_cast<std::uint32_t>(rand() % 100));
        emitter.add(get_rand_reg(), static_cast<std::uint32_t>(rand() % 100));
        emitter.add(get_rand_reg(), static_cast<std::uint32_t>(rand() % 100));
        emitter.push(get_rand_reg());
        emitter.nop();
        emitter.nop();
        emitter.cpuid();
        emitter.pop(get_rand_reg());
        break;
    default:
        emitter.nop();
        break;

    }
    }

}

void c_core::big_conditions_junk()
{
    auto& emitter = get_emitter();

    const stub_emit::Reg reg1 = get_rand_reg();
    const stub_emit::Reg reg2 = get_rand_reg();
    const int actions_count = random_in_profile_range(
        static_cast<int>(m_profile.condition_actions_min),
        static_cast<int>(m_profile.condition_actions_max)
    );

    for (int j = 0; j < actions_count; ++j) {
        switch (rand() % 5) {
        case 0: emitter.xor_(reg1, reg2); break;
        case 1: emitter.add(reg1, static_cast<std::uint32_t>(rand() % 10)); break;
        case 2: emitter.imul(reg2, rand() % 100); break;
        case 3: emitter.sub(reg1, static_cast<std::uint32_t>(rand() % 100)); break;
        case 4: emitter.mov(reg1, reg2); break;
        default: break;
        }
    }

    emitter.cmp(reg1, reg2);
    const stub_emit::Label cmp_label = emitter.new_label();
    switch (rand() % 7) {
    case 0: emitter.jmp(cmp_label); break;
    case 1: emitter.jz(cmp_label); break;
    case 2: emitter.jnz(cmp_label); break;
    case 3: emitter.jb(cmp_label); break;
    case 4: emitter.jbe(cmp_label); break;
    case 5: emitter.jl(cmp_label); break;
    case 6: emitter.jle(cmp_label); break;
    default: emitter.jmp(cmp_label); break;
    }

    emitter.bind(cmp_label);
    push_pop_junk();

}

stub_emit::Reg c_core::get_rand_reg()
{
    return arch_utils::get_random_gp_reg(is_x64());
}

stub_emit::Reg c_core::get_rand_lower_reg()
{
    return arch_utils::get_random_lower_reg(is_x64());
}

int c_core::random_in_profile_range(int min_value, int max_value) const
{
    if (min_value >= max_value) {
        return min_value;
    }
    return random_value(min_value, max_value);
}

void c_core::emit_mba_block()
{
    c_mba mba_obj(*this);
    c_mba::options mba_opt{};
    mba_opt.mba_factor = static_cast<int>(m_profile.mba_inner_ops);
    mba_obj.mba_code(mba_opt);
}

void c_core::obfuscation_process()
{
    c_adasm adasm_obj(*this);

    print_info(
        "Advanced values: level %u, passes %u, MBA weight %u%%, entry MBA %u, call depth %u\n",
        m_profile.level,
        m_profile.obfuscation_passes,
        m_profile.mba_weight_percent,
        m_profile.mba_entry_passes,
        m_profile.call_depth
    );

    for (std::uint32_t i = 0; i < m_profile.obfuscation_passes; ++i) {
        if (obf_anti_disasm) {
            adasm_obj.jmp_label_skip();
        }

        const int roll = rand() % 100;
        if (obf_mba && static_cast<std::uint32_t>(roll) < m_profile.mba_weight_percent) {
            emit_mba_block();
        }
        else {
            switch (rand() % 2) {
            case 0: simple_jump_obfuscation(); break;
            case 1: call_obfuscation(); break;
            default: break;
            }
        }
    }
}
