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

} // namespace

int main() {
    try {
        test_program();
        std::cout << "program tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
