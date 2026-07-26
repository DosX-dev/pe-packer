#include "stub_emitter.hpp"

// just custom emitter instead of asmjit

namespace stub_emit {

    constexpr bool full_width = true;

    c_stub_emitter::c_stub_emitter(bool is64) : is64_(is64) {}

    void c_stub_emitter::set_section_rva(std::uint32_t rva) {
        section_rva_ = rva;
    }

    Label c_stub_emitter::new_label() {
        return is64_ ? x64_.new_label() : x86_.new_label();
    }

    void c_stub_emitter::bind(Label label) {
        if (is64_) {
            x64_.bind(label);
        }
        else {
            x86_.bind(label);
        }
    }

    std::size_t c_stub_emitter::offset() const {
        return is64_ ? x64_.size() : x86_.size();
    }

    const std::vector<std::uint8_t>& c_stub_emitter::bytes() const {
        return is64_ ? x64_.bytes() : x86_.bytes();
    }

    std::vector<std::uint8_t> c_stub_emitter::take() {
        return is64_ ? x64_.take() : x86_.take();
    }

    void c_stub_emitter::db(std::uint8_t value) {
        if (is64_) {
            x64_.u8(value);
        }
        else {
            x86_.u8(value);
        }
    }

    void c_stub_emitter::nop() {
        if (is64_) {
            x64_.nop();
        }
        else {
            x86_.nop();
        }
    }

    void c_stub_emitter::mov(Reg dst, std::uint64_t imm) {
        if (is64_) {
            x64_.mov_reg_imm64(dst, imm);
        }
        else {
            x86_.mov_reg_imm32(dst, static_cast<std::int32_t>(imm), full_width);
        }
    }

    void c_stub_emitter::mov(Reg dst, Reg src) {
        if (is64_) {
            x64_.mov_reg_reg(dst, src, full_width);
        }
        else {
            x86_.mov_reg_reg(dst, src, full_width);
        }
    }

    void c_stub_emitter::push(Reg reg) {
        if (is64_) {
            x64_.push_reg(reg);
        }
        else {
            x86_.push_reg(reg);
        }
    }

    void c_stub_emitter::pop(Reg reg) {
        if (is64_) {
            x64_.pop_reg(reg);
        }
        else {
            x86_.pop_reg(reg);
        }
    }

    void c_stub_emitter::xor_(Reg dst, Reg src) {
        if (is64_) {
            x64_.xor_reg_reg(dst, src, full_width);
        }
        else {
            x86_.xor_reg_reg(dst, src, full_width);
        }
    }

    void c_stub_emitter::xor_(Reg dst, std::uint32_t imm) {
        if (is64_) {
            x64_.xor_reg_imm32(dst, static_cast<std::int32_t>(imm), full_width);
        }
        else {
            x86_.xor_reg_imm32(dst, static_cast<std::int32_t>(imm), full_width);
        }
    }

    void c_stub_emitter::emit_rex(bool w, Reg reg, Reg rm) {
        if (is64_) {
            x64_.emit_rex(w, reg >= 8, false, rm >= 8);
        }
    }

    void c_stub_emitter::emit_modrm(std::uint8_t mod, std::uint8_t reg, std::uint8_t rm) {
        if (is64_) {
            x64_.emit_modrm(mod, reg, rm);
        }
        else {
            x86_.emit_modrm(mod, reg, rm);
        }
    }

    void c_stub_emitter::xor_byte_ptr(Reg base, Reg value_reg8) {
        emit_rex(false, value_reg8, base);
        if (is64_) {
            x64_.u8(0x30);
        }
        else {
            x86_.u8(0x30);
        }
        emit_modrm(0, value_reg8 & 7, base & 7);
    }

    void c_stub_emitter::add(Reg dst, Reg src) {
        if (is64_) {
            x64_.add_reg_reg(dst, src, full_width);
        }
        else {
            x86_.add_reg_reg(dst, src, full_width);
        }
    }

