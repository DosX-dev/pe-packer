#include "core.hpp"

#include "adasm.hpp"
#include "mba.hpp"
#include "../handler/handler.hpp"
#include "../utils/utils.hpp"

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

    const stub_emit::Label callee = emitter.new_label();
    const stub_emit::Label done = emitter.new_label();

    emitter.push(regs.base_ptr);
    emitter.mov(regs.base_ptr, regs.stack_ptr);

    for (std::uint32_t i = 0; i < m_profile.call_depth; ++i) {
        generate_junk_code();
    }

    emitter.call(callee);

    for (std::uint32_t i = 0; i < m_profile.call_depth; ++i) {
        generate_junk_code();
    }

    emitter.pop(regs.base_ptr);
    emitter.jmp(done);

    emitter.bind(callee);
    for (std::uint32_t i = 0; i < m_profile.call_depth; ++i) {
        push_pop_junk();
    }
    emitter.ret();

    emitter.bind(done);
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

    const auto emit_alu_junk = [this, &emitter]() {
        const stub_emit::Reg a = get_rand_reg();
        const stub_emit::Reg b = get_rand_reg();
        switch (rand() % 12) {
        case 0:  emitter.add(a, b); break;
        case 1:  emitter.sub(a, b); break;
        case 2:  emitter.xor_(a, b); break;
        case 3:  emitter.and_(a, b); break;
        case 4:  emitter.or_(a, b); break;
        case 5:  emitter.add(a, static_cast<std::uint32_t>(random_value(1, 0xFF))); break;
        case 6:  emitter.sub(a, static_cast<std::uint32_t>(random_value(1, 0xFF))); break;
        case 7:  emitter.xor_(a, static_cast<std::uint32_t>(random_value(1, 0xFF))); break;
        case 8:  emitter.imul(a, static_cast<std::int32_t>(random_value(2, 17))); break;
        case 9:  emitter.shr(a, static_cast<std::uint8_t>(random_value(1, 7))); break;
        case 10: emitter.neg(a); break;
        default: emitter.not_(a); break;
        }
    };

    const stub_emit::Reg scratch = get_rand_reg();
    emitter.push(scratch);
    emit_alu_junk();
    if (rand() % 2) {
        emit_alu_junk();
    }
    emitter.pop(scratch);

    for (std::uint32_t round = 0; round < m_profile.push_pop_rounds; ++round) {
        const stub_emit::Reg round_reg = get_rand_reg();
        emitter.push(round_reg);

        switch (rand() % 4) {
        case 0:
            emit_alu_junk();
            emitter.inc(get_rand_reg());
            emitter.dec(get_rand_reg());
            break;
        case 1:
            emit_alu_junk();
            emitter.cmp(get_rand_reg(), get_rand_reg());
            emit_alu_junk();
            break;
        case 2:
            emitter.mov(get_rand_reg(), get_rand_reg());
            emit_alu_junk();
            emitter.xor_(get_rand_reg(), static_cast<std::uint32_t>(0));
            break;
        default:
            emit_alu_junk();
            emitter.nop();
            emit_alu_junk();
            break;
        }

        if (rand() % 20 == 0) {
            emitter.cpuid();
        }

        emitter.pop(round_reg);
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
    const stub_emit::Label cont = emitter.new_label();
    switch (rand() % 7) {
    case 0: emitter.jmp(cont); break;
    case 1: emitter.jz(cont); break;
    case 2: emitter.jnz(cont); break;
    case 3: emitter.jb(cont); break;
    case 4: emitter.jbe(cont); break;
    case 5: emitter.jl(cont); break;
    case 6: emitter.jle(cont); break;
    default: emitter.jmp(cont); break;
    }

    const int dead_ops = random_in_profile_range(
        static_cast<int>(m_profile.condition_actions_min),
        static_cast<int>(m_profile.condition_actions_max)
    );
    for (int i = 0; i < dead_ops; ++i) {
        if (rand() % 2) {
            push_pop_junk();
        } else {
            emitter.nop();
        }
    }

    emitter.bind(cont);
    push_pop_junk();
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
