#include "ubcm/ubcm.hpp"

#include <iostream>
#include <stdexcept>

namespace {

void expect(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

ubcm::ProgramImage example() {
    ubcm::Node finish;
    finish.command = static_cast<std::uint8_t>(ubcm::BuiltinCommand::finish_call);
    return {{ubcm::RegisterClass::global, 2},
            ubcm::RuntimeReference::at({ubcm::RegisterClass::global, 9}, 0),
            {{9, 2, *ubcm::BitVector::from_bit_string("1"), *ubcm::encode_node(finish)},
             {2, 1, *ubcm::BitVector::from_bit_string("0"),
                    *ubcm::BitVector::from_bit_string("101")}}};
}

void test_program() {
    const auto program = example();
    const ubcm::ProcedureImage procedure{program.registers.back().name,
                                         program.registers.back().contents};
    auto procedure_bits = ubcm::encode_procedure(procedure);
    expect(ubcm::decode_procedure(procedure_bits) == procedure,
           "separate procedure round trip preserves bit length");
    auto bad_procedure = procedure_bits;
    bad_procedure.set(bad_procedure.size() - 1, true);
    expect(!ubcm::decode_procedure(bad_procedure), "reject procedure padding");
    bad_procedure = procedure_bits;
    bad_procedure.append(ubcm::BitVector(8));
    expect(!ubcm::decode_procedure(bad_procedure), "reject trailing procedure data");
    expect(!ubcm::decode_procedure(procedure_bits.slice(0, 8)), "reject truncated procedure");
    auto bits = ubcm::encode_program(program);
    expect(bits.has_value(), "encode program");
    expect(ubcm::decode_program(*bits) == program, "container round trip");
    auto loaded = ubcm::load_program(program);
    expect(loaded && loaded->names.resolve(program.registers[0].name) ==
                         ubcm::RegisterHandle{ubcm::RegisterClass::global, 9} &&
               loaded->registers.size(program.procedure) == 3,
           "loader preserves sparse IDs, names and bit lengths");
    auto state = loaded->registers.create(ubcm::RegisterClass::global,
                                          *ubcm::encode_runtime_reference(program.entry));
    expect(state && state->id == 10, "runtime allocation follows imported IDs");
    ubcm::ActivationRecord activation;
    activation.procedure = program.procedure;
    activation.network_state = ubcm::RuntimeReference::at(*state, 0);
    ubcm::VirtualMachine vm(loaded->registers, activation, {.global = &loaded->names});
    auto run = vm.run(1);
    expect(run && run->halted, "execute loaded program");

    for (std::uint64_t size = 0; size < bits->size(); size += 8) {
        expect(!ubcm::decode_program(bits->slice(0, size)), "reject truncated container");
    }
    for (std::uint64_t offset : {0U, 47U, 48U, 64U}) {
        auto invalid = *bits;
        invalid.set(offset, !invalid.at(offset));
        expect(!ubcm::decode_program(invalid), "reject invalid header");
    }
    auto trailing = *bits;
    trailing.append(ubcm::BitVector(8));
    expect(!ubcm::decode_program(trailing), "reject trailing bytes");
    auto padding = *bits;
    padding.set(padding.size() - 1, true);
    expect(!ubcm::decode_program(padding), "reject nonzero record padding");
    auto invalid = program;
    invalid.registers.push_back(invalid.registers.front());
    expect(!ubcm::encode_program(invalid), "reject duplicate records");
    invalid = program;
    invalid.registers.back().name = invalid.registers.front().name;
    expect(!ubcm::validate_program(invalid), "reject duplicate names");
    invalid = program;
    invalid.registers.back().flags = 0;
    expect(!ubcm::load_program(invalid), "reject mutable root procedure");
    invalid = program;
    invalid.entry.bit_offset = 1;
    expect(!ubcm::load_program(invalid), "reject misaligned entry");
    invalid = program;
    invalid.registers.front().contents.resize(591);
    expect(!ubcm::validate_program(invalid), "reject incomplete node");
    invalid = program;
    ubcm::Node node;
    node.command = static_cast<std::uint8_t>(ubcm::BuiltinCommand::branch);
    node.next0 = ubcm::RuntimeReference::at({ubcm::RegisterClass::global, 99}, 0);
    invalid.registers.front().contents = *ubcm::encode_node(node);
    expect(!ubcm::load_program(invalid), "reject dangling static node reference");
    node.next0 = program.entry;
    node.next1 = program.entry;
    invalid.registers.front().contents = *ubcm::encode_node(node);
    expect(ubcm::validate_program(invalid).has_value(), "allow network cycles");
    invalid.registers.front().flags = 8;
    expect(!ubcm::validate_program(invalid), "reject reserved flags");
}

void test_two_languages() {
    for (std::uint64_t value : {5U, 6U}) {
        const auto name = *ubcm::BitVector::from_bit_string("1");
        auto handler = ubcm::encode_source({value, 0, true});
        handler.append(ubcm::encode_destination({ubcm::DirectReference{
            {{ubcm::RegisterClass::global, name}, 0}}}));
        handler.push_back(true);
        ubcm::Node call;
        call.kind = ubcm::NodeKind::procedure_call;
        call.procedure = {ubcm::RegisterClass::global, 3};
        call.entry = ubcm::RuntimeReference::at({ubcm::RegisterClass::global, 4}, 0);
        call.next0 = ubcm::RuntimeReference::at({ubcm::RegisterClass::global, 1},
                                               ubcm::node_bit_size);
        ubcm::Node finish;
        finish.command = static_cast<std::uint8_t>(ubcm::BuiltinCommand::finish_call);
        ubcm::Node copy;
        copy.command = static_cast<std::uint8_t>(ubcm::BuiltinCommand::copy);
        copy.next1 = ubcm::RuntimeReference::at({ubcm::RegisterClass::global, 4},
                                               ubcm::node_bit_size);
        auto network = *ubcm::encode_node(call);
        network.append(*ubcm::encode_node(finish));
        auto handler_network = *ubcm::encode_node(copy);
        handler_network.append(*ubcm::encode_node(finish));
        ubcm::ProgramImage image{{ubcm::RegisterClass::global, 0},
            ubcm::RuntimeReference::at({ubcm::RegisterClass::global, 1}, 0),
            {{0, 1, {}, {}}, // identical empty procedure for both languages
             {1, 2, *ubcm::BitVector::from_bit_string("0"), network},
             {2, 0, name, ubcm::BitVector(3)},
             {3, 1, *ubcm::BitVector::from_bit_string("00"), handler},
             {4, 2, *ubcm::BitVector::from_bit_string("01"), handler_network}}};
        auto bytes = ubcm::encode_program(image);
        expect(bytes.has_value(), "encode language example");
        auto decoded = ubcm::decode_program(*bytes);
        expect(decoded.has_value(), "decode language example");
        auto loaded = ubcm::load_program(*decoded);
        expect(loaded.has_value(), "load language example");
        auto state = loaded->registers.create(ubcm::RegisterClass::global,
                                               *ubcm::encode_runtime_reference(loaded->entry));
        expect(state.has_value(), "create language execution state");
        ubcm::ActivationRecord activation;
        activation.procedure = loaded->procedure;
        activation.network_state = ubcm::RuntimeReference::at(*state, 0);
        ubcm::VirtualMachine vm(loaded->registers, activation, {.global = &loaded->names});
        auto result = vm.run(4);
        auto output = loaded->registers.read({{ubcm::RegisterClass::global, 2}, 0}, 3);
        expect(result && result->halted && result->steps == 4 && output &&
                   output->to_bit_string() == (value == 5 ? "101" : "110"),
               "same procedure has different behavior under different networks");
    }
}

} // namespace

int main() {
    try {
        test_program();
        test_two_languages();
        std::cout << "program tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
