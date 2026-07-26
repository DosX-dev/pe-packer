#pragma once

#include "x64_emit.h"
#include "x86_emit.h"

#include <cstdint>
#include <vector>

namespace stub_emit {

    struct Reg {
        std::uint8_t id;

        constexpr Reg() : id(0) {}
        constexpr explicit Reg(std::uint8_t value) : id(value) {}
        constexpr operator std::uint8_t() const { return id; }
    };

    using Label = emit::X64Emitter::LabelId;

    namespace cc {
        constexpr std::uint8_t z = 0x4;
        constexpr std::uint8_t nz = 0x5;
        constexpr std::uint8_t b = 0x2;
        constexpr std::uint8_t be = 0x6;
        constexpr std::uint8_t l = 0xC;
        constexpr std::uint8_t le = 0xE;
        constexpr std::uint8_t nle = 0xF;
    }

    class c_stub_emitter {
    public:
        explicit c_stub_emitter(bool is64);

        void set_section_rva(std::uint32_t rva);

        Label new_label();
        void bind(Label label);
        std::size_t offset() const;
        const std::vector<std::uint8_t>& bytes() const;
        std::vector<std::uint8_t> take();

        void db(std::uint8_t value);
        void nop();

        void mov(Reg dst, std::uint64_t imm);
        void mov(Reg dst, Reg src);
        void push(Reg reg);
        void pop(Reg reg);

        void xor_(Reg dst, Reg src);
        void xor_(Reg dst, std::uint32_t imm);
        void xor_byte_ptr(Reg base, Reg value_reg8 = Reg(emit::rx::rax));

        void add(Reg dst, Reg src);
        void add(Reg dst, std::uint32_t imm);
        void sub(Reg dst, Reg src);
        void sub(Reg dst, std::uint32_t imm);
        void and_(Reg dst, Reg src);
        void and_(Reg dst, std::uint32_t imm);
        void or_(Reg dst, Reg src);
        void not_(Reg);

        void cmp(Reg dst, Reg src);
        void cmp(Reg dst, std::uint32_t imm);
        void neg(Reg dst);
        void inc(Reg dst);
        void dec(Reg dst);
        void imul(Reg dst, std::int32_t imm);
        void shr(Reg dst, std::uint8_t amount);
        void cpuid();

        void jz(Label label);
        void jnz(Label label);
        void je(Label label);
        void jg(Label label);
        void jb(Label label);
        void jbe(Label label);
        void jl(Label label);
        void jle(Label label);
        void jecxz(Label label);
        void jmp(Label label);
        void jmp(Reg dst);

        void call(Label label);
        void call(Reg dst);
        void call_rva(std::uint32_t oep_rva);
        void jmp_rva(std::uint32_t oep_rva);
        void ret();
        void load_image_base(Reg dst);

    private:
        bool is64_{ true };
        std::uint32_t section_rva_{ 0 };
        emit::X64Emitter x64_{};
        emit::X86Emitter x86_{};

        void emit_rex(bool w, Reg reg, Reg rm);
        void emit_modrm(std::uint8_t mod, std::uint8_t reg, std::uint8_t rm);
    };

}
