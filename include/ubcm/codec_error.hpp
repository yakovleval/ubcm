#pragma once

#include <cstdint>
#include <expected>
#include <string>

namespace ubcm {

enum class CodecErrorCode {
    unexpected_end,
    invalid_width,
    non_canonical,
    invalid_type,
    overflow,
    invalid_data,
};

struct CodecError {
    CodecErrorCode code{};
    std::uint64_t bit_position{};
    std::string message;
};

template <typename T>
using CodecResult = std::expected<T, CodecError>;

}  // namespace ubcm