    void c_stub_emitter::add(Reg dst, std::uint32_t imm) {
        if (is64_ && imm >= 0x80000000u) {
            x64_.mov_reg_imm64(emit::rx::rax, imm);
            x64_.add_reg_reg(dst, emit::rx::rax, full_width);
            return;
        }
        if (is64_) {
            x64_.add_reg_imm32(dst, static_cast<std::int32_t>(imm), full_width);
        }
        else {
            x86_.add_reg_imm32(dst, static_cast<std::int32_t>(imm), full_width);
        }
    }

    void c_stub_emitter::sub(Reg dst, Reg src) {
        if (is64_) {
            x64_.sub_reg_reg(dst, src, full_width);
        }
        else {
            x86_.sub_reg_reg(dst, src, full_width);
        }
    }

    void c_stub_emitter::sub(Reg dst, std::uint32_t imm) {
        if (is64_) {
            x64_.sub_reg_imm32(dst, static_cast<std::int32_t>(imm), full_width);
        }
        else {
            x86_.sub_reg_imm32(dst, static_cast<std::int32_t>(imm), full_width);
        }
    }

    void c_stub_emitter::and_(Reg dst, Reg src) {
        if (is64_) {
            x64_.and_reg_reg(dst, src, full_width);
        }
        else {
            x86_.and_reg_reg(dst, src, full_width);
        }
    }

    void c_stub_emitter::and_(Reg dst, std::uint32_t imm) {
        if (is64_) {
            x64_.and_reg_imm32(dst, static_cast<std::int32_t>(imm), full_width);
        }
        else {
            x86_.and_reg_imm32(dst, static_cast<std::int32_t>(imm), full_width);
        }
    }

    void c_stub_emitter::or_(Reg dst, Reg src) {
        if (is64_) {
            x64_.or_reg_reg(dst, src, full_width);
        }
        else {
            x86_.or_reg_reg(dst, src, full_width);
        }
    }

    void c_stub_emitter::cmp(Reg dst, Reg src) {
        if (is64_) {
            x64_.cmp_reg_reg(dst, src, full_width);
        }
        else {
            x86_.cmp_reg_reg(dst, src, full_width);
        }
    }

    void c_stub_emitter::cmp(Reg dst, std::uint32_t imm) {
        if (is64_) {
            x64_.cmp_reg_imm32(dst, static_cast<std::int32_t>(imm), full_width);
        }
        else {
            x86_.cmp_reg_imm32(dst, static_cast<std::int32_t>(imm), full_width);
        }
    }

    void c_stub_emitter::neg(Reg dst) {
        if (is64_) {
            x64_.neg_reg(dst, full_width);
        }
        else {
            x86_.neg_reg(dst, full_width);
        }
    }

	void c_stub_emitter::not_(Reg dst) {
        if (is64_) {
            x64_.not_reg(dst, full_width);
        }
        else {
            x86_.not_reg(dst, full_width);
        }
    }

    void c_stub_emitter::inc(Reg dst) {
        if (is64_) {
            x64_.inc_reg(dst, full_width);
        }
        else {
            x86_.inc_reg(dst, full_width);
        }
    }

    void c_stub_emitter::dec(Reg dst) {
        if (is64_) {
            x64_.dec_reg(dst, full_width);
        }
        else {
            x86_.dec_reg(dst, full_width);
        }
    }

    void c_stub_emitter::imul(Reg dst, std::int32_t imm) {
        emit_rex(full_width, dst, dst);
        if (is64_) {
            x64_.u8(0x69);
        }
        else {
            x86_.u8(0x69);
        }
        emit_modrm(3, dst & 7, dst & 7);
        if (is64_) {
            x64_.u32(static_cast<std::uint32_t>(imm));
        }
        else {
            x86_.u32(static_cast<std::uint32_t>(imm));
        }
    }

    void c_stub_emitter::shr(Reg dst, std::uint8_t amount) {
        if (is64_) {
            x64_.shr_reg_imm8(dst, amount, full_width);
        }
        else {
            x86_.shr_reg_imm8(dst, amount, full_width);
        }
    }

    void c_stub_emitter::cpuid() {
        if (is64_) {
            x64_.bytes({ 0x0F, 0xA2 });
        }
        else {
            x86_.bytes({ 0x0F, 0xA2 });
        }
    }

