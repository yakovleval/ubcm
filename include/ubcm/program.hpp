#pragma once

#include "ubcm/node.hpp"
#include "ubcm/registers.hpp"

#include <vector>

namespace ubcm {

struct ProgramRegister {
    std::uint64_t id{};
    std::uint8_t flags{}; // bit 0: immutable procedure, bit 1: nodes
    BitVector name;
    BitVector contents;

    friend bool operator==(const ProgramRegister&, const ProgramRegister&) = default;
};

struct ProgramImage {
    RegisterHandle procedure;
    NodeReference entry;
    std::vector<ProgramRegister> registers;

    friend bool operator==(const ProgramImage&, const ProgramImage&) = default;
};

struct LoadedProgram {
    RegisterBank registers;
    NameResolver names;
    RegisterHandle procedure;
    NodeReference entry;
};

[[nodiscard]] CodecResult<void> validate_program(const ProgramImage& program);
[[nodiscard]] CodecResult<BitVector> encode_program(const ProgramImage& program);
[[nodiscard]] CodecResult<ProgramImage> decode_program(const BitVector& bits);
[[nodiscard]] CodecResult<LoadedProgram> load_program(const ProgramImage& program);

}  // namespace ubcm
