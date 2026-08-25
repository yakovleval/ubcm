#pragma once

#include "ubcm/bit_cursor.hpp"

#include <cstdint>
#include <variant>

namespace ubcm {

struct VariableFloat {
    std::int64_t mantissa{};
    std::int64_t exponent{};

    friend bool operator==(const VariableFloat&, const VariableFloat&) = default;
};

using Value = std::variant<std::uint64_t, VariableFloat>;

[[nodiscard]] BitVector encode_size(std::uint64_t value);
[[nodiscard]] CodecResult<std::uint64_t> decode_size(BitCursor& cursor);

[[nodiscard]] BitVector encode_bit_string(const BitVector& value);
[[nodiscard]] CodecResult<BitVector> decode_bit_string(BitCursor& cursor);

[[nodiscard]] BitVector encode_var_uint(std::uint64_t value);
[[nodiscard]] CodecResult<std::uint64_t> decode_var_uint(BitCursor& cursor);

[[nodiscard]] BitVector encode_var_int(std::int64_t value);
[[nodiscard]] CodecResult<std::int64_t> decode_var_int(BitCursor& cursor);

[[nodiscard]] CodecResult<VariableFloat> normalize_variable_float(VariableFloat value);
[[nodiscard]] CodecResult<BitVector> encode_var_float(VariableFloat value);
[[nodiscard]] CodecResult<VariableFloat> decode_var_float(BitCursor& cursor);

[[nodiscard]] CodecResult<BitVector> encode_value(const Value& value);
[[nodiscard]] CodecResult<Value> decode_value(BitCursor& cursor);

}  // namespace ubcm
