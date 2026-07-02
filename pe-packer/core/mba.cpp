#include "mba.hpp"

#include "../utils/utils.hpp"
#include "arch_utils.hpp"

c_mba::c_mba(c_core& g_core) : m_core(g_core) {}

void c_mba::gen_math_operations() {
    auto& emitter = m_core.get_emitter();
    switch (rand() % 4) {
    case 0:
        emitter.shr(m_core.get_rand_reg(), static_cast<std::uint8_t>(random_value(1, 100)));
        break;
    case 1:
        emitter.and_(m_core.get_rand_reg(), static_cast<std::uint32_t>(random_value(1, 100)));
        break;
    case 2:
        emitter.xor_(m_core.get_rand_reg(), static_cast<std::uint32_t>(random_value(1, 100)));
        break;
    case 3:
        emitter.add(m_core.get_rand_reg(), static_cast<std::uint32_t>(random_value(1, 100)));
        break;
    default:
        break;
    }
}

void c_mba::mba_code(c_mba::options opt) {

    auto& emitter = m_core.get_emitter();
    const arch_utils::arch_regs regs = m_core.get_arch_regs();
    const bool is64 = m_core.is_x64();

    switch (rand() % 4) {
    case 0: {
        // (x | y) - (x & y)  ==  x ^ y
        const stub_emit::Label new_label = emitter.new_label();
        const stub_emit::Reg temp_reg = arch_utils::pick_random_reg(regs, is64);
        emitter.mov(temp_reg, 1);
        emitter.cmp(temp_reg, 0);
        emitter.je(new_label);

        const stub_emit::Reg reg_x = arch_utils::pick_random_reg(regs, is64);
        const stub_emit::Reg reg_y = arch_utils::pick_random_reg(regs, is64, reg_x);

        emitter.mov(regs.temp1, reg_x);
        emitter.mov(regs.counter, reg_y);
        emitter.or_(regs.temp1, regs.counter);
        emitter.push(regs.temp1);
        emitter.mov(regs.temp1, reg_x);
        emitter.and_(regs.temp1, regs.counter);
        emitter.pop(regs.base);
        emitter.sub(regs.base, regs.temp1);
        emitter.mov(regs.temp1, regs.base);
        emitter.push(regs.temp1);
        emitter.mov(regs.counter, regs.temp1);
        emitter.xor_(regs.counter, reg_x);
        emitter.pop(regs.temp1);
        emitter.bind(new_label);
        emitter.push(regs.base_ptr);
        emitter.mov(regs.base_ptr, regs.stack_ptr);
        for (int i = 0; i < opt.mba_factor; ++i) {
            gen_math_operations();
        }
        emitter.pop(regs.base_ptr);
        break;
    }

    case 1: {
        // (x & y) + (x | y)  ==  x + y
        const stub_emit::Label new_label = emitter.new_label();
        const stub_emit::Reg temp_reg = arch_utils::pick_random_reg(regs, is64);
        emitter.mov(temp_reg, 1);
        emitter.cmp(temp_reg, 0);
        emitter.je(new_label);

        const stub_emit::Reg reg_x = arch_utils::pick_random_reg(regs, is64);
        const stub_emit::Reg reg_y = arch_utils::pick_random_reg(regs, is64, reg_x);

        emitter.mov(regs.temp1, reg_x);
        emitter.mov(regs.counter, reg_y);
        emitter.and_(regs.temp1, regs.counter);
        emitter.push(regs.temp1);
        emitter.mov(regs.temp1, reg_x);
        emitter.or_(regs.temp1, regs.counter);
        emitter.pop(regs.base);
        emitter.add(regs.base, regs.temp1);
        emitter.mov(regs.temp1, regs.base);
        emitter.push(regs.temp1);
        emitter.mov(regs.counter, regs.temp1);
        emitter.xor_(regs.counter, reg_x);
        emitter.pop(regs.temp1);
        emitter.bind(new_label);
        emitter.push(regs.base_ptr);
        emitter.mov(regs.base_ptr, regs.stack_ptr);
        for (int i = 0; i < opt.mba_factor; ++i) {
            gen_math_operations();
        }
        emitter.pop(regs.base_ptr);
        break;
    }

    case 2: {
        // (x & ~y) - (~x & y)  ==  x - y
        const stub_emit::Label new_label = emitter.new_label();
        const stub_emit::Reg temp_reg = arch_utils::pick_random_reg(regs, is64);
        emitter.mov(temp_reg, 1);
        emitter.cmp(temp_reg, 0);
        emitter.je(new_label);

        const stub_emit::Reg reg_x = arch_utils::pick_random_reg(regs, is64);
        const stub_emit::Reg reg_y = arch_utils::pick_random_reg(regs, is64, reg_x);

        emitter.mov(regs.temp1, reg_x);
        emitter.mov(regs.counter, reg_y);
        emitter.not_(regs.counter);
        emitter.and_(regs.temp1, regs.counter);
        emitter.push(regs.temp1);
        emitter.mov(regs.temp1, reg_x);
        emitter.not_(regs.temp1);
        emitter.mov(regs.counter, reg_y);
        emitter.and_(regs.temp1, regs.counter);
        emitter.pop(regs.base);
        emitter.sub(regs.base, regs.temp1);
        emitter.mov(regs.temp1, regs.base);
        emitter.push(regs.temp1);
        emitter.mov(regs.counter, regs.temp1);
        emitter.xor_(regs.counter, reg_x);
        emitter.pop(regs.temp1);
        emitter.bind(new_label);
        emitter.push(regs.base_ptr);
        emitter.mov(regs.base_ptr, regs.stack_ptr);
        for (int i = 0; i < opt.mba_factor; ++i) {
            gen_math_operations();
        }
        emitter.pop(regs.base_ptr);
        break;
    }

    case 3: {
        // (x ^ y) - 2 * (~x & y)  ==  x - y
        const stub_emit::Label new_label = emitter.new_label();
        const stub_emit::Reg temp_reg = arch_utils::pick_random_reg(regs, is64);
        emitter.mov(temp_reg, 1);
        emitter.cmp(temp_reg, 0);
        emitter.je(new_label);

        const stub_emit::Reg reg_x = arch_utils::pick_random_reg(regs, is64);
        const stub_emit::Reg reg_y = arch_utils::pick_random_reg(regs, is64, reg_x);

        emitter.mov(regs.temp1, reg_x);
        emitter.mov(regs.counter, reg_y);
        emitter.xor_(regs.temp1, regs.counter);
        emitter.push(regs.temp1);
        emitter.mov(regs.temp1, reg_x);
        emitter.not_(regs.temp1);
        emitter.and_(regs.temp1, regs.counter);
        emitter.add(regs.temp1, regs.temp1);
        emitter.pop(regs.base);
        emitter.sub(regs.base, regs.temp1);
        emitter.mov(regs.temp1, regs.base);
        emitter.push(regs.temp1);
        emitter.mov(regs.counter, regs.temp1);
        emitter.xor_(regs.counter, reg_x);
        emitter.pop(regs.temp1);
        emitter.bind(new_label);
        emitter.push(regs.base_ptr);
        emitter.mov(regs.base_ptr, regs.stack_ptr);
        for (int i = 0; i < opt.mba_factor; ++i) {
            gen_math_operations();
        }
        emitter.pop(regs.base_ptr);
        break;
    }

    default:
        break;
    }
}
