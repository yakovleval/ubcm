#include "ubcm/codec.hpp"

#include <bit>
#include <limits>

namespace ubcm {
namespace {

CodecError error(CodecErrorCode code, std::uint64_t position, const char* message) {
    return CodecError{code, position, message};
}

void append_uint(BitVector& output, std::uint64_t value, std::uint8_t width) {
    for (std::uint8_t index = width; index > 0U; --index) {
        output.push_back(((value >> (index - 1U)) & 1U) != 0U);
    }
}

std::uint8_t minimal_signed_width(std::int64_t value) {
    for (std::uint8_t width = 1U; width < 64U; ++width) {
        const auto minimum = -(std::int64_t{1} << (width - 1U));
        const auto maximum = (std::int64_t{1} << (width - 1U)) - 1;
        if (value >= minimum && value <= maximum) {
            return width;
        }
    }
    return 64U;
}

}  // namespace

BitVector encode_size(std::uint64_t value) {
    std::uint8_t bytes = 1U;
    auto remaining = value;
    while (remaining > 0xffU && bytes < 8U) {
        remaining >>= 8U;
        ++bytes;
    }

    BitVector output;
    append_uint(output, static_cast<std::uint64_t>(bytes - 1U), 3U);
    append_uint(output, value, static_cast<std::uint8_t>(bytes * 8U));
    return output;
}

CodecResult<std::uint64_t> decode_size(BitCursor& cursor) {
    const auto start = cursor.position();
    auto probe = cursor;
    const auto header = probe.read_uint(3U);
    if (!header) {
        return std::unexpected(header.error());
    }
    const auto bytes = static_cast<std::uint8_t>(*header + 1U);
    const auto value = probe.read_uint(static_cast<std::uint8_t>(bytes * 8U));
    if (!value) {
        return std::unexpected(value.error());
    }
    if (bytes > 1U && *value < (std::uint64_t{1} << ((bytes - 1U) * 8U))) {
        return std::unexpected(error(CodecErrorCode::non_canonical, start,
                                     "size uses more bytes than necessary"));
    }
    cursor = probe;
    return *value;
}

BitVector encode_bit_string(const BitVector& value) {
    BitVector output = encode_size(value.size());
    output.append(value);
    return output;
}

CodecResult<BitVector> decode_bit_string(BitCursor& cursor) {
    auto probe = cursor;
    const auto size = decode_size(probe);
    if (!size) {
        return std::unexpected(size.error());
    }
    const auto value = probe.read_bits(*size);
    if (!value) {
        return std::unexpected(value.error());
    }
    cursor = probe;
    return *value;
}

BitVector encode_var_uint(std::uint64_t value) {
    const auto width = static_cast<std::uint8_t>(std::bit_width(value));
    BitVector output = encode_size(width);
    append_uint(output, value, width);
    return output;
}

CodecResult<std::uint64_t> decode_var_uint(BitCursor& cursor) {
    const auto start = cursor.position();
    auto probe = cursor;
    const auto width_value = decode_size(probe);
    if (!width_value) {
        return std::unexpected(width_value.error());
    }
    if (*width_value > 64U) {
        return std::unexpected(error(CodecErrorCode::invalid_width, start,
                                     "variable integer width exceeds 64 bits"));
    }
    const auto width = static_cast<std::uint8_t>(*width_value);
    const auto value = probe.read_uint(width);
    if (!value) {
        return std::unexpected(value.error());
    }
    if (width != 0U && (*value >> (width - 1U)) == 0U) {
        return std::unexpected(error(CodecErrorCode::non_canonical, start,
                                     "unsigned integer has a redundant leading zero"));
    }
    cursor = probe;
    return *value;
}

BitVector encode_var_int(std::int64_t value) {
    const auto width = minimal_signed_width(value);
    BitVector output = encode_size(value == 0 ? 0U : width);
    if (width != 0U) {
        auto raw = static_cast<std::uint64_t>(value);
        if (width < 64U) {
            raw &= (std::uint64_t{1} << width) - 1U;
        }
        append_uint(output, raw, width);
    }
    return output;
}

CodecResult<std::int64_t> decode_var_int(BitCursor& cursor) {
    const auto start = cursor.position();
    auto probe = cursor;
    const auto width_value = decode_size(probe);
    if (!width_value) {
        return std::unexpected(width_value.error());
    }
    if (*width_value > 64U) {
        return std::unexpected(error(CodecErrorCode::invalid_width, start,
                                     "signed integer width exceeds 64 bits"));
    }
    const auto width = static_cast<std::uint8_t>(*width_value);
    const auto raw_result = probe.read_uint(width);
    if (!raw_result) {
        return std::unexpected(raw_result.error());
    }

    std::int64_t value = 0;
    if (width != 0U) {
        auto raw = *raw_result;
        if (width < 64U && (raw & (std::uint64_t{1} << (width - 1U))) != 0U) {
            raw |= ~((std::uint64_t{1} << width) - 1U);
        }
        value = std::bit_cast<std::int64_t>(raw);
    }
    if ((value == 0 ? 0U : minimal_signed_width(value)) != width) {
        return std::unexpected(error(CodecErrorCode::non_canonical, start,
                                     "signed integer has a redundant sign bit"));
    }
    cursor = probe;
    return value;
}

CodecResult<VariableFloat> normalize_variable_float(VariableFloat value) {
    if (value.mantissa == 0) {
        value.exponent = 0;
        return value;
    }
    while ((value.mantissa % 2) == 0) {
        if (value.exponent == std::numeric_limits<std::int64_t>::max()) {
            return std::unexpected(error(CodecErrorCode::overflow, 0,
                                         "floating exponent overflow during normalization"));
        }
        value.mantissa /= 2;
        ++value.exponent;
    }
    return value;
}

CodecResult<BitVector> encode_var_float(VariableFloat value) {
    const auto normalized = normalize_variable_float(value);
    if (!normalized) {
        return std::unexpected(normalized.error());
    }
    BitVector output = encode_var_int(normalized->mantissa);
    output.append(encode_var_int(normalized->exponent));
    return output;
}

CodecResult<VariableFloat> decode_var_float(BitCursor& cursor) {
    const auto start = cursor.position();
    auto probe = cursor;
    const auto mantissa = decode_var_int(probe);
    if (!mantissa) {
        return std::unexpected(mantissa.error());
    }
    const auto exponent = decode_var_int(probe);
    if (!exponent) {
        return std::unexpected(exponent.error());
    }
    const VariableFloat value{*mantissa, *exponent};
    const auto normalized = normalize_variable_float(value);
    if (!normalized) {
        return std::unexpected(normalized.error());
    }
    if (*normalized != value) {
        return std::unexpected(error(CodecErrorCode::non_canonical, start,
                                     "floating mantissa is not normalized"));
    }
    cursor = probe;
    return value;
}

CodecResult<BitVector> encode_value(const Value& value) {
    BitVector output;
    if (const auto* integer = std::get_if<std::uint64_t>(&value)) {
        output.push_back(false);
        output.append(encode_var_uint(*integer));
        return output;
    }
    output.push_back(true);
    const auto floating = encode_var_float(std::get<VariableFloat>(value));
    if (!floating) {
        return std::unexpected(floating.error());
    }
    output.append(*floating);
    return output;
}

CodecResult<Value> decode_value(BitCursor& cursor) {
    auto probe = cursor;
    const auto type = probe.read_bit();
    if (!type) {
        return std::unexpected(type.error());
    }
    if (!*type) {
        const auto integer = decode_var_uint(probe);
        if (!integer) {
            return std::unexpected(integer.error());
        }
        cursor = probe;
        return Value{*integer};
    }
    const auto floating = decode_var_float(probe);
    if (!floating) {
        return std::unexpected(floating.error());
    }
    cursor = probe;
    return Value{*floating};
}

}  // namespace ubcm
