#include "ubcm/ubcm.hpp"

#include <bit>
#include <iostream>
#include <random>
#include <stdexcept>

namespace {

void expect(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

template <typename Decode, typename Encode>
void check(const ubcm::BitVector& bits, std::uint64_t start, Decode decode, Encode encode) {
    ubcm::BitCursor cursor(bits, start);
    const auto value = decode(cursor);
    if (!value) {
        expect(cursor.position() == start, "failed decoder consumed input");
        return;
    }
    ubcm::CodecResult<ubcm::BitVector> encoded = encode(*value);
    expect(encoded && *encoded == bits.slice(start, cursor.position() - start),
           "successful decoder did not reproduce canonical input");
}

void fuzz() {
    std::mt19937_64 random(0x5542434d);
    for (std::uint64_t value : {0ULL, 1ULL, 127ULL, 128ULL, 255ULL, ~0ULL}) {
        auto bits = ubcm::encode_var_int(std::bit_cast<std::int64_t>(value));
        ubcm::BitCursor cursor(bits);
        expect(ubcm::decode_var_int(cursor) == std::bit_cast<std::int64_t>(value) &&
                   cursor.remaining() == 0,
               "signed integer round trip left trailing bits");
    }
    for (int iteration = 0; iteration < 4000; ++iteration) {
        ubcm::BitVector bits(random() % 800);
        for (std::uint64_t i = 0; i < bits.size(); ++i) bits.set(i, random() & 1);
        const auto start = bits.empty() ? 0 : random() % (bits.size() + 1);
        check(bits, start, ubcm::decode_size, ubcm::encode_size);
        check(bits, start, ubcm::decode_bit_string, ubcm::encode_bit_string);
        check(bits, start, ubcm::decode_var_uint, ubcm::encode_var_uint);
        check(bits, start, ubcm::decode_var_int, ubcm::encode_var_int);
        check(bits, start, ubcm::decode_var_float, ubcm::encode_var_float);
        check(bits, start, ubcm::decode_value, ubcm::encode_value);
        check(bits, start, ubcm::decode_register_selector, ubcm::encode_register_selector);
        check(bits, start, ubcm::decode_reference, ubcm::encode_reference);
        check(bits, start, ubcm::decode_source, ubcm::encode_source);
        check(bits, start, ubcm::decode_destination, ubcm::encode_destination);
        check(bits, start, ubcm::decode_runtime_reference, ubcm::encode_runtime_reference);
        check(bits, start, [](ubcm::BitCursor& c) { return ubcm::decode_node(c); },
              ubcm::encode_node);
        check(bits, start, [](ubcm::BitCursor& c) { return ubcm::decode_activation(c); },
              ubcm::encode_activation);
        for (std::uint8_t command = 0; command < 16; ++command) {
            ubcm::BitCursor cursor(bits, start);
            const auto decoded = ubcm::decode_builtin(command, cursor);
            expect(decoded || cursor.position() == start,
                   "failed builtin decoder consumed input");
        }
        const auto program = ubcm::decode_program(bits);
        if (program) {
            auto encoded = ubcm::encode_program(*program);
            expect(encoded && *encoded == bits, "container round trip changed bits");
        }
    }
    // Mutate a valid structured input so that header-only random rejection
    // does not hide bugs deeper inside container records and nodes.
    ubcm::Node finish;
    finish.command = static_cast<std::uint8_t>(ubcm::BuiltinCommand::finish_call);
    ubcm::ProgramImage image{{ubcm::RegisterClass::global, 0},
        ubcm::RuntimeReference::at({ubcm::RegisterClass::global, 1}, 0),
        {{0, 1, {}, {}},
         {1, 2, *ubcm::BitVector::from_bit_string("1"), *ubcm::encode_node(finish)}}};
    const auto seed = *ubcm::encode_program(image);
    for (std::uint64_t i = 0; i < seed.size(); ++i) {
        auto bits = seed;
        bits.set(i, !bits.at(i));
        const auto decoded = ubcm::decode_program(bits);
        if (decoded) {
            const auto encoded = ubcm::encode_program(*decoded);
            expect(encoded && *encoded == bits, "mutated container is not canonical");
        }
    }
}

} // namespace

int main() {
    try {
        fuzz();
        std::cout << "deterministic codec fuzz tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
