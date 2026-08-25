#include "ubcm/arithmetic.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace ubcm {
namespace {

ArithmeticError error(ArithmeticErrorCode code, const char* message) {
    return {code, message};
}

bool is_integer(const Value& value) {
    return std::holds_alternative<std::uint64_t>(value);
}

std::uint64_t as_integer(const Value& value) {
    return std::get<std::uint64_t>(value);
}

double as_double(const Value& value) {
    if (const auto* integer = std::get_if<std::uint64_t>(&value)) {
        return static_cast<double>(*integer);
    }
    const auto floating = std::get<VariableFloat>(value);
    if (floating.exponent > std::numeric_limits<int>::max()) {
        return floating.mantissa < 0 ? -std::numeric_limits<double>::infinity()
                                     : std::numeric_limits<double>::infinity();
    }
    if (floating.exponent < std::numeric_limits<int>::min()) {
        return 0.0;
    }
    return std::ldexp(static_cast<double>(floating.mantissa),
                      static_cast<int>(floating.exponent));
}

ArithmeticResult<Value> from_double(double value) {
    if (!std::isfinite(value)) {
        return std::unexpected(error(ArithmeticErrorCode::mathematical_error,
                                     "mathematical result is not finite"));
    }
    if (value == 0.0) {
        return Value{VariableFloat{}};
    }
    int exponent = 0;
    const auto fraction = std::frexp(value, &exponent);
    const auto scaled = std::ldexp(fraction, 53);
    const auto mantissa = static_cast<std::int64_t>(std::nearbyint(scaled));
    auto normalized = normalize_variable_float(
        {mantissa, static_cast<std::int64_t>(exponent) - 53});
    if (!normalized) {
        return std::unexpected(error(ArithmeticErrorCode::mathematical_error,
                                     normalized.error().message.c_str()));
    }
    return Value{*normalized};
}

std::uint64_t modular_power(std::uint64_t base, std::uint64_t exponent) {
    std::uint64_t result = 1;
    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) {
            result *= base;
        }
        base *= base;
        exponent >>= 1U;
    }
    return result;
}

ArithmeticResult<Value> numeric_result(double value) {
    return from_double(value);
}

ArithmeticResult<Value> boolean_result(bool value) {
    return Value{static_cast<std::uint64_t>(value)};
}

ArithmeticResult<Value> unary_float_result(std::uint8_t operation, double operand) {
    double result = 0.0;
    switch (operation) {
        case 16: result = std::sin(operand); break;
        case 17:
            if (operand < -1.0 || operand > 1.0) {
                return std::unexpected(error(ArithmeticErrorCode::mathematical_error,
                                             "asin domain error"));
            }
            result = std::asin(operand);
            break;
        case 18: result = std::cos(operand); break;
        case 19:
            if (operand < -1.0 || operand > 1.0) {
                return std::unexpected(error(ArithmeticErrorCode::mathematical_error,
                                             "acos domain error"));
            }
            result = std::acos(operand);
            break;
        case 20: result = std::tan(operand); break;
        case 21: result = std::atan(operand); break;
        case 24: result = std::sinh(operand); break;
        case 25: result = std::asinh(operand); break;
        case 26: result = std::cosh(operand); break;
        case 27:
            if (operand < 1.0) {
                return std::unexpected(error(ArithmeticErrorCode::mathematical_error,
                                             "acosh domain error"));
            }
            result = std::acosh(operand);
            break;
        case 28: result = std::tanh(operand); break;
        case 29:
            if (operand <= -1.0 || operand >= 1.0) {
                return std::unexpected(error(ArithmeticErrorCode::mathematical_error,
                                             "atanh domain error"));
            }
            result = std::atanh(operand);
            break;
        case 30:
            if (operand <= 0.0) {
                return std::unexpected(error(ArithmeticErrorCode::mathematical_error,
                                             "ln domain error"));
            }
            result = std::log(operand);
            break;
        case 31:
            if (operand <= 0.0) {
                return std::unexpected(error(ArithmeticErrorCode::mathematical_error,
                                             "lg domain error"));
            }
            result = std::log10(operand);
            break;
        default:
            return std::unexpected(error(ArithmeticErrorCode::invalid_operation,
                                         "unknown unary operation"));
    }
    return numeric_result(result);
}

}  // namespace

