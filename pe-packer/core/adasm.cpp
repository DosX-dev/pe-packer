#include "adasm.hpp"

#include "../utils/utils.hpp"

c_adasm::c_adasm(c_core& g_core) : m_core(g_core) {}

void c_adasm::jmp_label_skip() {
    auto& emitter = m_core.get_emitter();
    const stub_emit::Label skip_cc = emitter.new_label();
    emitter.jz(skip_cc);
    emitter.jnz(skip_cc);
    emitter.db(0xE9);

    if (m_core.obf_fake_instr) {
        const int junk_count = m_core.random_in_profile_range(
            static_cast<int>(m_core.obfuscation_profile().adasm_junk_min),
            static_cast<int>(m_core.obfuscation_profile().adasm_junk_max)
        );
        for (int i = 0; i < junk_count; ++i) {
            emitter.db(static_cast<std::uint8_t>(random_value(0x00, 0xFF)));
        }
    }

    emitter.bind(skip_cc);
}