    void c_stub_emitter::jz(Label label) {
        if (is64_) {
            x64_.jcc_label(cc::z, label);
        }
        else {
            x86_.jcc_label(cc::z, label);
        }
    }

    void c_stub_emitter::jnz(Label label) {
        if (is64_) {
            x64_.jcc_label(cc::nz, label);
        }
        else {
            x86_.jcc_label(cc::nz, label);
        }
    }

    void c_stub_emitter::je(Label label) {
        jz(label);
    }

    void c_stub_emitter::jg(Label label) {
        if (is64_) {
            x64_.jcc_label(cc::nle, label);
        }
        else {
            x86_.jcc_label(cc::nle, label);
        }
    }

    void c_stub_emitter::jb(Label label) {
        if (is64_) {
            x64_.jcc_label(cc::b, label);
        }
        else {
            x86_.jcc_label(cc::b, label);
        }
    }

    void c_stub_emitter::jbe(Label label) {
        if (is64_) {
            x64_.jcc_label(cc::be, label);
        }
        else {
            x86_.jcc_label(cc::be, label);
        }
    }

    void c_stub_emitter::jl(Label label) {
        if (is64_) {
            x64_.jcc_label(cc::l, label);
        }
        else {
            x86_.jcc_label(cc::l, label);
        }
    }

    void c_stub_emitter::jle(Label label) {
        if (is64_) {
            x64_.jcc_label(cc::le, label);
        }
        else {
            x86_.jcc_label(cc::le, label);
        }
    }

    void c_stub_emitter::jecxz(Label label) {
        if (is64_) {
            x64_.jcc_label(cc::z, label);
            return;
        }

        x86_.jmp_label(label);
    }

    void c_stub_emitter::jmp(Label label) {
        if (is64_) {
            x64_.jmp_label(label);
        }
        else {
            x86_.jmp_label(label);
        }
    }

    void c_stub_emitter::jmp(Reg dst) {
        if (is64_) {
            x64_.jmp_reg(dst);
        }
        else {
            x86_.jmp_reg(dst);
        }
    }

    void c_stub_emitter::call(Label label) {
        if (is64_) {
            x64_.call_label(label);
        }
        else {
            x86_.call_label(label);
        }
    }

    void c_stub_emitter::call(Reg dst) {
        if (is64_) {
            x64_.call_reg(dst);
        }
        else {
            x86_.call_reg(dst);
        }
    }

    void c_stub_emitter::call_rva(std::uint32_t oep_rva) {
        const std::int32_t rel32 = static_cast<std::int32_t>(
            static_cast<std::int64_t>(oep_rva) -
            static_cast<std::int64_t>(section_rva_ + offset() + 5)
            );

        if (is64_) {
            x64_.call_rel32(rel32);
        }
        else {
            x86_.call_rel32(rel32);
        }
    }

    void c_stub_emitter::jmp_rva(std::uint32_t oep_rva) {
        const std::int32_t rel32 = static_cast<std::int32_t>(
            static_cast<std::int64_t>(oep_rva) -
            static_cast<std::int64_t>(section_rva_ + offset() + 5)
            );

        if (is64_) {
            x64_.jmp_rel32(rel32);
        }
        else {
            x86_.jmp_rel32(rel32);
        }
    }

    void c_stub_emitter::ret() {
        if (is64_) {
            x64_.ret();
        }
        else {
            x86_.ret();
        }
    }

    void c_stub_emitter::load_image_base(Reg dst) {
        if (is64_) {
            x64_.bytes({ 0x65, 0x48, 0x8B, 0x04, 0x25, 0x60, 0x00, 0x00, 0x00 });
            x64_.bytes({ 0x48, 0x8B, 0x40, 0x10 });
        }
        else {
            x86_.bytes({ 0x64, 0xA1, 0x30, 0x00, 0x00, 0x00 });
            x86_.bytes({ 0x8B, 0x40, 0x08 });
        }

        if (dst.id != emit::rx::rax) {
            mov(dst, Reg(emit::rx::rax));
        }
    }

}
