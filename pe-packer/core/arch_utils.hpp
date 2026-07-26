#pragma once

#include <cstdint>
#include <cstdlib>
#include "../emit/stub_emitter.hpp"

namespace arch_utils {

    inline bool is_x64(bool pe_is64) {
        return pe_is64;
    }

    inline bool is_x86(bool pe_is64) {
        return !pe_is64;
    }

    struct arch_regs {

        stub_emit::Reg base;      // rcx / ecx
        stub_emit::Reg counter;   // rbx / ebx
        stub_emit::Reg key;       // rax (al used for xor)
        stub_emit::Reg temp1;     // rax / eax
        stub_emit::Reg temp2;     // rdx / edx
        stub_emit::Reg stack_ptr; // rsp / esp
        stub_emit::Reg base_ptr;  // rbp / ebp

    };

    inline arch_regs get_arch_regs(bool is64bit) {

        arch_regs regs{};

        regs.base = stub_emit::Reg(emit::rx::rcx);
        regs.counter = stub_emit::Reg(emit::rx::rbx);
        regs.key = stub_emit::Reg(emit::rx::rax);
        regs.temp1 = stub_emit::Reg(emit::rx::rax);
        regs.temp2 = stub_emit::Reg(emit::rx::rdx);
        regs.stack_ptr = stub_emit::Reg(emit::rx::rsp);
        regs.base_ptr = stub_emit::Reg(emit::rx::rbp);
        (void)is64bit;
        return regs;

    }

    struct oep_scratch_regs {
        stub_emit::Reg base;
        stub_emit::Reg tmp;
        stub_emit::Reg idx;
    };

    inline oep_scratch_regs get_oep_scratch_regs(bool is64bit) {
        oep_scratch_regs scratch{};
        if (is64bit) {
            // r10/r11/rbx keep rcx/rdx/r8/r9 for winmain/wmain
            scratch.base = stub_emit::Reg(emit::rx::r11);
            scratch.tmp = stub_emit::Reg(emit::rx::r10);
            scratch.idx = stub_emit::Reg(emit::rx::rbx);
        }
        else {
            // esi/edi/ebx x86 impl
            scratch.base = stub_emit::Reg(emit::rx::rsi);
            scratch.tmp = stub_emit::Reg(emit::rx::rdi);
            scratch.idx = stub_emit::Reg(emit::rx::rbx);
        }
        return scratch;
    }



    inline stub_emit::Reg get_random_gp_reg(bool is64bit) {
        if (is64bit) {
            switch (rand() % 10) {
            case 0: return stub_emit::Reg(emit::rx::rax);
            case 1: return stub_emit::Reg(emit::rx::rbx);
            case 2: return stub_emit::Reg(emit::rx::rcx);
            case 3: return stub_emit::Reg(emit::rx::rdx);
            case 4: return stub_emit::Reg(emit::rx::rsi);
            case 5: return stub_emit::Reg(emit::rx::rdi);
            case 6: return stub_emit::Reg(emit::rx::r8);
            case 7: return stub_emit::Reg(emit::rx::r9);
            case 8: return stub_emit::Reg(emit::rx::r10);
            case 9: return stub_emit::Reg(emit::rx::r11);
            default: return stub_emit::Reg(emit::rx::rax);
            }
        }

        switch (rand() % 6) {
        case 0: return stub_emit::Reg(emit::rx::rax);
        case 1: return stub_emit::Reg(emit::rx::rbx);
        case 2: return stub_emit::Reg(emit::rx::rcx);
        case 3: return stub_emit::Reg(emit::rx::rdx);
        case 4: return stub_emit::Reg(emit::rx::rsi);
        case 5: return stub_emit::Reg(emit::rx::rdi);
        default: return stub_emit::Reg(emit::rx::rax);
        }
    }

    inline stub_emit::Reg get_random_lower_reg(bool is64bit) {
        return get_random_gp_reg(is64bit);
    }

    // Only keep stack/frame intact
    inline bool reg_conflicts(stub_emit::Reg reg, const arch_regs& regs) {
        return reg == regs.base_ptr || reg == regs.stack_ptr;
    }

    inline stub_emit::Reg pick_random_reg(const arch_regs& regs, bool is64bit, stub_emit::Reg exclude = stub_emit::Reg(0xFF)) {
        stub_emit::Reg reg = get_random_gp_reg(is64bit);
        int guard = 0;
        while ((reg_conflicts(reg, regs) || reg == exclude) && guard++ < 32) {
            reg = get_random_gp_reg(is64bit);
        }
        return reg;
    }
}

