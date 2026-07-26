#include "pe_view.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string_view>

namespace pe_raw {

    std::uint32_t peek_next_section_rva(const PeView& view) {
        std::uint32_t highest_va_end = 0;
        for (std::size_t i = 0; i < view.num_sections; ++i) {
            const auto& section = *reinterpret_cast<const IMAGE_SECTION_HEADER*>(
                view.bytes.data() + view.section_tbl_off + i * sizeof(IMAGE_SECTION_HEADER)
                );
            const std::uint32_t size = max_u32(section.Misc.VirtualSize, section.SizeOfRawData);
            highest_va_end = max_u32(highest_va_end, section.VirtualAddress + size);
        }

        return align_up(highest_va_end, view.section_alignment);
    }

    bool has_clr_directory(const PeView& view) {
        std::uint32_t clr_rva = 0;
        if (view.is64) {
            const auto* optional = reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(view.bytes.data() + view.opt_hdr_off);
            clr_rva = optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].VirtualAddress;
        }
        else {
            const auto* optional = reinterpret_cast<const IMAGE_OPTIONAL_HEADER32*>(view.bytes.data() + view.opt_hdr_off);
            clr_rva = optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].VirtualAddress;
        }

        return clr_rva != 0;
    }

    void set_section_characteristics(PeView& view, const char* section_name, std::uint32_t characteristics) {
        for (std::size_t i = 0; i < view.num_sections; ++i) {
            auto& section = section_at(view, i);
            if (std::strncmp(reinterpret_cast<const char*>(section.Name), section_name, IMAGE_SIZEOF_SHORT_NAME) == 0) {
                section.Characteristics = characteristics;
                return;
            }
        }

        throw PeError(std::string("section not found: ") + section_name);
    }

    std::vector<std::uint8_t> read_file_bytes(const std::string& path) {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in) {
            throw PeError("cannot open file: " + path);
        }

        const auto size = static_cast<std::size_t>(in.tellg());
        in.seekg(0);

        std::vector<std::uint8_t> data(size);
        if (!in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size))) {
            throw PeError("read failed: " + path);
        }

        return data;
    }

    void write_file_bytes(const std::string& path, const std::vector<std::uint8_t>& data) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw PeError("cannot open file for write: " + path);
        }

        if (!out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()))) {
            throw PeError("write failed: " + path);
        }
    }

    // validate that pe is fully correct and parse necessary info
    PeView parse_pe(std::vector<std::uint8_t> raw) {
        PeView view;
        view.bytes = std::move(raw);

        if (view.bytes.size() < sizeof(IMAGE_DOS_HEADER)) {
            throw PeError("file too small for DOS header");
        }

        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(view.bytes.data());
        if (dos->e_magic != kDosMagic) {
            throw PeError("bad DOS magic");
        }

        view.nt_off = static_cast<std::size_t>(dos->e_lfanew);
        if (view.nt_off + sizeof(std::uint32_t) + sizeof(IMAGE_FILE_HEADER) > view.bytes.size()) {
            throw PeError("NT headers out of range");
        }

        const std::uint32_t signature = *reinterpret_cast<std::uint32_t*>(view.bytes.data() + view.nt_off);
        if (signature != kPeMagic) {
            throw PeError("bad NT signature");
        }

        view.file_hdr_off = view.nt_off + 4;
        auto* file_header = reinterpret_cast<IMAGE_FILE_HEADER*>(view.bytes.data() + view.file_hdr_off);

        view.num_sections = file_header->NumberOfSections;
        view.opt_hdr_off = view.file_hdr_off + sizeof(IMAGE_FILE_HEADER);

        const std::uint16_t opt_magic = *reinterpret_cast<std::uint16_t*>(view.bytes.data() + view.opt_hdr_off);
        if (opt_magic == kOpt64) {
            view.is64 = true;
            if (view.opt_hdr_off + sizeof(IMAGE_OPTIONAL_HEADER64) > view.bytes.size()) {
                throw PeError("PE32+ optional header out of range");
            }

            auto* optional = reinterpret_cast<IMAGE_OPTIONAL_HEADER64*>(view.bytes.data() + view.opt_hdr_off);
            view.file_alignment = optional->FileAlignment;
            view.section_alignment = optional->SectionAlignment;
            view.size_of_headers = optional->SizeOfHeaders;
            view.size_of_image = optional->SizeOfImage;
            view.dll_characteristics = optional->DllCharacteristics;
            view.image_base = optional->ImageBase;
            view.entry_point_rva = optional->AddressOfEntryPoint;
            view.old_reloc_rva = optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
            view.old_reloc_size = optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;
        }
        else if (opt_magic == kOpt32) {
            if (view.opt_hdr_off + sizeof(IMAGE_OPTIONAL_HEADER32) > view.bytes.size()) {
                throw PeError("PE32 optional header out of range");
            }

            auto* optional = reinterpret_cast<IMAGE_OPTIONAL_HEADER32*>(view.bytes.data() + view.opt_hdr_off);
            view.file_alignment = optional->FileAlignment;
            view.section_alignment = optional->SectionAlignment;
            view.size_of_headers = optional->SizeOfHeaders;
            view.size_of_image = optional->SizeOfImage;
            view.dll_characteristics = optional->DllCharacteristics;
            view.image_base = optional->ImageBase;
            view.entry_point_rva = optional->AddressOfEntryPoint;
            view.old_reloc_rva = optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
            view.old_reloc_size = optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size;
        }
        else {
            throw PeError("unknown optional header magic");
        }

        view.section_tbl_off = view.opt_hdr_off + file_header->SizeOfOptionalHeader;
        if (view.section_tbl_off + view.num_sections * sizeof(IMAGE_SECTION_HEADER) > view.bytes.size()) {
            throw PeError("section table out of range");
        }

        if (view.file_alignment == 0 || view.section_alignment == 0) {
            throw PeError("zero alignment in optional header");
        }

        return view;
    }

    bool is_io_pe_error(const char* msg) {
        if (!msg) {
            return false;
        }
        const std::string_view text(msg);
        return text.find("cannot open file") != std::string_view::npos
            || text.find("read failed") != std::string_view::npos;
    }

    ParseFileResult parse_pe_file(const std::string& path) {
        ParseFileResult result;
        try {
            result.view = parse_pe(read_file_bytes(path));
        }
        catch (const PeError& ex) {
            if (is_io_pe_error(ex.what())) {
                result.error_message = std::string("Failed to read input file: ") + path;
            }
            else {
                result.error_message = "Binary is not a valid PE file";
            }
        }
        return result;
    }

    IMAGE_SECTION_HEADER& section_at(PeView& view, std::size_t index) {
        return *reinterpret_cast<IMAGE_SECTION_HEADER*>(
            view.bytes.data() + view.section_tbl_off + index * sizeof(IMAGE_SECTION_HEADER)
            );
    }

    const IMAGE_SECTION_HEADER* section_for_rva(PeView& view, std::uint32_t rva) {
        for (std::size_t i = 0; i < view.num_sections; ++i) {
            const auto& section = section_at(view, i);
            const std::uint32_t va = section.VirtualAddress;
            const std::uint32_t size = max_u32(section.Misc.VirtualSize, section.SizeOfRawData);

            if (rva >= va && rva < va + size) {
                return &section;
            }
        }

        return nullptr;
    }

    const IMAGE_SECTION_HEADER* section_by_name(PeView& view, const char* name) {
        for (std::size_t i = 0; i < view.num_sections; ++i) {
            const auto& section = section_at(view, i);
            if (std::strncmp(reinterpret_cast<const char*>(section.Name), name, IMAGE_SIZEOF_SHORT_NAME) == 0) {
                return &section;
            }
        }

        return nullptr;
    }

    std::size_t rva_to_file_off(const IMAGE_SECTION_HEADER& section, std::uint32_t rva) {
        return static_cast<std::size_t>(section.PointerToRawData) + static_cast<std::size_t>(rva - section.VirtualAddress);
    }

    std::size_t any_rva_to_file_off(PeView& view, std::uint32_t rva) {
        for (std::size_t i = 0; i < view.num_sections; ++i) {
            const auto& section = section_at(view, i);
            const std::uint32_t va = section.VirtualAddress;
            const std::uint32_t size = max_u32(section.Misc.VirtualSize, section.SizeOfRawData);

            if (rva >= va && rva < va + size) {
                return rva_to_file_off(section, rva);
            }
        }

        return 0;
    }

    std::uint32_t va_to_rva(const PeView& view, std::uint64_t va) {
        if (va < view.image_base) {
            throw PeError("virtual address below image base");
        }

        return static_cast<std::uint32_t>(va - view.image_base);
    }

    std::uint64_t rva_to_va(const PeView& view, std::uint32_t rva) {
        return view.image_base + rva;
    }

    ImportLookup find_iat_slot(PeView& view, const char* module, const char* symbol) {
        ImportLookup result;

        std::uint32_t import_rva = 0;
        std::uint32_t import_size = 0;

        if (view.is64) {
            auto* optional = reinterpret_cast<IMAGE_OPTIONAL_HEADER64*>(view.bytes.data() + view.opt_hdr_off);
            import_rva = optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
            import_size = optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;
        }
        else {
            auto* optional = reinterpret_cast<IMAGE_OPTIONAL_HEADER32*>(view.bytes.data() + view.opt_hdr_off);
            import_rva = optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
            import_size = optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;
        }

        if (import_rva == 0 || import_size < sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
            return result;
        }

        const std::size_t import_file_off = any_rva_to_file_off(view, import_rva);
        if (import_file_off == 0) {
            return result;
        }

        auto* descriptors = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(view.bytes.data() + import_file_off);
        const std::size_t descriptor_count = import_size / sizeof(IMAGE_IMPORT_DESCRIPTOR);

        auto eq_ci = [](char a, char b) {
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
            return a == b;
            };

        for (std::size_t d = 0; d < descriptor_count; ++d) {
            const auto& descriptor = descriptors[d];
            if (descriptor.Name == 0 || descriptor.OriginalFirstThunk == 0 || descriptor.FirstThunk == 0) {
                continue;
            }

            const std::size_t name_off = any_rva_to_file_off(view, descriptor.Name);
            if (name_off == 0) {
                continue;
            }

            const char* dll_name = reinterpret_cast<const char*>(view.bytes.data() + name_off);

            std::size_t module_index = 0;
            bool match = true;
            for (; module[module_index]; ++module_index) {
                if (!dll_name[module_index] || !eq_ci(dll_name[module_index], module[module_index])) {
                    match = false;
                    break;
                }
            }

            if (match) {
                const char tail = dll_name[module_index];
                if (tail != 0 && tail != '.') {
                    match = false;
                }
            }

            if (!match) {
                continue;
            }

            const std::size_t ilt_off = any_rva_to_file_off(view, descriptor.OriginalFirstThunk);
            if (ilt_off == 0) {
                continue;
            }

            const std::size_t thunk_size = view.is64 ? 8u : 4u;
            std::uint32_t index = 0;

            while (true) {
                const std::size_t current = ilt_off + index * thunk_size;
                if (current + thunk_size > view.bytes.size()) {
                    break;
                }

                std::uint64_t thunk_value = 0;
                if (view.is64) {
                    thunk_value = *reinterpret_cast<std::uint64_t*>(view.bytes.data() + current);
                }
                else {
                    thunk_value = *reinterpret_cast<std::uint32_t*>(view.bytes.data() + current);
                }

                if (thunk_value == 0) {
                    break;
                }

                const std::uint64_t ordinal_bit = view.is64 ? (1ULL << 63) : (1ULL << 31);
                if (thunk_value & ordinal_bit) {
                    ++index;
                    continue;
                }

                const std::uint32_t name_rva = static_cast<std::uint32_t>(thunk_value);
                const std::size_t hint_off = any_rva_to_file_off(view, name_rva);
                if (hint_off == 0) {
                    ++index;
                    continue;
                }

                const char* function_name = reinterpret_cast<const char*>(view.bytes.data() + hint_off + 2);
                if (std::strcmp(function_name, symbol) == 0) {
                    result.iat_rva = descriptor.FirstThunk + static_cast<std::uint32_t>(index * thunk_size);
                    result.iat_file_off = any_rva_to_file_off(view, result.iat_rva);
                    result.found = result.iat_file_off != 0;
                    return result;
                }

                ++index;
            }
        }

        return result;
    }

    AddedSection add_executable_section(
        PeView& view,
        const std::string& section_name,
        const std::vector<std::uint8_t>& section_data,
        bool set_entry_point,
        std::uint32_t entry_offset_in_section
    ) {
        if (section_data.empty()) {
            throw PeError("section data is empty");
        }

        std::uint32_t highest_va_end = 0;
        for (std::size_t i = 0; i < view.num_sections; ++i) {
            const auto& section = section_at(view, i);
            const std::uint32_t size = max_u32(section.Misc.VirtualSize, section.SizeOfRawData);
            highest_va_end = max_u32(highest_va_end, section.VirtualAddress + size);
        }

        const std::uint32_t new_rva = align_up(highest_va_end, view.section_alignment);
        const std::uint32_t new_raw_off = align_up(static_cast<std::uint32_t>(view.bytes.size()), view.file_alignment);

        std::uint32_t headers_end = view.size_of_headers;
        if (view.num_sections > 0) {
            const auto& first = section_at(view, 0);
            if (first.PointerToRawData > 0 && first.PointerToRawData < headers_end) {
                headers_end = first.PointerToRawData;
            }
        }

        const std::size_t new_section_header_off =
            view.section_tbl_off + static_cast<std::size_t>(view.num_sections) * sizeof(IMAGE_SECTION_HEADER);

        if (new_section_header_off + sizeof(IMAGE_SECTION_HEADER) > headers_end) {
            throw PeError("no room in section header table for another entry");
        }

        const std::uint32_t virtual_size = static_cast<std::uint32_t>(section_data.size());
        const std::uint32_t raw_size = align_up(virtual_size, view.file_alignment);

        if (view.bytes.size() < new_raw_off) {
            view.bytes.resize(new_raw_off, 0);
        }

        view.bytes.insert(view.bytes.end(), section_data.begin(), section_data.end());
        if (view.bytes.size() < new_raw_off + raw_size) {
            view.bytes.resize(new_raw_off + raw_size, 0);
        }

        auto* section_header = reinterpret_cast<IMAGE_SECTION_HEADER*>(view.bytes.data() + new_section_header_off);
        std::memset(section_header, 0, sizeof(*section_header));

        const std::size_t name_len = min_size(section_name.size(), static_cast<std::size_t>(IMAGE_SIZEOF_SHORT_NAME));
        std::memcpy(section_header->Name, section_name.data(), name_len);

        section_header->Misc.VirtualSize = virtual_size;
        section_header->VirtualAddress = new_rva;
        section_header->SizeOfRawData = raw_size;
        section_header->PointerToRawData = new_raw_off;
        section_header->Characteristics =
            IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE;

        auto* file_header = reinterpret_cast<IMAGE_FILE_HEADER*>(view.bytes.data() + view.file_hdr_off);
        file_header->NumberOfSections = static_cast<std::uint16_t>(view.num_sections + 1);

        const std::uint32_t new_size_of_image = align_up(new_rva + virtual_size, view.section_alignment);
        if (view.is64) {
            auto* optional = reinterpret_cast<IMAGE_OPTIONAL_HEADER64*>(view.bytes.data() + view.opt_hdr_off);
            optional->SizeOfImage = new_size_of_image;
            optional->CheckSum = 0;
            if (set_entry_point) {
                optional->AddressOfEntryPoint = new_rva + entry_offset_in_section;
                view.entry_point_rva = optional->AddressOfEntryPoint;
            }
        }
        else {
            auto* optional = reinterpret_cast<IMAGE_OPTIONAL_HEADER32*>(view.bytes.data() + view.opt_hdr_off);
            optional->SizeOfImage = new_size_of_image;
            optional->CheckSum = 0;
            if (set_entry_point) {
                optional->AddressOfEntryPoint = new_rva + entry_offset_in_section;
                view.entry_point_rva = optional->AddressOfEntryPoint;
            }
        }

        view.num_sections += 1;
        view.size_of_image = new_size_of_image;

        AddedSection added;
        added.rva = new_rva;
        added.raw_offset = new_raw_off;
        added.raw_size = raw_size;
        added.virtual_size = virtual_size;
        added.name = section_name;
        return added;
    }

    void set_dll_characteristics(PeView& view, std::uint16_t characteristics) {
        if (view.is64) {
            auto* optional = reinterpret_cast<IMAGE_OPTIONAL_HEADER64*>(view.bytes.data() + view.opt_hdr_off);
            optional->DllCharacteristics = characteristics;
        }
        else {
            auto* optional = reinterpret_cast<IMAGE_OPTIONAL_HEADER32*>(view.bytes.data() + view.opt_hdr_off);
            optional->DllCharacteristics = characteristics;
        }

        view.dll_characteristics = characteristics;
    }

    // special for -noaslr / -senc
    void clear_aslr(PeView& view) {
        const std::uint16_t drop =
            IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE |
            IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA;
        set_dll_characteristics(
            view,
            static_cast<std::uint16_t>(view.dll_characteristics & ~drop)
        );
    }

    void clear_basereloc_directory(PeView& view) {
        if (view.is64) {
            auto* optional = reinterpret_cast<IMAGE_OPTIONAL_HEADER64*>(view.bytes.data() + view.opt_hdr_off);
            optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress = 0;
            optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size = 0;
        }
        else {
            auto* optional = reinterpret_cast<IMAGE_OPTIONAL_HEADER32*>(view.bytes.data() + view.opt_hdr_off);
            optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress = 0;
            optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size = 0;
        }
        view.old_reloc_rva = 0;
        view.old_reloc_size = 0;
    }


    std::uint32_t load_config_rva(const PeView& view) {
        if (view.is64) {
            const auto* optional = reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(view.bytes.data() + view.opt_hdr_off);
            return optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].VirtualAddress;
        }

        const auto* optional = reinterpret_cast<const IMAGE_OPTIONAL_HEADER32*>(view.bytes.data() + view.opt_hdr_off);
        return optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].VirtualAddress;
    }

    std::uint32_t load_config_size(const PeView& view) {
        if (view.is64) {
            const auto* optional = reinterpret_cast<const IMAGE_OPTIONAL_HEADER64*>(view.bytes.data() + view.opt_hdr_off);
            return optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].Size;
        }

        const auto* optional = reinterpret_cast<const IMAGE_OPTIONAL_HEADER32*>(view.bytes.data() + view.opt_hdr_off);
        return optional->DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG].Size;
    }

    std::uint32_t read_u32(const std::uint8_t* data, std::size_t offset) {
        std::uint32_t value = 0;
        std::memcpy(&value, data + offset, sizeof(value));
        return value;
    }

    std::uint64_t read_u64(const std::uint8_t* data, std::size_t offset) {
        std::uint64_t value = 0;
        std::memcpy(&value, data + offset, sizeof(value));
        return value;
    }

    void write_u32(std::uint8_t* data, std::size_t offset, std::uint32_t value) {
        std::memcpy(data + offset, &value, sizeof(value));
    }

    std::uint32_t guard_cf_entry_stride(std::uint32_t guard_flags) {
        return 4u + ((guard_flags & 0xF0000000u) >> 28);
    }

    std::uint32_t guard_cf_flags_offset(bool is64, std::uint32_t config_size) {
        /*
        * IMAGE_LOAD_CONFIG_DIRECTORY64
        * ref: https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_load_config_directory64
        */
        if (is64) {
            if (config_size >= 0x94u) {
                return 0x90u;
            }
            if (config_size >= 0x90u) {
                return 0x8Cu; // GuardFlags
            }
            return 0;
        }

        /*
        * IMAGE_LOAD_CONFIG_DIRECTORY32
        * ref: https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-image_load_config_directory32
        */

        if (config_size >= 0x5Cu) {
            return 0x58u;
        }
        return 0;
    }

    struct GuardCfDirectoryLayout {
        std::uint32_t min_directory_size;
        std::uint32_t min_declared_size;
        std::uint32_t table_va_offset;
        std::uint32_t count_offset;
    };

    GuardCfDirectoryLayout guard_cf_layout(bool is64) {
        if (is64) {
            return { 0x8Cu, 0x8Cu, 0x80u, 0x88u };
        }
        return { 0x58u, 0x58u, 0x50u, 0x54u };
    }

    std::uint32_t load_config_bytes_available(PeView& view, std::uint32_t config_rva) {
        const auto* section = section_for_rva(view, config_rva);
        if (!section) {
            return 0;
        }

        const std::uint32_t section_end_rva = section->VirtualAddress +
            max_u32(section->Misc.VirtualSize, section->SizeOfRawData);
        if (config_rva >= section_end_rva) {
            return 0;
        }

        return section_end_rva - config_rva;
    }

    std::uint32_t effective_load_config_size(
        PeView& view,
        std::uint32_t config_rva,
        std::uint32_t directory_size,
        std::uint32_t declared_size
    ) {
        std::uint32_t effective = declared_size;
        if (effective == 0) {
            effective = directory_size;
        }
        if (directory_size > effective) {
            effective = directory_size;
        }

        const std::uint32_t section_available = load_config_bytes_available(view, config_rva);
        if (section_available > 0 && effective > section_available) {
            effective = section_available;
        }

        return effective;
    }

    std::uint64_t read_va(const std::uint8_t* data, std::size_t offset, bool is64) {
        if (is64) {
            return read_u64(data, offset);
        }
        return read_u32(data, offset);
    }

    std::uint32_t va_to_rva_from_image_base(std::uint64_t va, const PeView& view) {
        if (va < view.image_base) {
            throw PeError("Guard CF pointer is below image base");
        }
        return static_cast<std::uint32_t>(va - view.image_base);
    }

    bool guard_cf_table_is_sorted(
        const std::uint8_t* table,
        std::uint32_t count,
        std::uint32_t stride
    ) {
        std::uint32_t previous = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t rva = read_u32(table, static_cast<std::size_t>(i) * stride);
            if (i != 0 && rva < previous) {
                return false;
            }
            previous = rva;
        }
        return true;
    }

    std::uint32_t detect_guard_cf_stride(const std::uint8_t* table, std::uint32_t count) {
        for (std::uint32_t extra = 0; extra <= 16u; ++extra) {
            const std::uint32_t stride = 4u + extra;
            if (guard_cf_table_is_sorted(table, count, stride)) {
                return stride;
            }
        }
        return 0;
    }


    void add_guard_cf_target(PeView& view, std::uint32_t target_rva) {
        if ((view.dll_characteristics & IMAGE_DLLCHARACTERISTICS_GUARD_CF) == 0) {
            return;
        }

        const GuardCfDirectoryLayout layout = guard_cf_layout(view.is64);
        const std::uint32_t config_rva = load_config_rva(view);
        if (config_rva == 0) {
            throw PeError("CFG image is missing a load configuration directory");
        }

        const std::size_t config_off = any_rva_to_file_off(view, config_rva);
        if (config_off == 0 || config_off + 4 > view.bytes.size()) {
            throw PeError("load configuration directory is out of range");
        }

        const std::uint32_t directory_size = load_config_size(view);
        const std::uint32_t declared_size = read_u32(view.bytes.data() + config_off, 0);
        const std::uint32_t config_size = effective_load_config_size(view, config_rva, directory_size, declared_size);
        if (config_size < layout.min_declared_size) {
            throw PeError("CFG image is missing a load configuration directory");
        }

        if (config_off + config_size > view.bytes.size()) {
            throw PeError("load configuration directory is out of range");
        }

        std::uint8_t* config = view.bytes.data() + config_off;
        if (declared_size == 0 || declared_size < layout.min_declared_size) {
            throw PeError("unsupported load configuration directory size for CFG patching");
        }

        const std::uint64_t table_va = read_va(config, layout.table_va_offset, view.is64);
        const std::uint32_t entry_count = read_u32(config, layout.count_offset);
        if (table_va == 0 || entry_count == 0) {
            throw PeError("CFG image is missing a Guard CF function table");
        }

        const std::uint32_t flags_offset = guard_cf_flags_offset(view.is64, declared_size);
        if (flags_offset == 0) {
            throw PeError("cannot locate GuardFlags in load configuration directory");
        }

        std::uint32_t guard_flags = read_u32(config, flags_offset);
        if (guard_flags == 0 && view.is64 && declared_size >= 0x94u) {
            guard_flags = read_u32(config, 0x8Cu);
        }

        const std::uint32_t table_rva = va_to_rva_from_image_base(table_va, view);
        const std::size_t table_off = any_rva_to_file_off(view, table_rva);
        if (table_off == 0) {
            throw PeError("Guard CF function table is out of range");
        }

        std::uint8_t* table = view.bytes.data() + table_off;
        std::uint32_t stride = guard_cf_entry_stride(guard_flags);
        if (!guard_cf_table_is_sorted(table, entry_count, stride)) {
            const std::uint32_t detected = detect_guard_cf_stride(table, entry_count);
            if (detected == 0) {
                throw PeError("Guard CF function table is not sorted");
            }
            stride = detected;
        }

        const auto* section = section_for_rva(view, table_rva);
        if (!section) {
            throw PeError("Guard CF function table is not inside a section");
        }

        const std::uint32_t section_end_rva = section->VirtualAddress +
            max_u32(section->Misc.VirtualSize, section->SizeOfRawData);
        const std::uint32_t needed_end = table_rva + (entry_count + 1u) * stride;
        if (needed_end > section_end_rva) {
            throw PeError("no room in Guard CF function table section for stub entry");
        }

        std::vector<std::uint32_t> entries;
        entries.reserve(entry_count + 1u);
        for (std::uint32_t i = 0; i < entry_count; ++i) {
            entries.push_back(read_u32(table, static_cast<std::size_t>(i) * stride));
        }

        for (std::uint32_t existing : entries) {
            if (existing == target_rva) {
                return;
            }
        }

        const auto insert_it = std::lower_bound(entries.begin(), entries.end(), target_rva);
        entries.insert(insert_it, target_rva);

        for (std::size_t i = 0; i < entries.size(); ++i) {
            const std::size_t entry_off = static_cast<std::size_t>(i) * stride;
            write_u32(table, entry_off, entries[i]);
            for (std::uint32_t pad = 4; pad < stride; ++pad) {
                table[entry_off + pad] = 0;
            }
        }

        write_u32(config, layout.count_offset, static_cast<std::uint32_t>(entries.size()));
    }

    void patch_at_rva(PeView& view, std::uint32_t rva, const std::uint8_t* data, std::size_t size) {
        const auto* section = section_for_rva(view, rva);
        if (!section) {
            throw PeError("patch rva is not inside any section");
        }

        const std::size_t file_off = rva_to_file_off(*section, rva);
        if (file_off + size > view.bytes.size()) {
            throw PeError("patch exceeds file bounds");
        }

        std::memcpy(view.bytes.data() + file_off, data, size);
    }

}
