#pragma once

#include "ubcm/addressing.hpp"

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace ubcm {

enum class OperandErrorCode {
    register_access,
    decode_error,
    unresolved_name,
    invalid_reference,
    unsupported_reference,
    invalid_value,
    immutable_destination,
    insufficient_destination_capacity,
};

struct OperandError {
    OperandErrorCode code{};
    std::string message;
};

template <typename T>
using OperandResult = std::expected<T, OperandError>;

struct OperandResolvers {
    const NameResolver* superlocal{};
    const NameResolver* local{};
    const NameResolver* global{};
};

struct ResolvedReference {
    std::optional<std::uint64_t> immediate;
    RegisterAddress address{};
};

class OperandResolver {
public:
    OperandResolver(RegisterBank& registers, RegisterHandle procedure,
                    OperandResolvers resolvers = {});

    [[nodiscard]] OperandResult<RegisterAddress> resolve_address(
        const EncodedRegisterAddress& address) const;
    [[nodiscard]] OperandResult<ResolvedReference> resolve_reference(
        const AddressReference& reference) const;
    [[nodiscard]] OperandResult<BitVector> read_source_bits(
        const SourceOperand& source) const;
    [[nodiscard]] OperandResult<Value> read_source_value(
        const SourceOperand& source) const;
    [[nodiscard]] OperandResult<std::uint64_t> read_source_uint(
        const SourceOperand& source) const;
    [[nodiscard]] OperandResult<RegisterAddress> resolve_destination(
        const DestinationOperand& destination) const;
    [[nodiscard]] OperandResult<void> validate_destination(
        RegisterAddress address, std::uint64_t bit_count) const;
    OperandResult<void> write_destination(const DestinationOperand& destination,
                                          const BitVector& value) const;

private:
    [[nodiscard]] OperandResult<ResolvedReference> resolve_reference_impl(
        const AddressReference& reference, std::uint8_t depth) const;
    [[nodiscard]] OperandResult<RegisterHandle> resolve_selector(
        const RegisterSelector& selector) const;

    RegisterBank* registers_;
    RegisterHandle procedure_;
    OperandResolvers resolvers_;
};

}  // namespace ubcm
