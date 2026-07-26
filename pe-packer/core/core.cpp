#include "core.hpp"

#include "../handler/handler.hpp"
#include "../pack/pipeline.hpp"
#include "../pe_raw/pe_view.hpp"
#include "../utils/utils.hpp"

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

    m_emitter = std::make_unique<stub_emit::c_stub_emitter>(m_peView.is64);

    if (m_peView.is64) {
        print_info("Detected x64 architecture\n");
    } else {
        print_info("Detected x86 architecture\n");
    }

    print_custom("emit", "Stub emitter initialized\n");

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
        const std::uint8_t key = static_cast<std::uint8_t>(random_value(0x00, 0xFF));

        if (!addr_start.empty() && !addr_end.empty()) {
            try {
                const std::uint64_t start_va = std::stoull(addr_start, nullptr, 16);
                const std::uint64_t end_va = std::stoull(addr_end, nullptr, 16);
                if (start_va >= end_va) {
                    print_warning("Argument -fpack: start address must be less than end address\n");
                } else {
                    print_info(
                        "Packing functions from 0x%llx to 0x%llx enabled\n",
                        static_cast<unsigned long long>(start_va),
                        static_cast<unsigned long long>(end_va)
                    );
                    obf_func_pack = true;
                    obf_xor_targets.push_back({ start_va, end_va, key });
                }
            } catch (const std::exception&) {
                print_warning(
                    "Argument -fpack: invalid hex address [%s] [%s]\n",
                    addr_start.c_str(),
                    addr_end.c_str()
                );
            }
        } else {
            print_warning("Argument -fpack must be followed by two addresses [START_ADDR] [END_ADDR]\n");
        }
    }
}

void c_core::process()
{
    pack_run(*this);
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
