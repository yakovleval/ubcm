#include "ubcm/ubcm.hpp"

#include <cstdint>
#include <array>
#include <iostream>
#include <limits>
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
    const auto maximum_size = std::numeric_limits<std::uint64_t>::max();
    const auto maximum_size_bits = ubcm::encode_size(maximum_size);
    expect(maximum_size_bits.size() == 67 &&
               maximum_size_bits.to_bit_string() == "111" + std::string(64, '1'),
           "maximum size encoding");
    ubcm::BitCursor maximum_size_cursor(maximum_size_bits);
    expect(ubcm::decode_size(maximum_size_cursor) == maximum_size &&
               maximum_size_cursor.remaining() == 0,
           "maximum size round trip");
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

void test_arithmetic_operations() {
    const auto binary = [](std::uint8_t operation, ubcm::Value left,
                           ubcm::Value right) {
        const std::array operands{left, right};
        return ubcm::evaluate_operation(operation, operands);
    };
    const auto unary = [](std::uint8_t operation, ubcm::Value value) {
        const std::array operands{value};
        return ubcm::evaluate_operation(operation, operands);
    };
    expect(binary(0, std::uint64_t{5}, std::uint64_t{3}) == ubcm::Value{std::uint64_t{8}},
           "integer addition");
    expect(binary(1, std::uint64_t{0}, std::uint64_t{1}) ==
               ubcm::Value{std::numeric_limits<std::uint64_t>::max()},
           "integer subtraction wraps");
    expect(binary(2, std::uint64_t{3}, std::uint64_t{4}) == ubcm::Value{std::uint64_t{12}},
           "integer multiplication");
    expect(binary(3, std::uint64_t{7}, std::uint64_t{3}) == ubcm::Value{std::uint64_t{2}},
           "integer division");
    expect(binary(4, std::uint64_t{7}, std::uint64_t{3}) == ubcm::Value{std::uint64_t{1}},
           "integer remainder");
    expect(binary(5, std::uint64_t{2}, std::uint64_t{10}) == ubcm::Value{std::uint64_t{1024}},
           "integer power");
    expect(binary(6, std::uint64_t{0}, std::uint64_t{1}) == ubcm::Value{std::uint64_t{0}} &&
               binary(7, std::uint64_t{0}, std::uint64_t{1}) == ubcm::Value{std::uint64_t{1}},
           "logical binary operations");
    expect(binary(10, std::numeric_limits<std::uint64_t>::max(), std::uint64_t{1}) ==
               ubcm::Value{std::uint64_t{0}},
           "integer comparisons are signed");
    expect(binary(14, std::uint64_t{0x0a}, std::uint64_t{0x0c}) ==
               ubcm::Value{std::uint64_t{0x08}} &&
               binary(15, std::uint64_t{0x0a}, std::uint64_t{0x0c}) ==
               ubcm::Value{std::uint64_t{0x0e}},
           "bitwise binary operations");
    expect(binary(0, ubcm::VariableFloat{3, -1}, std::uint64_t{1}) ==
               ubcm::Value{ubcm::VariableFloat{5, -1}} &&
               !binary(14, ubcm::VariableFloat{1, 0}, std::uint64_t{1}),
           "floating arithmetic and integer-only bitwise validation");
    expect(unary(22, std::uint64_t{0}) == ubcm::Value{std::uint64_t{1}} &&
               unary(23, std::uint64_t{0}) ==
               ubcm::Value{std::numeric_limits<std::uint64_t>::max()},
           "logical and bitwise unary operations");

    const std::array<std::pair<std::uint8_t, ubcm::Value>, 14> float_operations{{
        {16, std::uint64_t{0}}, {17, std::uint64_t{0}}, {18, std::uint64_t{0}},
        {19, std::uint64_t{1}}, {20, std::uint64_t{0}}, {21, std::uint64_t{0}},
        {24, std::uint64_t{0}}, {25, std::uint64_t{0}}, {26, std::uint64_t{0}},
        {27, std::uint64_t{1}}, {28, std::uint64_t{0}}, {29, std::uint64_t{0}},
        {30, std::uint64_t{1}}, {31, std::uint64_t{1}},
    }};
    for (const auto& [operation, operand] : float_operations) {
        expect(unary(operation, operand).has_value(), "floating unary operation");
    }
    expect(!binary(3, std::uint64_t{1}, std::uint64_t{0}) &&
               !unary(30, std::uint64_t{0}),
           "mathematical domain errors are rejected");
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

void test_operand_resolution() {
    ubcm::RegisterBank registers;
    auto target = registers.create(ubcm::RegisterClass::global,
                                   *ubcm::BitVector::from_bit_string("00000"));
    expect(target.has_value(), "create operand target");
    auto name = *ubcm::BitVector::from_bit_string("1");
    ubcm::NameResolver globals;
    expect(globals.bind(name, *target).has_value(), "bind operand target name");
    ubcm::OperandResolver resolver(
        registers, {ubcm::RegisterClass::procedure, 0},
        {.global = &globals});

    const ubcm::DirectReference direct{{{ubcm::RegisterClass::global, name}, 1}};
    const ubcm::DestinationOperand destination{direct};
    auto value = *ubcm::BitVector::from_bit_string("11");
    expect(resolver.write_destination(destination, value).has_value(),
           "write direct destination");
    expect(registers.read({*target, 0}, 5)->to_bit_string() == "01100",
           "destination suffix is zero-filled");

    const ubcm::SourceOperand immediate{std::uint64_t{5}, 0, true};
    expect(resolver.read_source_bits(immediate)->to_bit_string() == "101",
           "immediate source uses minimal raw bits");
    expect(resolver.read_source_uint(immediate) == 5,
           "immediate source is an unsigned integer");
}

struct VmNodeStorage {
    ubcm::RegisterHandle nodes;
    ubcm::RegisterHandle state;
    ubcm::NodeReference next;
};

VmNodeStorage install_branching_nodes(ubcm::RegisterBank& registers,
                                      ubcm::BuiltinCommand command) {
    ubcm::Node first;
    first.command = static_cast<std::uint8_t>(command);
    ubcm::Node second;
    second.command = static_cast<std::uint8_t>(ubcm::BuiltinCommand::branch);
    auto first_bits = ubcm::encode_node(first);
    auto second_bits = ubcm::encode_node(second);
    expect(first_bits && second_bits, "encode builtin nodes");
    auto node_bits = *first_bits;
    node_bits.append(*second_bits);
    auto nodes = registers.create(ubcm::RegisterClass::global, node_bits);
    expect(nodes.has_value(), "create builtin node storage");
    const auto next = ubcm::RuntimeReference::at(*nodes, ubcm::node_bit_size);
    first.next0 = next;
    first.next1 = next;
    first_bits = ubcm::encode_node(first);
    expect(first_bits.has_value(), "link builtin nodes");
    expect(registers.write({*nodes, 0}, *first_bits).has_value(),
           "store linked builtin node");
    auto state_bits = ubcm::encode_runtime_reference(
        ubcm::RuntimeReference::at(*nodes, 0));
    expect(state_bits.has_value(), "encode builtin state");
    auto state = registers.create(ubcm::RegisterClass::global, *state_bits);
    expect(state.has_value(), "create builtin state");
    return {*nodes, *state, next};
}

void test_vm_core_builtin_effects() {
    {
        ubcm::RegisterBank registers;
        ubcm::NameResolver globals;
        auto target_name = *ubcm::BitVector::from_bit_string("1");
        auto target = registers.create(ubcm::RegisterClass::global,
                                       *ubcm::BitVector::from_bit_string("00000"));
        expect(target && globals.bind(target_name, *target), "bind copy target");
        const ubcm::SourceOperand source{std::uint64_t{5}, 0, true};
        const ubcm::DestinationOperand destination{ubcm::DirectReference{
            {{ubcm::RegisterClass::global, target_name}, 1}}};
        auto program = ubcm::encode_source(source);
        program.append(ubcm::encode_destination(destination));
        program.push_back(true);
        auto procedure = registers.create(ubcm::RegisterClass::procedure, program, true);
        const auto node_storage = install_branching_nodes(registers, ubcm::BuiltinCommand::copy);
        ubcm::ActivationRecord activation;
        activation.procedure = *procedure;
        activation.network_state = ubcm::RuntimeReference::at(node_storage.state, 0);
        ubcm::VirtualMachine vm(registers, activation, {.global = &globals});
        expect(vm.step().has_value(), "execute copy builtin");
        expect(registers.read({*target, 0}, 5)->to_bit_string() == "01010",
               "copy writes raw source bits and clears suffix");
    }

    {
        ubcm::RegisterBank registers;
        ubcm::NameResolver globals;
        auto target_name = *ubcm::BitVector::from_bit_string("1");
        auto target = registers.create(ubcm::RegisterClass::global,
                                       *ubcm::BitVector::from_bit_string("00"));
        expect(target && globals.bind(target_name, *target), "bind short copy target");
        const ubcm::SourceOperand source{std::uint64_t{5}, 0, true};
        const ubcm::DestinationOperand destination{ubcm::DirectReference{
            {{ubcm::RegisterClass::global, target_name}, 0}}};
        auto program = ubcm::encode_source(source);
        program.append(ubcm::encode_destination(destination));
        program.push_back(true);
        auto procedure = registers.create(ubcm::RegisterClass::procedure, program, true);
        const auto node_storage = install_branching_nodes(registers, ubcm::BuiltinCommand::copy);
        ubcm::ActivationRecord activation;
        activation.procedure = *procedure;
        activation.network_state = ubcm::RuntimeReference::at(node_storage.state, 0);
        ubcm::VirtualMachine vm(registers, activation, {.global = &globals});
        expect(!vm.step() && vm.current_activation().procedure_position == 0,
               "failed copy preserves procedure cursor");
        auto state = registers.read({node_storage.state, 0},
                                    ubcm::runtime_reference_bit_size);
        ubcm::BitCursor state_cursor(*state);
        expect(ubcm::decode_runtime_reference(state_cursor) ==
                   ubcm::RuntimeReference::at(node_storage.nodes, 0),
               "failed copy preserves network state");
    }

    {
        ubcm::RegisterBank registers;
        const ubcm::SourceOperand source{std::uint64_t{0}, 0, true};
        auto program = ubcm::encode_source(source);
        program.push_back(true);
        auto procedure = registers.create(ubcm::RegisterClass::procedure, program, true);
        const auto node_storage = install_branching_nodes(
            registers, ubcm::BuiltinCommand::set_procedure_position);
        ubcm::ActivationRecord activation;
        activation.procedure = *procedure;
        activation.network_state = ubcm::RuntimeReference::at(node_storage.state, 0);
        ubcm::VirtualMachine vm(registers, activation);
        expect(vm.step() && vm.current_activation().procedure_position == 0,
               "set position overrides decoded cursor position");
    }

    {
        ubcm::RegisterBank registers;
        ubcm::NameResolver globals;
        auto target_name = *ubcm::BitVector::from_bit_string("10");
        auto target = registers.create(ubcm::RegisterClass::global,
                                       *ubcm::BitVector::from_bit_string("101"));
        expect(target && globals.bind(target_name, *target), "bind resize target");
        const ubcm::RegisterSelector selector{ubcm::RegisterClass::global, target_name};
        const ubcm::SourceOperand size{std::uint64_t{7}, 0, true};
        auto program = ubcm::encode_register_selector(selector);
        program.append(ubcm::encode_source(size));
        program.push_back(true);
        auto procedure = registers.create(ubcm::RegisterClass::procedure, program, true);
        const auto node_storage = install_branching_nodes(
            registers, ubcm::BuiltinCommand::resize_register);
        ubcm::ActivationRecord activation;
        activation.procedure = *procedure;
        activation.network_state = ubcm::RuntimeReference::at(node_storage.state, 0);
        ubcm::VirtualMachine vm(registers, activation, {.global = &globals});
        expect(vm.step().has_value() && registers.size(*target) == 7,
               "resize builtin changes register size");
    }

    {
        ubcm::RegisterBank registers;
        ubcm::NameResolver globals;
        auto source_name = *ubcm::BitVector::from_bit_string("10");
        auto destination_name = *ubcm::BitVector::from_bit_string("11");
        auto source = registers.create(ubcm::RegisterClass::global,
                                       *ubcm::BitVector::from_bit_string("10101"));
        auto destination = registers.create(ubcm::RegisterClass::global, ubcm::BitVector(32));
        expect(source && destination && globals.bind(source_name, *source) &&
                   globals.bind(destination_name, *destination),
               "bind get-size registers");
        const ubcm::RegisterSelector selector{ubcm::RegisterClass::global, source_name};
        const ubcm::DestinationOperand output{ubcm::DirectReference{
            {{ubcm::RegisterClass::global, destination_name}, 0}}};
        auto program = ubcm::encode_register_selector(selector);
        program.append(ubcm::encode_destination(output));
        program.push_back(true);
        auto procedure = registers.create(ubcm::RegisterClass::procedure, program, true);
        const auto node_storage = install_branching_nodes(
            registers, ubcm::BuiltinCommand::get_register_size);
        ubcm::ActivationRecord activation;
        activation.procedure = *procedure;
        activation.network_state = ubcm::RuntimeReference::at(node_storage.state, 0);
        ubcm::VirtualMachine vm(registers, activation, {.global = &globals});
        expect(vm.step().has_value(), "execute get-size builtin");
        auto output_bits = registers.read({*destination, 0}, 32);
        ubcm::BitCursor output_cursor(*output_bits);
        auto value = ubcm::decode_value(output_cursor);
        expect(value && std::holds_alternative<std::uint64_t>(*value) &&
                   std::get<std::uint64_t>(*value) == 5,
               "get-size writes canonical Value");
    }
}

ubcm::BitVector encode_compute_program(std::uint8_t operation,
                                       const ubcm::SourceOperand& first,
                                       const ubcm::SourceOperand& second,
                                       const ubcm::DestinationOperand& destination) {
    ubcm::BitVector program;
    for (int shift = 4; shift >= 0; --shift) {
        program.push_back(((operation >> shift) & 1U) != 0U);
    }
    program.append(ubcm::encode_source(first));
    program.append(ubcm::encode_source(second));
    program.append(ubcm::encode_destination(destination));
    program.push_back(true);
    return program;
}

void test_vm_compute_builtin() {
    ubcm::RegisterBank registers;
    ubcm::NameResolver globals;
    auto output_name = *ubcm::BitVector::from_bit_string("1");
    auto output = registers.create(ubcm::RegisterClass::global, ubcm::BitVector(32));
    expect(output && globals.bind(output_name, *output), "bind compute output");
    const ubcm::SourceOperand five{std::uint64_t{5}, 0, true};
    const ubcm::SourceOperand three{std::uint64_t{3}, 0, true};
    const ubcm::DestinationOperand destination{ubcm::DirectReference{
        {{ubcm::RegisterClass::global, output_name}, 0}}};
    auto program = encode_compute_program(0, five, three, destination);
    auto procedure = registers.create(ubcm::RegisterClass::procedure, program, true);
    const auto node_storage = install_branching_nodes(registers, ubcm::BuiltinCommand::compute);
    ubcm::ActivationRecord activation;
    activation.procedure = *procedure;
    activation.network_state = ubcm::RuntimeReference::at(node_storage.state, 0);
    ubcm::VirtualMachine vm(registers, activation, {.global = &globals});
    auto step = vm.step();
    expect(step && vm.current_activation().procedure_position == program.size(),
           "execute compute builtin");
    auto stored = registers.read({*output, 0}, 32);
    ubcm::BitCursor cursor(*stored);
    auto value = ubcm::decode_value(cursor);
    expect(value && *value == ubcm::Value{std::uint64_t{8}},
           "compute writes canonical integer Value");

    ubcm::RegisterBank failing_registers;
    ubcm::NameResolver failing_globals;
    auto failing_output = failing_registers.create(ubcm::RegisterClass::global,
                                                   ubcm::BitVector(32));
    expect(failing_output && failing_globals.bind(output_name, *failing_output),
           "bind failing compute output");
    auto failing_program = encode_compute_program(3, five,
                                                  ubcm::SourceOperand{std::uint64_t{0}, 0, true},
                                                  destination);
    auto failing_procedure = failing_registers.create(ubcm::RegisterClass::procedure,
                                                       failing_program, true);
    const auto failing_nodes = install_branching_nodes(
        failing_registers, ubcm::BuiltinCommand::compute);
    ubcm::ActivationRecord failing_activation;
    failing_activation.procedure = *failing_procedure;
    failing_activation.network_state = ubcm::RuntimeReference::at(failing_nodes.state, 0);
    ubcm::VirtualMachine failing_vm(failing_registers, failing_activation,
                                    {.global = &failing_globals});
    expect(!failing_vm.step() && failing_vm.current_activation().procedure_position == 0,
           "mathematical failure preserves VM cursor");
    auto state = failing_registers.read({failing_nodes.state, 0},
                                        ubcm::runtime_reference_bit_size);
    ubcm::BitCursor state_cursor(*state);
    expect(ubcm::decode_runtime_reference(state_cursor) ==
               ubcm::RuntimeReference::at(failing_nodes.nodes, 0),
           "mathematical failure preserves network state");
}

}  // namespace

int main() {
    struct TestCase { std::string_view name; std::function<void()> body; };
    const std::vector<TestCase> tests{
        {"version", [] { expect(ubcm::version() == std::string_view{"0.1.0"}, "version"); }},
        {"bit_vector", test_bit_vector},
        {"bit_cursor", test_cursor},
        {"codecs", test_codecs},
        {"arithmetic_operations", test_arithmetic_operations},
        {"registers_and_addressing", test_registers_and_addressing},
        {"runtime_references_and_nodes", test_runtime_references_and_nodes},
        {"activation_records", test_activation_records},
        {"builtin_decoder", test_builtin_decoder},
        {"vm_branch_step", test_vm_branch_step},
        {"operand_resolution", test_operand_resolution},
        {"vm_core_builtin_effects", test_vm_core_builtin_effects},
        {"vm_compute_builtin", test_vm_compute_builtin},
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
