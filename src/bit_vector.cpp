#include "ubcm/bit_vector.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace ubcm {
namespace {

CodecError error(CodecErrorCode code, std::uint64_t position, const char* message) {
    return CodecError{code, position, message};
}

}  // namespace

std::size_t BitVector::byte_count(std::uint64_t bit_size) {
    const auto bytes = bit_size / 8U + (bit_size % 8U != 0U ? 1U : 0U);
    if (bytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::length_error("bit vector is too large for this platform");
    }
    return static_cast<std::size_t>(bytes);
}

BitVector::BitVector(std::uint64_t bit_size, bool initial_value)
    : bytes_(byte_count(bit_size), initial_value ? 0xffU : 0U), bit_size_(bit_size) {
    clear_padding_bits();
}

CodecResult<BitVector> BitVector::from_bytes(std::vector<std::uint8_t> bytes,
                                             std::uint64_t bit_size) {
    try {
        if (bytes.size() != byte_count(bit_size)) {
            return std::unexpected(error(CodecErrorCode::invalid_data, 0,
                                          "byte count does not match bit size"));
        }
    } catch (const std::length_error&) {
        return std::unexpected(error(CodecErrorCode::overflow, 0, "bit vector is too large"));
    }

    if (bit_size % 8U != 0U && !bytes.empty()) {
        const auto valid_mask = static_cast<std::uint8_t>(0xffU << (8U - bit_size % 8U));
        if ((bytes.back() & static_cast<std::uint8_t>(~valid_mask)) != 0U) {
            return std::unexpected(error(CodecErrorCode::invalid_data, bit_size,
                                          "non-zero padding bits"));
        }
    }

    BitVector result;
    result.bytes_ = std::move(bytes);
    result.bit_size_ = bit_size;
    return result;
}

CodecResult<BitVector> BitVector::from_bit_string(std::string_view bits) {
    BitVector result;
    try {
        result.bytes_.reserve((bits.size() + 7U) / 8U);
    } catch (const std::length_error&) {
        return std::unexpected(error(CodecErrorCode::overflow, 0, "bit vector is too large"));
    }
    for (const char bit : bits) {
        if (bit != '0' && bit != '1') {
            return std::unexpected(error(CodecErrorCode::invalid_data, result.bit_size_,
                                          "bit string contains a character other than 0 or 1"));
        }
        result.push_back(bit == '1');
    }
    return result;
}

std::uint64_t BitVector::size() const noexcept { return bit_size_; }
bool BitVector::empty() const noexcept { return bit_size_ == 0; }
std::span<const std::uint8_t> BitVector::bytes() const noexcept { return bytes_; }

bool BitVector::at(std::uint64_t index) const {
    if (index >= bit_size_) {
        throw std::out_of_range("bit index out of range");
    }
    const auto byte = static_cast<std::size_t>(index / 8U);
    const auto mask = static_cast<std::uint8_t>(0x80U >> (index % 8U));
    return (bytes_[byte] & mask) != 0U;
}

void BitVector::set(std::uint64_t index, bool value) {
    if (index >= bit_size_) {
        throw std::out_of_range("bit index out of range");
    }
    const auto byte = static_cast<std::size_t>(index / 8U);
    const auto mask = static_cast<std::uint8_t>(0x80U >> (index % 8U));
    if (value) {
        bytes_[byte] |= mask;
    } else {
        bytes_[byte] &= static_cast<std::uint8_t>(~mask);
    }
}

void BitVector::resize(std::uint64_t new_size, bool fill_value) {
    const auto old_size = bit_size_;
    const auto new_byte_count = byte_count(new_size);
    bytes_.resize(new_byte_count, fill_value ? 0xffU : 0U);
    bit_size_ = new_size;
    if (new_size > old_size && fill_value) {
        for (std::uint64_t index = old_size; index < new_size; ++index) {
            set(index, true);
        }
    }
    clear_padding_bits();
}

void BitVector::push_back(bool value) {
    if (bit_size_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::length_error("bit vector size overflow");
    }
    resize(bit_size_ + 1U, false);
    set(bit_size_ - 1U, value);
}

void BitVector::append(const BitVector& other) {
    if (other.bit_size_ > std::numeric_limits<std::uint64_t>::max() - bit_size_) {
        throw std::length_error("bit vector size overflow");
    }
    const BitVector copy = other;
    const auto old_size = bit_size_;
    resize(bit_size_ + copy.bit_size_);
    for (std::uint64_t index = 0; index < copy.bit_size_; ++index) {
        set(old_size + index, copy.at(index));
    }
}

void BitVector::write(std::uint64_t offset, const BitVector& source) {
    if (offset > bit_size_ || source.bit_size_ > bit_size_ - offset) {
        throw std::out_of_range("bit write out of range");
    }
    const BitVector copy = source;
    for (std::uint64_t index = 0; index < copy.bit_size_; ++index) {
        set(offset + index, copy.at(index));
    }
}

BitVector BitVector::slice(std::uint64_t offset, std::uint64_t count) const {
    if (offset > bit_size_ || count > bit_size_ - offset) {
        throw std::out_of_range("bit slice out of range");
    }
    BitVector result(count);
    for (std::uint64_t index = 0; index < count; ++index) {
        result.set(index, at(offset + index));
    }
    return result;
}

std::string BitVector::to_bit_string() const {
    std::string result;
    result.reserve(static_cast<std::size_t>(bit_size_));
    for (std::uint64_t index = 0; index < bit_size_; ++index) {
        result.push_back(at(index) ? '1' : '0');
    }
    return result;
}

void BitVector::clear_padding_bits() noexcept {
    if (bit_size_ % 8U != 0U && !bytes_.empty()) {
        const auto valid_mask = static_cast<std::uint8_t>(0xffU << (8U - bit_size_ % 8U));
        bytes_.back() &= valid_mask;
    }
}

}  // namespace ubcm
