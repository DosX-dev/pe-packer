#pragma once

#include <Windows.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../emit/stub_emitter.hpp"
#include "../handler/handler.hpp"
#include "../pack/types.hpp"
#include "../pe_raw/pe_view.hpp"
#include "../utils/arguments.hpp"
#include "arch_utils.hpp"
#include "mutation_profile.hpp"

class c_core {
public:
    c_core(std::string input_file, std::string output_file, std::uint32_t obfuscation_level);

    stub_emit::c_stub_emitter& get_emitter() {
        return *m_emitter;
    }

    pe_raw::PeView& get_pe_view() {
        return m_peView;
    }

    bool is_x64() const {
        return m_peView.is64;
    }

    bool is_x86() const {
        return !m_peView.is64;
    }

    std::uint64_t get_image_base() const {
        return m_peView.image_base;
    }

    std::uint32_t obfuscation_level() const {
        return m_level;
    }

    const mutation_profile::Profile& obfuscation_profile() const {
        return m_profile;
    }

    int random_in_profile_range(int min_value, int max_value) const;

    using xor_target_t = XorTarget;

    std::vector<xor_target_t> obf_xor_targets;

    void process();

    void simple_jump_obfuscation();
    void call_obfuscation();
    void generate_junk_code();
    void push_pop_junk();
    void big_conditions_junk();
    void obfuscation_process();
    void emit_mba_block();

    stub_emit::Reg get_rand_reg();
    stub_emit::Reg get_rand_lower_reg();

    arch_utils::arch_regs get_arch_regs() const {
        return arch_utils::get_arch_regs(is_x64());
    }

    bool obf_call_oep = false;
    bool obf_anti_disasm = false;
    bool obf_mba = false;
    bool obf_xor_sections = false;
    bool obf_fake_instr = false;
    bool obf_func_pack = false;

    std::uint32_t             m_level{1};
    mutation_profile::Profile m_profile{};
    std::string               m_input;
    std::string               m_output;

private:
    std::unique_ptr<stub_emit::c_stub_emitter> m_emitter;
    pe_raw::PeView                             m_peView;
};

extern c_core* mutator;
