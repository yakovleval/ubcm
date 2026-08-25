#pragma once

#include "ubcm/bit_cursor.hpp"
#include "ubcm/registers.hpp"

#include <cstdint>

namespace ubcm {

inline constexpr std::uint64_t register_handle_bit_size = 64;
inline constexpr std::uint64_t runtime_reference_bit_size = 128;

struct RuntimeReference {
    RegisterHandle handle{};
    std::uint64_t bit_offset{};
    bool null{true};

    [[nodiscard]] static RuntimeReference null_reference() noexcept;
    [[nodiscard]] static RuntimeReference at(RegisterHandle handle,
                                             std::uint64_t bit_offset) noexcept;
    friend bool operator==(const RuntimeReference&, const RuntimeReference&) = default;
};

[[nodiscard]] CodecResult<BitVector> encode_register_handle(RegisterHandle handle);
[[nodiscard]] CodecResult<RegisterHandle> decode_register_handle(BitCursor& cursor);
[[nodiscard]] CodecResult<BitVector> encode_runtime_reference(
    const RuntimeReference& reference);
[[nodiscard]] CodecResult<RuntimeReference> decode_runtime_reference(BitCursor& cursor);

}  // namespace ubcm
