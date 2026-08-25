#pragma once

#include "ubcm/codec.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace ubcm {

enum class ArithmeticErrorCode {
    invalid_operation,
    invalid_operand,
    mathematical_error,
};

struct ArithmeticError {
    ArithmeticErrorCode code{};
    std::string message;
};

template <typename T>
using ArithmeticResult = std::expected<T, ArithmeticError>;

[[nodiscard]] ArithmeticResult<Value> evaluate_operation(
    std::uint8_t operation, std::span<const Value> operands);

}  // namespace ubcm
