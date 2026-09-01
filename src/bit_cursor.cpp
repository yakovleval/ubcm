#include "ubcm/bit_cursor.hpp"

#include <new>
#include <stdexcept>

namespace ubcm {
namespace {

CodecError error(CodecErrorCode code, std::uint64_t position, const char* message) {
    return CodecError{code, position, message};
}

}  // namespace

BitCursor::BitCursor(const BitVector& bits, std::uint64_t position)
    : bits_(&bits), position_(position) {
    if (position > bits.size()) {
        throw std::out_of_range("cursor position out of range");
    }
}

const BitVector& BitCursor::bits() const noexcept { return *bits_; }
std::uint64_t BitCursor::position() const noexcept { return position_; }
std::uint64_t BitCursor::remaining() const noexcept { return bits_->size() - position_; }

CodecResult<void> BitCursor::seek(std::uint64_t position) {
    if (position > bits_->size()) {
        return std::unexpected(error(CodecErrorCode::unexpected_end, position,
                                     "cursor position out of range"));
    }
    position_ = position;
    return {};
}

CodecResult<bool> BitCursor::read_bit() {
    if (position_ >= bits_->size()) {
        return std::unexpected(error(CodecErrorCode::unexpected_end, position_,
                                     "cannot read past end of bit stream"));
    }
    const bool value = bits_->at(position_);
    ++position_;
    return value;
}

CodecResult<BitVector> BitCursor::read_bits(std::uint64_t count) {
    if (count > remaining()) {
        return std::unexpected(error(CodecErrorCode::unexpected_end, position_,
                                     "not enough bits in stream"));
    }
    try {
        BitVector result(count);
        for (std::uint64_t index = 0; index < count; ++index) {
            result.set(index, bits_->at(position_ + index));
        }
        position_ += count;
        return result;
    } catch (const std::bad_alloc&) {
        return std::unexpected(error(CodecErrorCode::overflow, position_,
                                     "not enough memory for bit range"));
    } catch (const std::length_error&) {
        return std::unexpected(error(CodecErrorCode::overflow, position_,
                                     "bit range is too large for this platform"));
    }
}

CodecResult<std::uint64_t> BitCursor::read_uint(std::uint8_t width) {
    if (width > 64U) {
        return std::unexpected(error(CodecErrorCode::invalid_width, position_,
                                     "unsigned integer width exceeds 64 bits"));
    }
    if (width > remaining()) {
        return std::unexpected(error(CodecErrorCode::unexpected_end, position_,
                                     "not enough bits for unsigned integer"));
    }
    std::uint64_t value = 0;
    for (std::uint8_t index = 0; index < width; ++index) {
        value = (value << 1U) | (bits_->at(position_ + index) ? 1U : 0U);
    }
    position_ += width;
    return value;
}

}  // namespace ubcm
