#include "mba.hpp"
#include "../utils/utils.hpp"
#include "arch_utils.hpp"

using namespace asmjit;

c_mba::c_mba(c_core& g_core) : m_core(g_core){}

void c_mba::gen_math_operations() {
	switch (rand() % 4) {
	case 0:
		m_core.get_assembler()->shr(m_core.get_rand_reg(), random_value(1, 100));
		break;

	case 1:
		m_core.get_assembler()->and_(m_core.get_rand_reg(), random_value(1, 100));
		break;

	case 2:
		m_core.get_assembler()->xor_(m_core.get_rand_reg(), random_value(1, 100));
		break;

	case 3:
		m_core.get_assembler()->add(m_core.get_rand_reg(), random_value(1, 100));
		break;

	default:
		break;
	}


}

void c_mba::mba_code(c_mba::options opt) {

	int x = random_value(0, 3);
	switch (x) {

	case 0: {
		arch_utils::arch_regs regs = m_core.get_arch_regs();
		bool is64 = m_core.is_x64();

		Label new_label = m_core.get_assembler()->newLabel();
		
		// setup comparison to ensure je works correctly
		// use a register that won't conflict with critical registers
		auto temp_reg = m_core.get_rand_reg();
		// ensure temp_reg is not base_ptr or stack_ptr
		while (temp_reg == regs.base_ptr || temp_reg == regs.stack_ptr) {
			temp_reg = m_core.get_rand_reg();
		}
		
		// set up a comparison that will always be false (ensuring je never jumps)
		// this makes the code path deterministic
		m_core.get_assembler()->mov(temp_reg, 1);
		m_core.get_assembler()->cmp(temp_reg, 0);
		
		// create new jump equal to label (will never jump, but obfuscates)
		m_core.get_assembler()->je(new_label);

		// load x and y into regs (ensure they don't conflict with critical regs)
		auto reg_x = m_core.get_rand_reg();
		auto reg_y = m_core.get_rand_reg();
		// ensure reg_x and reg_y don't conflict with critical registers
		while (reg_x == regs.base_ptr || reg_x == regs.stack_ptr || reg_x == regs.base || reg_x == regs.counter) {
			reg_x = m_core.get_rand_reg();
		}
		while (reg_y == regs.base_ptr || reg_y == regs.stack_ptr || reg_y == regs.base || reg_y == regs.counter || reg_y == reg_x) {
			reg_y = m_core.get_rand_reg();
		}
		
		m_core.get_assembler()->mov(regs.temp1, reg_x);
		m_core.get_assembler()->mov(regs.counter, reg_y);

		// calculate in temp1: (X | Y)
		// store result in stack
		m_core.get_assembler()->or_(regs.temp1, regs.counter); // temp1 = X | Y
		m_core.get_assembler()->push(regs.temp1);

		// calculate in temp1: (X & Y)
		m_core.get_assembler()->mov(regs.temp1, reg_x);
		m_core.get_assembler()->and_(regs.temp1, regs.counter);

		// get (X | Y) from stack and substraction (X & Y) 
		// store result in base
		m_core.get_assembler()->pop(regs.base);
		m_core.get_assembler()->sub(regs.base, regs.temp1);

		m_core.get_assembler()->mov(regs.temp1, regs.base);

		// store result in stack and manipulate it
		m_core.get_assembler()->push(regs.temp1);
		m_core.get_assembler()->mov(regs.counter, regs.temp1);
		m_core.get_assembler()->xor_(regs.counter, reg_x);
		
		// clean up stack: pop the value we pushed
		m_core.get_assembler()->pop(regs.temp1);

		// its loc
		m_core.get_assembler()->bind(new_label);

		// store base pointer and push new from stack
		m_core.get_assembler()->push(regs.base_ptr);
		m_core.get_assembler()->mov(regs.base_ptr, regs.stack_ptr);
		gen_math_operations();

		// restore base pointer
		m_core.get_assembler()->pop(regs.base_ptr);

		break;
	}

	case 1: {
		arch_utils::arch_regs regs = m_core.get_arch_regs();
		bool is64 = m_core.is_x64();

		Label new_label = m_core.get_assembler()->newLabel();

		auto temp_reg = m_core.get_rand_reg();
		while (temp_reg == regs.base_ptr || temp_reg == regs.stack_ptr) {
			temp_reg = m_core.get_rand_reg();
		}
		
		m_core.get_assembler()->mov(temp_reg, 1);
		m_core.get_assembler()->cmp(temp_reg, 0);

		// create new jump equal to label
		m_core.get_assembler()->je(new_label);

		// load x and y into regs
		auto reg_x = m_core.get_rand_reg();
		auto reg_y = m_core.get_rand_reg();
		// ensure reg_x and reg_y don't conflict with critical registers
		while (reg_x == regs.base_ptr || reg_x == regs.stack_ptr || reg_x == regs.base || reg_x == regs.counter) {
			reg_x = m_core.get_rand_reg();
		}
		while (reg_y == regs.base_ptr || reg_y == regs.stack_ptr || reg_y == regs.base || reg_y == regs.counter || reg_y == reg_x) {
			reg_y = m_core.get_rand_reg();
		}
		
		m_core.get_assembler()->mov(regs.temp1, reg_x);
		m_core.get_assembler()->mov(regs.counter, reg_y);

		// calculate in temp1: (X & Y)
		// store result in stack
		m_core.get_assembler()->and_(regs.temp1, regs.counter);
		m_core.get_assembler()->push(regs.temp1);

		// calculate in temp1: (X | Y)
		m_core.get_assembler()->mov(regs.temp1, reg_x);
		m_core.get_assembler()->or_(regs.temp1, regs.counter);

		// get (X & Y) from stack and addition (X | Y)
		// store result in base
		m_core.get_assembler()->pop(regs.base);
		m_core.get_assembler()->add(regs.base, regs.temp1);

		m_core.get_assembler()->mov(regs.temp1, regs.base);

		// store result in stack and manipulate it
		m_core.get_assembler()->push(regs.temp1);
		m_core.get_assembler()->mov(regs.counter, regs.temp1);
		m_core.get_assembler()->xor_(regs.counter, reg_x);
		
		// clean up stack: pop the value we pushed
		m_core.get_assembler()->pop(regs.temp1);

		// its loc
		m_core.get_assembler()->bind(new_label);

		// store base pointer and push new from stack
		m_core.get_assembler()->push(regs.base_ptr);
		m_core.get_assembler()->mov(regs.base_ptr, regs.stack_ptr);
		gen_math_operations();

		// restore base pointer
		m_core.get_assembler()->pop(regs.base_ptr);

		break;
	}

	case 2: {
		arch_utils::arch_regs regs = m_core.get_arch_regs();
		bool is64 = m_core.is_x64();

		Label new_label = m_core.get_assembler()->newLabel();

		auto temp_reg = m_core.get_rand_reg();
		while (temp_reg == regs.base_ptr || temp_reg == regs.stack_ptr) {
			temp_reg = m_core.get_rand_reg();
		}
		
		m_core.get_assembler()->mov(temp_reg, 1);
		m_core.get_assembler()->cmp(temp_reg, 0);

		// create new jump equal to label
		m_core.get_assembler()->je(new_label);

		// load x and y into regs
		auto reg_x = m_core.get_rand_reg();
		auto reg_y = m_core.get_rand_reg();

		while (reg_x == regs.base_ptr || reg_x == regs.stack_ptr || reg_x == regs.base || reg_x == regs.counter) {
			reg_x = m_core.get_rand_reg();
		}
		while (reg_y == regs.base_ptr || reg_y == regs.stack_ptr || reg_y == regs.base || reg_y == regs.counter || reg_y == reg_x) {
			reg_y = m_core.get_rand_reg();
		}
		
		m_core.get_assembler()->mov(regs.temp1, reg_x);
		m_core.get_assembler()->mov(regs.counter, reg_y);

		// calculate in temp1: (X & Y)
		// store result in stack
		m_core.get_assembler()->xor_(regs.temp1, regs.counter);
		m_core.get_assembler()->neg(regs.temp1);
		m_core.get_assembler()->push(regs.temp1);

		// calculate in temp1: (X | Y)
		m_core.get_assembler()->mov(regs.temp1, reg_x);
		m_core.get_assembler()->neg(regs.temp1);
		m_core.get_assembler()->and_(regs.temp1, regs.counter);

		// get (X & Y) from stack and addition (X | Y)
		// store result in base
		m_core.get_assembler()->pop(regs.base);
		m_core.get_assembler()->add(regs.base, regs.temp1);

		m_core.get_assembler()->mov(regs.temp1, regs.base);

		// store result in stack and manipulate it
		m_core.get_assembler()->push(regs.temp1);
		m_core.get_assembler()->mov(regs.counter, regs.temp1);
		m_core.get_assembler()->xor_(regs.counter, reg_x);
		
		// clean up stack: pop the value we pushed
		m_core.get_assembler()->pop(regs.temp1);

		// its loc
		m_core.get_assembler()->bind(new_label);

		// store base pointer and push new from stack
		m_core.get_assembler()->push(regs.base_ptr);
		m_core.get_assembler()->mov(regs.base_ptr, regs.stack_ptr);
		gen_math_operations();

		// restore base pointer
		m_core.get_assembler()->pop(regs.base_ptr);

		break;
	}
	}
}