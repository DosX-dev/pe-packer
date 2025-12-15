#pragma once
#include <cstdint>
#include <cstdlib>
#include "../pe_lib/pe_bliss.h"
#include "../asmjit/src/asmjit/asmjit.h"

namespace arch_utils {
    // detect binary is x64
    inline bool is_x64(pe_bliss::pe_base* peImage) {
        return peImage->get_pe_type() == pe_bliss::pe_type_64;
    }

    // detect binary is x32
    inline bool is_x86(pe_bliss::pe_base* peImage) {
        return peImage->get_pe_type() == pe_bliss::pe_type_32;
    }

    // get correct imagebase addr
    inline uintptr_t get_image_base(pe_bliss::pe_base* peImage) {
        if (is_x64(peImage)) {
            return static_cast<uintptr_t>(peImage->get_image_base_64());
        } else {
            return static_cast<uintptr_t>(peImage->get_image_base_32());
        }
    }

    struct arch_regs {
        asmjit::x86::Gp base;      // ecx/rcx
        asmjit::x86::Gp counter;   // ebx/rbx
        asmjit::x86::Gp key;       // al/rax (lower 8 bits)
        asmjit::x86::Gp temp1;     // eax/rax
        asmjit::x86::Gp temp2;     // edx/rdx
        asmjit::x86::Gp stack_ptr; // esp/rsp
        asmjit::x86::Gp base_ptr;  // ebp/rbp
    };

    // init args for both arch's
    inline arch_regs get_arch_regs(asmjit::x86::Assembler* assembler, bool is64bit) {
        arch_regs regs;
        
        if (is64bit) {
            regs.base = asmjit::x86::rcx;
            regs.counter = asmjit::x86::rbx;
            regs.key = asmjit::x86::al;  // 8-bit register
            regs.temp1 = asmjit::x86::rax;
            regs.temp2 = asmjit::x86::rdx;
            regs.stack_ptr = asmjit::x86::rsp;
            regs.base_ptr = asmjit::x86::rbp;
        } else {
            regs.base = asmjit::x86::ecx;
            regs.counter = asmjit::x86::ebx;
            regs.key = asmjit::x86::al;  // 8-bit register
            regs.temp1 = asmjit::x86::eax;
            regs.temp2 = asmjit::x86::edx;
            regs.stack_ptr = asmjit::x86::esp;
            regs.base_ptr = asmjit::x86::ebp;
        }
        
        return regs;
    }

    inline asmjit::x86::Gp get_random_gp_reg(asmjit::x86::Assembler* assembler, bool is64bit) {
        int reg_num = rand() % 6;
        
        if (is64bit) {
            switch (reg_num) {
                case 0: return asmjit::x86::rax;
                case 1: return asmjit::x86::rbx;
                case 2: return asmjit::x86::rcx;
                case 3: return asmjit::x86::rdx;
                case 4: return asmjit::x86::rsi;
                case 5: return asmjit::x86::rdi;
                default: return asmjit::x86::rax;
            }
        } else {
            switch (reg_num) {
                case 0: return asmjit::x86::eax;
                case 1: return asmjit::x86::ebx;
                case 2: return asmjit::x86::ecx;
                case 3: return asmjit::x86::edx;
                case 4: return asmjit::x86::esi;
                case 5: return asmjit::x86::edi;
                default: return asmjit::x86::eax;
            }
        }
    }

    inline asmjit::x86::Gp get_random_lower_reg(bool is64bit) {
        int reg_num = rand() % 6;
        
        if (is64bit) {
            switch (reg_num) {
                case 0: return asmjit::x86::ax;
                case 1: return asmjit::x86::bx;
                case 2: return asmjit::x86::cx;
                case 3: return asmjit::x86::dx;
                case 4: return asmjit::x86::si;
                case 5: return asmjit::x86::di;
                default: return asmjit::x86::ax;
            }
        } else {
            switch (reg_num) {
                case 0: return asmjit::x86::ax;
                case 1: return asmjit::x86::bx;
                case 2: return asmjit::x86::cx;
                case 3: return asmjit::x86::dx;
                case 4: return asmjit::x86::si;
                case 5: return asmjit::x86::di;
                default: return asmjit::x86::ax;
            }
        }
    }
}

