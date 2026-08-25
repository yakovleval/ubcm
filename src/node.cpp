#include "ubcm/node.hpp"

#include <utility>

namespace ubcm {
namespace {

CodecError error(CodecErrorCode code, std::uint64_t position, const char* message) {
    return {code, position, message};
}

bool valid_node_reference(const NodeReference& reference) {
    return reference.null || reference.bit_offset % node_bit_size == 0U;
}

CodecResult<void> append_reference(BitVector& output, const NodeReference& reference) {
    if (!valid_node_reference(reference)) {
        return std::unexpected(error(CodecErrorCode::invalid_data, output.size(),
                                     "node reference is not aligned to 592 bits"));
    }
    auto encoded = encode_runtime_reference(reference);
    if (!encoded) {
        return std::unexpected(encoded.error());
    }
    output.append(*encoded);
    return {};
}

}  // namespace

CodecResult<BitVector> encode_node(const Node& node) {
    BitVector output;
    output.push_back(node.kind == NodeKind::procedure_call);

    if (node.kind == NodeKind::builtin) {
        if (node.command > 0x0dU) {
            return std::unexpected(error(CodecErrorCode::invalid_data, 1,
                                         "builtin command must be in range 0000..1101"));
        }
        for (int shift = 14; shift >= 0; --shift) {
            output.push_back(((node.command >> shift) & 1U) != 0U);
        }
        output.append(BitVector(192));
    } else if (node.kind == NodeKind::procedure_call) {
        output.append(BitVector(15));
        auto procedure = encode_register_handle(node.procedure);
        auto entry = encode_runtime_reference(node.entry);
        if (!procedure) {
            return std::unexpected(procedure.error());
        }
        if (!entry) {
            return std::unexpected(entry.error());
        }
        if (!valid_node_reference(node.entry) || node.entry.null) {
            return std::unexpected(error(CodecErrorCode::invalid_data, 80,
                                         "call entry must be a non-null aligned node reference"));
        }
        output.append(*procedure);
        output.append(*entry);
    } else {
        return std::unexpected(error(CodecErrorCode::invalid_type, 0,
                                     "unknown node kind"));
    }

    if (auto result = append_reference(output, node.next0); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = append_reference(output, node.next1); !result) {
        return std::unexpected(result.error());
    }
    if (auto result = append_reference(output, node.superlocal_resolver); !result) {
        return std::unexpected(result.error());
    }
    return output;
}

CodecResult<Node> decode_node(BitCursor& cursor) {
    const auto start = cursor.position();
    if (cursor.remaining() < node_bit_size) {
        return std::unexpected(error(CodecErrorCode::unexpected_end, start,
                                     "node requires 592 bits"));
    }
    BitCursor input = cursor;
    auto type = input.read_bit();
    auto data = input.read_uint(15);
    auto action = input.read_bits(192);
    if (!type || !data || !action) {
        return std::unexpected(error(CodecErrorCode::unexpected_end, start,
                                     "truncated node header"));
    }

    Node node;
    if (!*type) {
        if (*data > 0x0dU || *action != BitVector(192)) {
            return std::unexpected(error(CodecErrorCode::invalid_data, start,
                                         "invalid builtin node header"));
        }
        node.kind = NodeKind::builtin;
        node.command = static_cast<std::uint8_t>(*data);
    } else {
        if (*data != 0U) {
            return std::unexpected(error(CodecErrorCode::invalid_data, start + 1,
                                         "call node DATA must be zero"));
        }
        BitCursor action_cursor(*action);
        auto procedure = decode_register_handle(action_cursor);
        auto entry = decode_runtime_reference(action_cursor);
        if (!procedure || !entry || entry->null || !valid_node_reference(*entry)) {
            return std::unexpected(error(CodecErrorCode::invalid_data, start + 16,
                                         "invalid call node ACTION"));
        }
        node.kind = NodeKind::procedure_call;
        node.procedure = *procedure;
        node.entry = *entry;
    }

    auto next0 = decode_runtime_reference(input);
    auto next1 = decode_runtime_reference(input);
    auto resolver = decode_runtime_reference(input);
    if (!next0 || !next1 || !resolver) {
        return std::unexpected(error(CodecErrorCode::invalid_data, input.position(),
                                     "invalid node reference"));
    }
    if (!valid_node_reference(*next0) || !valid_node_reference(*next1) ||
        !valid_node_reference(*resolver)) {
        return std::unexpected(error(CodecErrorCode::invalid_data, start + 208,
                                     "unaligned node reference"));
    }
    node.next0 = *next0;
    node.next1 = *next1;
    node.superlocal_resolver = *resolver;
    (void)cursor.seek(start + node_bit_size);
    return node;
}

CodecResult<Node> decode_node(const BitVector& bits) {
    if (bits.size() != node_bit_size) {
        return std::unexpected(error(CodecErrorCode::invalid_width, 0,
                                     "node bit vector must contain exactly 592 bits"));
    }
    BitCursor cursor(bits);
    return decode_node(cursor);
}

}  // namespace ubcm
