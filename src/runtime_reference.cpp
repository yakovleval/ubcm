#include "ubcm/runtime_reference.hpp"

#include <limits>

namespace ubcm {
namespace {

constexpr std::uint64_t id_mask = (std::uint64_t{1} << 62U) - 1U;

CodecError error(CodecErrorCode code, std::uint64_t position, const char* message) {
    return {code, position, message};
}

BitVector encode_u64(std::uint64_t value) {
    BitVector bits;
    for (int shift = 63; shift >= 0; --shift) {
        bits.push_back(((value >> shift) & 1U) != 0U);
    }
    return bits;
}

CodecResult<RegisterHandle> handle_from_raw(std::uint64_t raw, std::uint64_t position) {
    const auto class_id = static_cast<RegisterClass>(raw >> 62U);
    const auto id = raw & id_mask;
    if (class_id == RegisterClass::procedure && id != 0U) {
        return std::unexpected(error(CodecErrorCode::invalid_data, position,
                                     "procedure handle id must be zero"));
    }
    return RegisterHandle{class_id, id};
}

}  // namespace

RuntimeReference RuntimeReference::null_reference() noexcept { return {}; }

RuntimeReference RuntimeReference::at(RegisterHandle handle,
                                      std::uint64_t bit_offset) noexcept {
    return {handle, bit_offset, false};
}

CodecResult<BitVector> encode_register_handle(RegisterHandle handle) {
    if (handle.id > id_mask) {
        return std::unexpected(error(CodecErrorCode::overflow, 0,
                                     "register id does not fit in 62 bits"));
    }
    if (handle.class_id == RegisterClass::procedure && handle.id != 0U) {
        return std::unexpected(error(CodecErrorCode::invalid_data, 0,
                                     "procedure handle id must be zero"));
    }
    const auto raw = (static_cast<std::uint64_t>(handle.class_id) << 62U) | handle.id;
    return encode_u64(raw);
}

CodecResult<RegisterHandle> decode_register_handle(BitCursor& cursor) {
    const auto start = cursor.position();
    auto raw = cursor.read_uint(64);
    if (!raw) {
        return std::unexpected(raw.error());
    }
    auto handle = handle_from_raw(*raw, start);
    if (!handle) {
        (void)cursor.seek(start);
    }
    return handle;
}

CodecResult<BitVector> encode_runtime_reference(const RuntimeReference& reference) {
    if (reference.null) {
        return BitVector(runtime_reference_bit_size, true);
    }
    auto handle = encode_register_handle(reference.handle);
    if (!handle) {
        return std::unexpected(handle.error());
    }
    BitVector bits = std::move(*handle);
    bits.append(encode_u64(reference.bit_offset));
    if (bits.size() == runtime_reference_bit_size &&
        bits == BitVector(runtime_reference_bit_size, true)) {
        return std::unexpected(error(CodecErrorCode::invalid_data, 0,
                                     "all-ones runtime reference is reserved for null"));
    }
    return bits;
}

CodecResult<RuntimeReference> decode_runtime_reference(BitCursor& cursor) {
    const auto start = cursor.position();
    auto raw_handle = cursor.read_uint(64);
    auto offset = cursor.read_uint(64);
    if (!raw_handle || !offset) {
        (void)cursor.seek(start);
        return std::unexpected(!raw_handle ? raw_handle.error() : offset.error());
    }
    if (*raw_handle == std::numeric_limits<std::uint64_t>::max() &&
        *offset == std::numeric_limits<std::uint64_t>::max()) {
        return RuntimeReference::null_reference();
    }
    auto handle = handle_from_raw(*raw_handle, start);
    if (!handle) {
        (void)cursor.seek(start);
        return std::unexpected(handle.error());
    }
    return RuntimeReference::at(*handle, *offset);
}

}  // namespace ubcm
