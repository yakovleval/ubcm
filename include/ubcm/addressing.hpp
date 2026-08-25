#pragma once

#include "ubcm/registers.hpp"
#include "ubcm/codec.hpp"

#include <cstdint>
#include <variant>

namespace ubcm {

struct RegisterSelector {
    RegisterClass class_id{RegisterClass::global};
    BitVector name;

    friend bool operator==(const RegisterSelector&, const RegisterSelector&) = default;
};

struct EncodedRegisterAddress {
    RegisterSelector selector;
    std::uint64_t bit_offset{};
    friend bool operator==(const EncodedRegisterAddress&, const EncodedRegisterAddress&) = default;
};

struct DirectReference {
    EncodedRegisterAddress address;
    friend bool operator==(const DirectReference&, const DirectReference&) = default;
};

struct IndirectReference {
    EncodedRegisterAddress pointer;
    friend bool operator==(const IndirectReference&, const IndirectReference&) = default;
};

struct ForeignReference {
    std::uint64_t depth{};
    bool indirect{};
    BitVector local_name;
    std::uint64_t bit_offset{};

    friend bool operator==(const ForeignReference&, const ForeignReference&) = default;
};

using AddressReference = std::variant<std::uint64_t, IndirectReference,
                                      DirectReference, ForeignReference>;

struct SourceOperand {
    AddressReference reference;
    std::uint64_t bit_count{};
    bool immediate{};
};

struct DestinationOperand {
    AddressReference reference;
};

struct ResolutionContext {
    const NameResolver* procedure{};
    const NameResolver* superlocal{};
    const NameResolver* local{};
    const NameResolver* global{};
};

[[nodiscard]] CodecResult<RegisterSelector> decode_register_selector(BitCursor& cursor);
[[nodiscard]] BitVector encode_register_selector(const RegisterSelector& selector);

[[nodiscard]] CodecResult<EncodedRegisterAddress> decode_register_address(BitCursor& cursor);
[[nodiscard]] BitVector encode_register_address(const EncodedRegisterAddress& address);

[[nodiscard]] CodecResult<AddressReference> decode_reference(BitCursor& cursor);
[[nodiscard]] BitVector encode_reference(const AddressReference& reference);

[[nodiscard]] CodecResult<SourceOperand> decode_source(BitCursor& cursor);
[[nodiscard]] BitVector encode_source(const SourceOperand& source);

[[nodiscard]] CodecResult<DestinationOperand> decode_destination(BitCursor& cursor);
[[nodiscard]] BitVector encode_destination(const DestinationOperand& destination);

[[nodiscard]] RegisterResult<RegisterHandle> resolve_selector(
    const RegisterSelector& selector, const ResolutionContext& context);

}  // namespace ubcm
