#pragma once

#include "ubcm/codec_error.hpp"

#include <cstdint>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ubcm {

class BitVector {
public:
    BitVector() = default;
    explicit BitVector(std::uint64_t bit_size, bool initial_value = false);

    [[nodiscard]] static CodecResult<BitVector> from_bytes(
        std::vector<std::uint8_t> bytes, std::uint64_t bit_size);
    [[nodiscard]] static CodecResult<BitVector> from_bit_string(std::string_view bits);

    [[nodiscard]] std::uint64_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept;

    [[nodiscard]] bool at(std::uint64_t index) const;
    void set(std::uint64_t index, bool value);
    void resize(std::uint64_t new_size, bool fill_value = false);
    void push_back(bool value);
    void append(const BitVector& other);
    void write(std::uint64_t offset, const BitVector& source);

    [[nodiscard]] BitVector slice(std::uint64_t offset, std::uint64_t count) const;
    [[nodiscard]] std::string to_bit_string() const;

    friend bool operator==(const BitVector&, const BitVector&) = default;

private:
    [[nodiscard]] static std::size_t byte_count(std::uint64_t bit_size);
    void clear_padding_bits() noexcept;

    std::vector<std::uint8_t> bytes_;
    std::uint64_t bit_size_{};
};

}  // namespace ubcm
