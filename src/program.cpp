#include "ubcm/program.hpp"
#include "ubcm/codec.hpp"

#include <map>
#include <set>

namespace ubcm {
namespace {

CodecError error(const char* message, std::uint64_t position = 0) {
    return {CodecErrorCode::invalid_data, position, message};
}

void append_uint(BitVector& bits, std::uint64_t value, int width) {
    for (int i = width - 1; i >= 0; --i) bits.push_back((value >> i) & 1U);
}

}  // namespace

BitVector encode_procedure(const ProcedureImage& procedure) {
    auto bits = encode_bit_string(procedure.name);
    bits.append(encode_bit_string(procedure.contents));
    while (bits.size() % 8 != 0) bits.push_back(false);
    return bits;
}

CodecResult<ProcedureImage> decode_procedure(const BitVector& bits) {
    BitCursor cursor(bits);
    auto name = decode_bit_string(cursor);
    auto contents = decode_bit_string(cursor);
    if (!name) return std::unexpected(name.error());
    if (!contents) return std::unexpected(contents.error());
    if (bits.size() % 8 != 0 || cursor.remaining() >= 8) {
        return std::unexpected(error("invalid procedure file length"));
    }
    while (cursor.remaining() != 0) {
        auto padding = cursor.read_bit();
        if (!padding || *padding) return std::unexpected(error("invalid procedure padding"));
    }
    return ProcedureImage{std::move(*name), std::move(*contents)};
}

CodecResult<void> validate_program(const ProgramImage& program) {
    std::map<std::uint64_t, const ProgramRegister*> records;
    std::set<std::string> names;
    for (const auto& record : program.registers) {
        if (record.id >= (std::uint64_t{1} << 62) || record.flags > 3 ||
            !records.emplace(record.id, &record).second ||
            !names.insert(record.name.to_bit_string()).second) {
            return std::unexpected(error("invalid or duplicate register record"));
        }
        if ((record.flags & 2) && record.contents.size() % node_bit_size != 0) {
            return std::unexpected(error("node register has an incomplete node"));
        }
    }
    const auto procedure = [&](RegisterHandle handle) {
        auto record = records.find(handle.id);
        return handle.class_id == RegisterClass::global && record != records.end() &&
               (record->second->flags & 1);
    };
    const auto node_ref = [&](const NodeReference& ref) {
        if (ref.null) return true;
        auto record = records.find(ref.handle.id);
        return ref.handle.class_id == RegisterClass::global &&
               record != records.end() && (record->second->flags & 2) &&
               ref.bit_offset % node_bit_size == 0 &&
               ref.bit_offset < record->second->contents.size();
    };
    if (!procedure(program.procedure) || program.entry.null || !node_ref(program.entry)) {
        return std::unexpected(error("invalid program procedure or entry"));
    }
    for (const auto& record : program.registers) {
        if (!(record.flags & 2)) continue;
        BitCursor cursor(record.contents);
        while (cursor.remaining() != 0) {
            auto node = decode_node(cursor);
            if (!node) return std::unexpected(node.error());
            if (!node_ref(node->next0) || !node_ref(node->next1) ||
                !node_ref(node->superlocal_resolver) ||
                (node->kind == NodeKind::procedure_call &&
                 (!procedure(node->procedure) || !node_ref(node->entry)))) {
                return std::unexpected(error("node points outside the program"));
            }
        }
    }
    return {};
}

CodecResult<BitVector> encode_program(const ProgramImage& program) {
    if (auto valid = validate_program(program); !valid) return std::unexpected(valid.error());
    BitVector bits;
    append_uint(bits, 0x5542434d, 32);
    append_uint(bits, 2, 16);
    append_uint(bits, 0, 16);
    append_uint(bits, program.registers.size(), 64);
    auto procedure = encode_register_handle(program.procedure);
    auto entry = encode_runtime_reference(program.entry);
    if (!procedure) return std::unexpected(procedure.error());
    if (!entry) return std::unexpected(entry.error());
    bits.append(*procedure);
    bits.append(*entry);
    for (const auto& record : program.registers) {
        append_uint(bits, record.id, 64);
        append_uint(bits, record.flags, 8);
        bits.append(encode_bit_string(record.name));
        bits.append(encode_bit_string(record.contents));
        while (bits.size() % 8 != 0) bits.push_back(false);
    }
    return bits;
}

CodecResult<ProgramImage> decode_program(const BitVector& bits) {
    BitCursor cursor(bits);
    auto magic = cursor.read_uint(32);
    auto version = cursor.read_uint(16);
    auto flags = cursor.read_uint(16);
    auto count = cursor.read_uint(64);
    auto procedure = decode_register_handle(cursor);
    auto entry = decode_runtime_reference(cursor);
    if (!magic || !version || !flags || !count || !procedure || !entry) {
        return std::unexpected(error("truncated container header", cursor.position()));
    }
    if (*magic != 0x5542434d || *version != 2 || *flags != 0 || bits.size() % 8 != 0 ||
        *count > cursor.remaining() / 96) {
        return std::unexpected(error("invalid container header"));
    }
    ProgramImage program{*procedure, *entry, {}};
    for (std::uint64_t i = 0; i < *count; ++i) {
        auto id = cursor.read_uint(64);
        auto record_flags = cursor.read_uint(8);
        auto name = decode_bit_string(cursor);
        auto contents = decode_bit_string(cursor);
        if (!id || !record_flags || !name || !contents) {
            return std::unexpected(error("truncated or invalid register record", cursor.position()));
        }
        program.registers.push_back({*id, static_cast<std::uint8_t>(*record_flags),
                                     std::move(*name), std::move(*contents)});
        while (cursor.position() % 8 != 0) {
            auto padding = cursor.read_bit();
            if (!padding || *padding) {
                return std::unexpected(error("invalid container padding", cursor.position()));
            }
        }
    }
    if (cursor.remaining() != 0) return std::unexpected(error("trailing container data"));
    if (auto valid = validate_program(program); !valid) return std::unexpected(valid.error());
    return program;
}

CodecResult<LoadedProgram> load_program(const ProgramImage& program) {
    if (auto valid = validate_program(program); !valid) return std::unexpected(valid.error());
    LoadedProgram loaded{{}, {}, program.procedure, program.entry};
    for (const auto& record : program.registers) {
        auto inserted = loaded.registers.insert(record.id, record.contents, record.flags & 1);
        if (!inserted) return std::unexpected(error("cannot allocate program register"));
        auto bound = loaded.names.bind(record.name,
                                        RegisterHandle{RegisterClass::global, record.id});
        if (!bound) return std::unexpected(error("cannot bind program register name"));
    }
    return loaded;
}

}  // namespace ubcm
