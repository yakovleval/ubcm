#include "ubcm/activation.hpp"

namespace ubcm {
namespace {

CodecError error(CodecErrorCode code, std::uint64_t position, const char* message) {
    return {code, position, message};
}

BitVector encode_unsigned(std::uint64_t value, std::uint8_t width) {
    BitVector output;
    for (int shift = static_cast<int>(width) - 1; shift >= 0; --shift) {
        output.push_back(((value >> shift) & 1U) != 0U);
    }
    return output;
}

CodecResult<BitVector> encode_prefix(const PrefixState& prefix) {
    BitVector payload(128);
    switch (prefix.kind) {
        case PrefixKind::none:
            if (prefix.depth != 0U || prefix.condition) {
                return std::unexpected(error(CodecErrorCode::invalid_data, 0,
                                             "empty prefix must have a zero payload"));
            }
            break;
        case PrefixKind::read:
        case PrefixKind::modify:
            if (prefix.condition) {
                return std::unexpected(error(CodecErrorCode::invalid_data, 0,
                                             "depth prefix cannot contain a condition"));
            }
            payload.write(64, encode_unsigned(prefix.depth, 64));
            break;
        case PrefixKind::condition:
            if (prefix.depth != 0U) {
                return std::unexpected(error(CodecErrorCode::invalid_data, 0,
                                             "condition prefix cannot contain a depth"));
            }
            payload.set(127, prefix.condition);
            break;
        default:
            return std::unexpected(error(CodecErrorCode::invalid_type, 0,
                                         "unknown prefix kind"));
    }
    return payload;
}

CodecResult<PrefixState> decode_prefix(std::uint64_t kind_value,
                                       const BitVector& payload,
                                       std::uint64_t position) {
    if (kind_value > static_cast<std::uint64_t>(PrefixKind::condition)) {
        return std::unexpected(error(CodecErrorCode::invalid_type, position,
                                     "unknown prefix kind"));
    }
    const auto kind = static_cast<PrefixKind>(kind_value);
    BitCursor payload_cursor(payload);
    auto high = payload_cursor.read_uint(64);
    auto low = payload_cursor.read_uint(64);
    if (!high || !low) {
        return std::unexpected(error(CodecErrorCode::unexpected_end, position,
                                     "truncated prefix payload"));
    }
    if (*high != 0U) {
        return std::unexpected(error(CodecErrorCode::invalid_data, position,
                                     "reserved prefix payload bits must be zero"));
    }
    switch (kind) {
        case PrefixKind::none:
            if (*low != 0U) {
                return std::unexpected(error(CodecErrorCode::invalid_data, position,
                                             "empty prefix payload must be zero"));
            }
            return PrefixState{};
        case PrefixKind::read:
        case PrefixKind::modify:
            return PrefixState{kind, *low, false};
        case PrefixKind::condition:
            if (*low > 1U) {
                return std::unexpected(error(CodecErrorCode::invalid_data, position,
                                             "condition prefix has reserved non-zero bits"));
            }
            return PrefixState{kind, 0, *low == 1U};
    }
    return std::unexpected(error(CodecErrorCode::invalid_type, position,
                                 "unknown prefix kind"));
}

}  // namespace

CodecResult<BitVector> encode_activation(const ActivationRecord& activation) {
    if (!activation.local_resolver.null &&
        activation.local_resolver.bit_offset % node_bit_size != 0U) {
        return std::unexpected(error(CodecErrorCode::invalid_data, 288,
                                     "local resolver reference is not node-aligned"));
    }
    if (activation.result_register.class_id != RegisterClass::local) {
        return std::unexpected(error(CodecErrorCode::invalid_data, 544,
                                     "activation result register must be local"));
    }

    auto procedure = encode_register_handle(activation.procedure);
    auto network = encode_runtime_reference(activation.network_state);
    auto resolver = encode_runtime_reference(activation.local_resolver);
    auto previous = encode_runtime_reference(activation.previous_activation);
    auto result = encode_register_handle(activation.result_register);
    auto prefix = encode_prefix(activation.prefix);
    if (!procedure) return std::unexpected(procedure.error());
    if (!network) return std::unexpected(network.error());
    if (!resolver) return std::unexpected(resolver.error());
    if (!previous) return std::unexpected(previous.error());
    if (!result) return std::unexpected(result.error());
    if (!prefix) return std::unexpected(prefix.error());

    BitVector output;
    output.append(encode_unsigned(activation_magic, 16));
    output.append(encode_unsigned(activation_version, 8));
    output.append(encode_unsigned(activation.flags, 8));
    output.append(*procedure);
    output.append(encode_unsigned(activation.procedure_position, 64));
    output.append(*network);
    output.append(*resolver);
    output.append(*previous);
    output.append(*result);
    output.append(encode_unsigned(static_cast<std::uint8_t>(activation.prefix.kind), 8));
    output.append(*prefix);
    return output;
}

CodecResult<ActivationRecord> decode_activation(BitCursor& cursor) {
    const auto start = cursor.position();
    if (cursor.remaining() < activation_bit_size) {
        return std::unexpected(error(CodecErrorCode::unexpected_end, start,
                                     "activation requires 744 bits"));
    }
    BitCursor input = cursor;
    auto magic = input.read_uint(16);
    auto version = input.read_uint(8);
    auto flags = input.read_uint(8);
    if (!magic || !version || !flags || *magic != activation_magic ||
        *version != activation_version) {
        return std::unexpected(error(CodecErrorCode::invalid_data, start,
                                     "invalid activation header"));
    }
    auto procedure = decode_register_handle(input);
    auto position = input.read_uint(64);
    auto network = decode_runtime_reference(input);
    auto resolver = decode_runtime_reference(input);
    auto previous = decode_runtime_reference(input);
    auto result = decode_register_handle(input);
    auto prefix_kind = input.read_uint(8);
    auto prefix_payload = input.read_bits(128);
    if (!procedure || !position || !network || !resolver || !previous || !result ||
        !prefix_kind || !prefix_payload) {
        return std::unexpected(error(CodecErrorCode::invalid_data, start,
                                     "invalid activation field"));
    }
    if (!resolver->null && resolver->bit_offset % node_bit_size != 0U) {
        return std::unexpected(error(CodecErrorCode::invalid_data, start + 288,
                                     "local resolver reference is not node-aligned"));
    }
    if (result->class_id != RegisterClass::local) {
        return std::unexpected(error(CodecErrorCode::invalid_data, start + 544,
                                     "activation result register must be local"));
    }
    auto prefix = decode_prefix(*prefix_kind, *prefix_payload, start + 616);
    if (!prefix) {
        return std::unexpected(prefix.error());
    }
    ActivationRecord activation{static_cast<std::uint8_t>(*flags), *procedure, *position,
                                *network, *resolver, *previous, *result, *prefix};
    (void)cursor.seek(start + activation_bit_size);
    return activation;
}

CodecResult<ActivationRecord> decode_activation(const BitVector& bits) {
    if (bits.size() != activation_bit_size) {
        return std::unexpected(error(CodecErrorCode::invalid_width, 0,
                                     "activation bit vector must contain exactly 744 bits"));
    }
    BitCursor cursor(bits);
    return decode_activation(cursor);
}

}  // namespace ubcm
