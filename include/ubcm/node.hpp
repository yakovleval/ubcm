#pragma once

#include "ubcm/runtime_reference.hpp"

#include <cstdint>

namespace ubcm {

inline constexpr std::uint64_t node_bit_size = 592;
inline constexpr std::uint64_t node_byte_size = node_bit_size / 8;

using NodeReference = RuntimeReference;

enum class NodeKind : std::uint8_t {
    builtin = 0,
    procedure_call = 1,
};

struct Node {
    NodeKind kind{NodeKind::builtin};
    std::uint8_t command{};
    RegisterHandle procedure{};
    NodeReference entry{};
    NodeReference next0{};
    NodeReference next1{};
    NodeReference superlocal_resolver{};

    friend bool operator==(const Node&, const Node&) = default;
};

[[nodiscard]] CodecResult<BitVector> encode_node(const Node& node);
[[nodiscard]] CodecResult<Node> decode_node(BitCursor& cursor);
[[nodiscard]] CodecResult<Node> decode_node(const BitVector& bits);

}  // namespace ubcm
