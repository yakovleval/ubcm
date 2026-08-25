#pragma once

#include "ubcm/bit_vector.hpp"

#include <cstdint>

namespace ubcm {

class BitCursor {
public:
    explicit BitCursor(const BitVector& bits, std::uint64_t position = 0);

    [[nodiscard]] const BitVector& bits() const noexcept;
    [[nodiscard]] std::uint64_t position() const noexcept;
    [[nodiscard]] std::uint64_t remaining() const noexcept;

    [[nodiscard]] CodecResult<void> seek(std::uint64_t position);
    [[nodiscard]] CodecResult<bool> read_bit();
    [[nodiscard]] CodecResult<BitVector> read_bits(std::uint64_t count);
    [[nodiscard]] CodecResult<std::uint64_t> read_uint(std::uint8_t width);

private:
    const BitVector* bits_;
    std::uint64_t position_;
};

}  // namespace ubcm
