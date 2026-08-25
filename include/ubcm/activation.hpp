#pragma once

#include "ubcm/node.hpp"

#include <cstdint>

namespace ubcm {

inline constexpr std::uint16_t activation_magic = 0x5541;
inline constexpr std::uint8_t activation_version = 1;
inline constexpr std::uint64_t activation_bit_size = 744;
inline constexpr std::uint64_t activation_byte_size = activation_bit_size / 8;

enum class PrefixKind : std::uint8_t {
    none = 0,
    read = 1,
    modify = 2,
    condition = 3,
};

struct PrefixState {
    PrefixKind kind{PrefixKind::none};
    std::uint64_t depth{};
    bool condition{};

    friend bool operator==(const PrefixState&, const PrefixState&) = default;
};

struct ActivationRecord {
    std::uint8_t flags{};
    RegisterHandle procedure{RegisterClass::procedure, 0};
    std::uint64_t procedure_position{};
    RuntimeReference network_state{};
    NodeReference local_resolver{};
    RuntimeReference previous_activation{};
    RegisterHandle result_register{RegisterClass::local, 0};
    PrefixState prefix{};

    friend bool operator==(const ActivationRecord&, const ActivationRecord&) = default;
};

[[nodiscard]] CodecResult<BitVector> encode_activation(const ActivationRecord& activation);
[[nodiscard]] CodecResult<ActivationRecord> decode_activation(BitCursor& cursor);
[[nodiscard]] CodecResult<ActivationRecord> decode_activation(const BitVector& bits);

}  // namespace ubcm
