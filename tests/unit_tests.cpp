#include "ubcm/ubcm.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <functional>

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void test_bit_vector() {
    const auto parsed = ubcm::BitVector::from_bit_string("101100001");
    expect(parsed.has_value(), "bit string must parse");
    expect(parsed->size() == 9, "bit string size");
    const auto stored = parsed->bytes();
    expect(stored.size() == 2 && stored[0] == 0xb0 && stored[1] == 0x80,
           "big-endian bit storage");
    expect(parsed->at(0) && !parsed->at(1) && parsed->at(2), "bit access");

    auto value = *parsed;
    value.set(8, true);
    expect(value.to_bit_string() == "101100001", "setting an existing bit");
    expect(value.slice(1, 4).to_bit_string() == "0110", "bit slice");

    auto replacement = ubcm::BitVector::from_bit_string("010");
    expect(replacement.has_value(), "replacement parse");
    value.write(2, *replacement);
    expect(value.to_bit_string() == "100100001", "bit write");
    value.resize(12);
    expect(value.to_bit_string() == "100100001000", "zero extension");
}

void test_cursor() {
    const auto bits = ubcm::BitVector::from_bit_string("101100001");
    expect(bits.has_value(), "cursor input parse");
    ubcm::BitCursor cursor(*bits);
    const auto first = cursor.read_uint(3);
    expect(first && *first == 5, "cross-byte integer read");
    const auto next = cursor.read_bits(4);
    expect(next && next->to_bit_string() == "1000", "bit range read");
    expect(cursor.position() == 7 && cursor.remaining() == 2, "cursor position");
    const auto failed = cursor.read_bits(3);
    expect(!failed && cursor.position() == 7, "failed read is transactional");
}

void test_codecs() {
    expect(ubcm::encode_size(256).to_bit_string() == "0010000000100000000", "size encoding");
    expect(ubcm::encode_var_uint(5).to_bit_string() == "00000000011101", "unsigned encoding");
    expect(ubcm::encode_var_int(-2).to_bit_string() == "0000000001010", "signed encoding");

    const auto float_bits = ubcm::encode_var_float({13, -1});
    expect(float_bits && float_bits->to_bit_string() == "0000000010101101000000000011",
           "float encoding");

    const auto value_bits = ubcm::encode_value(std::uint64_t{5});
    expect(value_bits && value_bits->to_bit_string() == "000000000011101", "value encoding");

    const auto input = ubcm::BitVector::from_bit_string("00000000011101");
    expect(input.has_value(), "codec input parse");
    ubcm::BitCursor cursor(*input);
    const auto decoded = ubcm::decode_var_uint(cursor);
    expect(decoded && *decoded == 5 && cursor.position() == 14, "unsigned decoding");

    const auto malformed = ubcm::BitVector::from_bit_string("000000001000101");
    expect(malformed.has_value(), "malformed input parse");
    ubcm::BitCursor malformed_cursor(*malformed);
    const auto rejected = ubcm::decode_var_uint(malformed_cursor);
    expect(!rejected && malformed_cursor.position() == 0, "non-canonical value rejection");
}

void test_registers_and_addressing() {
    auto name = *ubcm::BitVector::from_bit_string("101");
    ubcm::NameResolver resolver;
    auto handle = ubcm::RegisterHandle{ubcm::RegisterClass::global, 7};
    expect(resolver.bind(name, handle).has_value(), "bind name");
    expect(resolver.resolve(name) && *resolver.resolve(name) == handle, "resolve name");
    ubcm::RegisterBank bank;
    auto created = bank.create(ubcm::RegisterClass::global, *ubcm::BitVector::from_bit_string("0000"));
    expect(created.has_value(), "create register");
    expect(bank.write({*created, 1}, *ubcm::BitVector::from_bit_string("11")).has_value(), "write register");
    expect(bank.read({*created, 1}, 2)->to_bit_string() == "11", "read register");
    ubcm::RegisterSelector selector{ubcm::RegisterClass::global, name};
    ubcm::ResolutionContext context{nullptr, nullptr, nullptr, &resolver};
    expect(ubcm::resolve_selector(selector, context) && *ubcm::resolve_selector(selector, context) == handle, "selector resolution");
}

}  // namespace

int main() {
    struct TestCase { std::string_view name; std::function<void()> body; };
    const std::vector<TestCase> tests{
        {"version", [] { expect(ubcm::version() == std::string_view{"0.1.0"}, "version"); }},
        {"bit_vector", test_bit_vector},
        {"bit_cursor", test_cursor},
        {"codecs", test_codecs},
        {"registers_and_addressing", test_registers_and_addressing},
    };
    std::size_t failed = 0;
    for (const auto& test : tests) {
        try {
            test.body();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& exception) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << ": " << exception.what() << '\n';
        } catch (...) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << ": unknown exception\n";
        }
    }
    std::cout << tests.size() - failed << '/' << tests.size() << " tests passed\n";
    return failed == 0 ? 0 : 1;
}
