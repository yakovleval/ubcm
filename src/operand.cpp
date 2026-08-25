#include "ubcm/operand.hpp"

#include <utility>

namespace ubcm {
namespace {

constexpr std::uint8_t max_indirection_depth = 64;

OperandError error(OperandErrorCode code, const char* message) {
    return {code, message};
}

OperandError from_register_error(const RegisterError& source) {
    return {OperandErrorCode::register_access, source.message};
}

OperandError from_codec_error(const CodecError& source) {
    return {OperandErrorCode::decode_error, source.message};
}

BitVector immediate_bits(std::uint64_t value) {
    BitVector output;
    if (value == 0U) {
        return output;
    }
    std::uint8_t width = 0;
    for (auto remaining = value; remaining != 0U; remaining >>= 1U) {
        ++width;
    }
    for (int shift = width - 1; shift >= 0; --shift) {
        output.push_back(((value >> shift) & 1U) != 0U);
    }
    return output;
}

}  // namespace

OperandResolver::OperandResolver(RegisterBank& registers, RegisterHandle procedure,
                                 OperandResolvers resolvers)
    : registers_(&registers), procedure_(procedure), resolvers_(resolvers) {}

OperandResult<RegisterHandle> OperandResolver::resolve_selector(
    const RegisterSelector& selector) const {
    if (selector.class_id == RegisterClass::procedure) {
        return procedure_;
    }
    const NameResolver* resolver = nullptr;
    switch (selector.class_id) {
        case RegisterClass::superlocal:
            resolver = resolvers_.superlocal;
            break;
        case RegisterClass::local:
            resolver = resolvers_.local;
            break;
        case RegisterClass::global:
            resolver = resolvers_.global;
            break;
        case RegisterClass::procedure:
            break;
    }
    if (resolver == nullptr) {
        return std::unexpected(error(OperandErrorCode::unresolved_name,
                                     "register resolver is unavailable"));
    }
    auto handle = resolver->resolve(selector.name);
    if (!handle) {
        return std::unexpected(from_register_error(handle.error()));
    }
    return *handle;
}

OperandResult<RegisterAddress> OperandResolver::resolve_address(
    const EncodedRegisterAddress& address) const {
    auto handle = resolve_selector(address.selector);
    if (!handle) {
        return std::unexpected(handle.error());
    }
    return RegisterAddress{*handle, address.bit_offset};
}

OperandResult<ResolvedReference> OperandResolver::resolve_reference(
    const AddressReference& reference) const {
    return resolve_reference_impl(reference, 0);
}

OperandResult<ResolvedReference> OperandResolver::resolve_reference_impl(
    const AddressReference& reference, std::uint8_t depth) const {
    if (depth > max_indirection_depth) {
        return std::unexpected(error(OperandErrorCode::invalid_reference,
                                     "reference indirection exceeds 64 levels"));
    }
    if (const auto* immediate = std::get_if<std::uint64_t>(&reference)) {
        return ResolvedReference{*immediate, {}};
    }
    if (const auto* direct = std::get_if<DirectReference>(&reference)) {
        auto address = resolve_address(direct->address);
        if (!address) return std::unexpected(address.error());
        return ResolvedReference{std::nullopt, *address};
    }
    if (const auto* indirect = std::get_if<IndirectReference>(&reference)) {
        auto location = resolve_address(indirect->pointer);
        if (!location) return std::unexpected(location.error());
        auto size = registers_->size(location->handle);
        if (!size) return std::unexpected(from_register_error(size.error()));
        if (location->bit_offset > *size) {
            return std::unexpected(error(OperandErrorCode::invalid_reference,
                                         "indirect reference address is out of range"));
        }
        auto bits = registers_->read(*location, *size - location->bit_offset);
        if (!bits) return std::unexpected(from_register_error(bits.error()));
        BitCursor cursor(*bits);
        auto nested = decode_reference(cursor);
        if (!nested) return std::unexpected(from_codec_error(nested.error()));
        return resolve_reference_impl(*nested, depth + 1U);
    }

    const auto& foreign = std::get<ForeignReference>(reference);
    if (foreign.depth != 0U) {
        return std::unexpected(error(OperandErrorCode::unsupported_reference,
                                     "foreign activation references are not active yet"));
    }
    if (resolvers_.local == nullptr) {
        return std::unexpected(error(OperandErrorCode::unresolved_name,
                                     "local register resolver is unavailable"));
    }
    auto handle = resolvers_.local->resolve(foreign.local_name);
    if (!handle) return std::unexpected(from_register_error(handle.error()));
    const auto address = RegisterAddress{*handle, foreign.bit_offset};
    if (!foreign.indirect) {
        return ResolvedReference{std::nullopt, address};
    }
    auto size = registers_->size(address.handle);
    if (!size) return std::unexpected(from_register_error(size.error()));
    if (address.bit_offset > *size) {
        return std::unexpected(error(OperandErrorCode::invalid_reference,
                                     "foreign indirect reference is out of range"));
    }
    auto bits = registers_->read(address, *size - address.bit_offset);
    if (!bits) return std::unexpected(from_register_error(bits.error()));
    BitCursor cursor(*bits);
    auto nested = decode_reference(cursor);
    if (!nested) return std::unexpected(from_codec_error(nested.error()));
    return resolve_reference_impl(*nested, depth + 1U);
}

OperandResult<BitVector> OperandResolver::read_source_bits(
    const SourceOperand& source) const {
    auto reference = resolve_reference(source.reference);
    if (!reference) return std::unexpected(reference.error());
    if (reference->immediate) {
        return immediate_bits(*reference->immediate);
    }
    auto bits = registers_->read(reference->address, source.bit_count);
    if (!bits) return std::unexpected(from_register_error(bits.error()));
    return *bits;
}

OperandResult<std::uint64_t> OperandResolver::read_source_uint(
    const SourceOperand& source) const {
    auto value = read_source_value(source);
    if (!value) return std::unexpected(value.error());
    if (!std::holds_alternative<std::uint64_t>(*value)) {
        return std::unexpected(error(OperandErrorCode::invalid_value,
                                     "expected an unsigned integer Value"));
    }
    return std::get<std::uint64_t>(*value);
}

OperandResult<Value> OperandResolver::read_source_value(
    const SourceOperand& source) const {
    auto reference = resolve_reference(source.reference);
    if (!reference) return std::unexpected(reference.error());
    if (reference->immediate) {
        return Value{*reference->immediate};
    }
    auto bits = registers_->read(reference->address, source.bit_count);
    if (!bits) return std::unexpected(from_register_error(bits.error()));
    BitCursor cursor(*bits);
    auto value = decode_value(cursor);
    if (!value) return std::unexpected(from_codec_error(value.error()));
    while (cursor.remaining() != 0U) {
        auto padding = cursor.read_bit();
        if (!padding || *padding) {
            return std::unexpected(error(OperandErrorCode::invalid_value,
                                         "numeric source has non-zero trailing bits"));
        }
    }
    return *value;
}

OperandResult<RegisterAddress> OperandResolver::resolve_destination(
    const DestinationOperand& destination) const {
    auto reference = resolve_reference(destination.reference);
    if (!reference) return std::unexpected(reference.error());
    if (reference->immediate) {
        return std::unexpected(error(OperandErrorCode::invalid_reference,
                                     "destination cannot be immediate"));
    }
    return reference->address;
}

OperandResult<void> OperandResolver::validate_destination(
    RegisterAddress address, std::uint64_t bit_count) const {
    if (address.handle.class_id == RegisterClass::procedure) {
        return std::unexpected(error(OperandErrorCode::immutable_destination,
                                     "procedure registers cannot be destinations"));
    }
    auto immutable = registers_->is_immutable(address.handle);
    if (!immutable) return std::unexpected(from_register_error(immutable.error()));
    if (*immutable) {
        return std::unexpected(error(OperandErrorCode::immutable_destination,
                                     "destination register is immutable"));
    }
    auto size = registers_->size(address.handle);
    if (!size) return std::unexpected(from_register_error(size.error()));
    if (address.bit_offset > *size || bit_count > *size - address.bit_offset) {
        return std::unexpected(error(OperandErrorCode::insufficient_destination_capacity,
                                     "destination range is too small"));
    }
    return {};
}

OperandResult<void> OperandResolver::write_destination(
    const DestinationOperand& destination, const BitVector& value) const {
    auto address = resolve_destination(destination);
    if (!address) return std::unexpected(address.error());
    auto size = registers_->size(address->handle);
    if (!size) return std::unexpected(from_register_error(size.error()));
    if (address->bit_offset > *size) {
        return std::unexpected(error(OperandErrorCode::insufficient_destination_capacity,
                                     "destination offset is out of range"));
    }
    const auto capacity = *size - address->bit_offset;
    if (auto valid = validate_destination(*address, value.size()); !valid) {
        return std::unexpected(valid.error());
    }
    BitVector stored = value;
    stored.resize(capacity);
    auto written = registers_->write(*address, stored);
    if (!written) return std::unexpected(from_register_error(written.error()));
    return {};
}

}  // namespace ubcm
