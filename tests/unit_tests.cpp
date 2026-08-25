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

void test_runtime_references_and_nodes() {
    const auto global3 = ubcm::RegisterHandle{ubcm::RegisterClass::global, 3};
    const auto global4 = ubcm::RegisterHandle{ubcm::RegisterClass::global, 4};
    const auto reference = ubcm::RuntimeReference::at(global3, 592);
    auto encoded_reference = ubcm::encode_runtime_reference(reference);
    expect(encoded_reference && encoded_reference->size() == 128,
           "runtime reference width");
    ubcm::BitCursor reference_cursor(*encoded_reference);
    expect(ubcm::decode_runtime_reference(reference_cursor) == reference,
           "runtime reference round trip");

    ubcm::Node node;
    node.command = 0b0100;
    node.next0 = ubcm::RuntimeReference::at(global3, 0);
    node.next1 = reference;
    node.superlocal_resolver = ubcm::RuntimeReference::at(global4, 0);
    auto encoded = ubcm::encode_node(node);
    expect(encoded && encoded->size() == ubcm::node_bit_size, "node width");
    expect(encoded->bytes()[0] == 0x00 && encoded->bytes()[1] == 0x04,
           "builtin node header");
    expect(ubcm::decode_node(*encoded) == node, "node round trip");

    node.next1.bit_offset = 1;
    expect(!ubcm::encode_node(node), "unaligned node reference rejection");
}

void test_activation_records() {
    ubcm::ActivationRecord activation;
    activation.procedure_position = 5;
    activation.network_state = ubcm::RuntimeReference::at(
        {ubcm::RegisterClass::global, 5}, 0);
    activation.local_resolver = ubcm::RuntimeReference::at(
        {ubcm::RegisterClass::global, 3}, 1184);
    activation.result_register = {ubcm::RegisterClass::local, 7};
    activation.prefix = {ubcm::PrefixKind::read, 1, false};

    auto encoded = ubcm::encode_activation(activation);
    expect(encoded && encoded->size() == ubcm::activation_bit_size,
           "activation width");
    expect(encoded->bytes().size() == ubcm::activation_byte_size,
           "activation byte width");
    expect(encoded->bytes()[0] == 0x55 && encoded->bytes()[1] == 0x41 &&
               encoded->bytes()[2] == 0x01 && encoded->bytes()[3] == 0x00,
           "activation header");
    expect(ubcm::decode_activation(*encoded) == activation,
           "activation round trip");

    encoded->set(0, true);
    expect(!ubcm::decode_activation(*encoded), "invalid activation magic rejection");
}

void test_builtin_decoder() {
    auto branch_bits = *ubcm::BitVector::from_bit_string("1");
    ubcm::BitCursor branch_cursor(branch_bits);
    auto branch = ubcm::decode_builtin(
        static_cast<std::uint8_t>(ubcm::BuiltinCommand::branch), branch_cursor);
    expect(branch && branch->branch == true && branch->next_procedure_position == 1,
           "branch command decoding");

    auto compute_bits = *ubcm::BitVector::from_bit_string(
        "00000000000000001110100000000000101110110000000100001010010000000000001");
    ubcm::BitCursor compute_cursor(compute_bits);
    auto compute = ubcm::decode_builtin(
        static_cast<std::uint8_t>(ubcm::BuiltinCommand::compute), compute_cursor);
    expect(compute && compute->next_procedure_position == 71 &&
               compute->branch == true,
           "compute command length and branch");
    const auto* arguments = compute
        ? std::get_if<ubcm::ComputeArguments>(&compute->arguments)
        : nullptr;
    expect(arguments && arguments->operation == 0 && arguments->sources.size() == 2,
           "compute command arguments");

    auto truncated = *ubcm::BitVector::from_bit_string("0");
    ubcm::BitCursor truncated_cursor(truncated);
    expect(!ubcm::decode_builtin(
               static_cast<std::uint8_t>(ubcm::BuiltinCommand::compute),
               truncated_cursor) && truncated_cursor.position() == 0,
           "failed builtin decoding is transactional");
}

void test_vm_branch_step() {
    ubcm::RegisterBank registers;
    auto procedure_bits = *ubcm::BitVector::from_bit_string("1");
    auto procedure = registers.create(ubcm::RegisterClass::procedure,
                                      procedure_bits, true);
    expect(procedure.has_value(), "create VM procedure");

    ubcm::Node first;
    first.command = static_cast<std::uint8_t>(ubcm::BuiltinCommand::branch);
    ubcm::Node second;
    second.command = static_cast<std::uint8_t>(ubcm::BuiltinCommand::branch);
    auto first_bits = ubcm::encode_node(first);
    auto second_bits = ubcm::encode_node(second);
    expect(first_bits && second_bits, "encode VM nodes");
    ubcm::BitVector node_storage = *first_bits;
    node_storage.append(*second_bits);
    auto nodes = registers.create(ubcm::RegisterClass::global, node_storage);
    expect(nodes.has_value(), "create node storage");

    const auto second_reference = ubcm::RuntimeReference::at(*nodes,
                                                              ubcm::node_bit_size);
    first.next1 = second_reference;
    first_bits = ubcm::encode_node(first);
    expect(first_bits.has_value(), "encode linked VM node");
    expect(registers.write({*nodes, 0}, *first_bits).has_value(),
           "write linked VM node");

    auto initial_reference = ubcm::encode_runtime_reference(
        ubcm::RuntimeReference::at(*nodes, 0));
    expect(initial_reference.has_value(), "encode initial node reference");
    auto network_state = registers.create(ubcm::RegisterClass::global,
                                          *initial_reference);
    expect(network_state.has_value(), "create network state");

    ubcm::ActivationRecord activation;
    activation.procedure = *procedure;
    activation.network_state = ubcm::RuntimeReference::at(*network_state, 0);
    ubcm::VirtualMachine vm(registers, activation);
    auto step = vm.step();
    expect(step && step->branch && step->next_node == second_reference,
           "VM selects next1");
    expect(vm.current_activation().procedure_position == 1,
           "VM advances procedure cursor");

    auto stored = registers.read({*network_state, 0},
                                 ubcm::runtime_reference_bit_size);
    expect(stored.has_value(), "read updated network state");
    ubcm::BitCursor stored_cursor(*stored);
    expect(ubcm::decode_runtime_reference(stored_cursor) == second_reference,
           "VM commits network transition");

    auto failed_step = vm.step();
    expect(!failed_step && vm.current_activation().procedure_position == 1,
           "failed VM step preserves procedure position");
    auto unchanged = registers.read({*network_state, 0},
                                    ubcm::runtime_reference_bit_size);
    ubcm::BitCursor unchanged_cursor(*unchanged);
    expect(ubcm::decode_runtime_reference(unchanged_cursor) == second_reference,
           "failed VM step preserves network state");
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
        {"runtime_references_and_nodes", test_runtime_references_and_nodes},
        {"activation_records", test_activation_records},
        {"builtin_decoder", test_builtin_decoder},
        {"vm_branch_step", test_vm_branch_step},
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