ArithmeticResult<Value> evaluate_operation(std::uint8_t operation,
                                           std::span<const Value> operands) {
    if (operation > 31U) {
        return std::unexpected(error(ArithmeticErrorCode::invalid_operation,
                                     "operation code is out of range"));
    }
    const auto expected_count = operation < 16U ? 2U : 1U;
    if (operands.size() != expected_count) {
        return std::unexpected(error(ArithmeticErrorCode::invalid_operand,
                                     "operation has an invalid operand count"));
    }

    const auto first_integer = is_integer(operands[0]);
    const auto left = as_double(operands[0]);
    if (!std::isfinite(left)) {
        return std::unexpected(error(ArithmeticErrorCode::mathematical_error,
                                     "operand is not finite after conversion to double"));
    }
    if (operation >= 16U) {
        if (operation == 22U) {
            return boolean_result(left == 0.0);
        }
        if (operation == 23U) {
            if (!first_integer) {
                return std::unexpected(error(ArithmeticErrorCode::invalid_operand,
                                             "bitwise not requires an integer"));
            }
            return Value{~as_integer(operands[0])};
        }
        return unary_float_result(operation, left);
    }

    const auto second_integer = is_integer(operands[1]);
    const auto both_integer = first_integer && second_integer;
    const auto right = as_double(operands[1]);
    if (!std::isfinite(right)) {
        return std::unexpected(error(ArithmeticErrorCode::mathematical_error,
                                     "operand is not finite after conversion to double"));
    }

    switch (operation) {
        case 0:
            return both_integer ? ArithmeticResult<Value>{Value{as_integer(operands[0]) + as_integer(operands[1])}}
                                : numeric_result(left + right);
        case 1:
            return both_integer ? ArithmeticResult<Value>{Value{as_integer(operands[0]) - as_integer(operands[1])}}
                                : numeric_result(left - right);
        case 2:
            return both_integer ? ArithmeticResult<Value>{Value{as_integer(operands[0]) * as_integer(operands[1])}}
                                : numeric_result(left * right);
        case 3:
            if (both_integer) {
                if (as_integer(operands[1]) == 0U) {
                    return std::unexpected(error(ArithmeticErrorCode::mathematical_error,
                                                 "integer division by zero"));
                }
                return Value{as_integer(operands[0]) / as_integer(operands[1])};
            }
            if (right == 0.0) {
                return std::unexpected(error(ArithmeticErrorCode::mathematical_error,
                                             "floating division by zero"));
            }
            return numeric_result(left / right);
        case 4:
            if (both_integer) {
                if (as_integer(operands[1]) == 0U) {
                    return std::unexpected(error(ArithmeticErrorCode::mathematical_error,
                                                 "integer remainder by zero"));
                }
                return Value{as_integer(operands[0]) % as_integer(operands[1])};
            }
            if (right == 0.0) {
                return std::unexpected(error(ArithmeticErrorCode::mathematical_error,
                                             "floating remainder by zero"));
            }
            return numeric_result(std::fmod(left, right));
        case 5:
            return both_integer ? ArithmeticResult<Value>{Value{modular_power(as_integer(operands[0]), as_integer(operands[1]))}}
                                : numeric_result(std::pow(left, right));
        case 6:
            return boolean_result(left != 0.0 && right != 0.0);
        case 7:
            return boolean_result(left != 0.0 || right != 0.0);
        case 8:
            return both_integer
                       ? boolean_result(as_integer(operands[0]) == as_integer(operands[1]))
                       : boolean_result(left == right);
        case 9:
            return both_integer
                       ? boolean_result(as_integer(operands[0]) != as_integer(operands[1]))
                       : boolean_result(left != right);
        case 10:
            return both_integer
                       ? boolean_result(static_cast<std::int64_t>(as_integer(operands[0])) >
                                        static_cast<std::int64_t>(as_integer(operands[1])))
                       : boolean_result(left > right);
        case 11:
            return both_integer
                       ? boolean_result(static_cast<std::int64_t>(as_integer(operands[0])) >=
                                        static_cast<std::int64_t>(as_integer(operands[1])))
                       : boolean_result(left >= right);
        case 12:
            return both_integer
                       ? boolean_result(static_cast<std::int64_t>(as_integer(operands[0])) <
                                        static_cast<std::int64_t>(as_integer(operands[1])))
                       : boolean_result(left < right);
        case 13:
            return both_integer
                       ? boolean_result(static_cast<std::int64_t>(as_integer(operands[0])) <=
                                        static_cast<std::int64_t>(as_integer(operands[1])))
                       : boolean_result(left <= right);
        case 14:
            if (!both_integer) {
                return std::unexpected(error(ArithmeticErrorCode::invalid_operand,
                                             "bitwise and requires integers"));
            }
            return Value{as_integer(operands[0]) & as_integer(operands[1])};
        case 15:
            if (!both_integer) {
                return std::unexpected(error(ArithmeticErrorCode::invalid_operand,
                                             "bitwise or requires integers"));
            }
            return Value{as_integer(operands[0]) | as_integer(operands[1])};
        default:
            return std::unexpected(error(ArithmeticErrorCode::invalid_operation,
                                         "unknown binary operation"));
    }
}

}  // namespace ubcm
