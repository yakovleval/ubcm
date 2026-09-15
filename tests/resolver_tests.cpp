#include "ubcm/ubcm.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

void expect(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

ubcm::NodeReference install(ubcm::RegisterBank& bank, ubcm::Node node) {
    auto bits = ubcm::encode_node(node);
    expect(bits.has_value(), "encode resolver node");
    auto handle = bank.create(ubcm::RegisterClass::global, *bits);
    expect(handle.has_value(), "allocate resolver node");
    return ubcm::RuntimeReference::at(*handle, 0);
}

ubcm::BitVector address_name(std::uint64_t id, std::uint64_t offset) {
    auto bits = ubcm::encode_source({id, 0, true});
    bits.append(ubcm::encode_source({offset, 0, true}));
    return bits;
}

struct Fixture {
    ubcm::RegisterBank bank;
    ubcm::NameResolver globals;
    ubcm::RegisterHandle target;
    ubcm::NodeReference resolver;
    ubcm::ActivationRecord root;

    explicit Fixture(ubcm::BuiltinCommand command = ubcm::BuiltinCommand::resolve_return) {
        target = *bank.create(ubcm::RegisterClass::global, ubcm::BitVector(32));
        expect(globals.bind(*ubcm::BitVector::from_bit_string("1"), target).has_value(),
               "bind writable test register");
        ubcm::Node node;
        node.command = static_cast<std::uint8_t>(command);
        resolver = install(bank, node);
        node.command = static_cast<std::uint8_t>(ubcm::BuiltinCommand::finish_call);
        node.superlocal_resolver = resolver;
        auto entry = install(bank, node);
        root.procedure = *bank.create(ubcm::RegisterClass::global, ubcm::BitVector{}, true);
        auto state = *bank.create(ubcm::RegisterClass::global,
                                  *ubcm::encode_runtime_reference(entry));
        root.network_state = ubcm::RuntimeReference::at(state, 0);
        root.local_resolver = resolver;
    }
};

void test_address_and_context() {
    Fixture f;
    ubcm::VirtualMachine vm(f.bank, f.root, {.global = &f.globals});
    auto name = address_name(f.target.id, 7);
    ubcm::BitCursor cursor(name);
    auto decoded = ubcm::decode_builtin(0xe, cursor);
    expect(decoded && !decoded->branch && cursor.remaining() == 0,
           "RESOLVE_RETURN has two sources and no branch bit");
    auto before = f.bank;
    for (auto scope : {ubcm::RegisterClass::local, ubcm::RegisterClass::superlocal}) {
        expect(vm.resolve_name(scope, name) == ubcm::RegisterAddress{f.target, 7},
               "local and superlocal networks return physical addresses");
    }
    expect(vm.set_global_resolver(f.resolver).has_value(), "configure global network");
    expect(vm.resolve_name(ubcm::RegisterClass::global, name) ==
               ubcm::RegisterAddress{f.target, 7}, "global resolver network executes");
    expect(vm.resolve_name(ubcm::RegisterClass::global,
               address_name(12345, std::numeric_limits<std::uint64_t>::max())) ==
               ubcm::RegisterAddress{{ubcm::RegisterClass::global, 12345},
                                     std::numeric_limits<std::uint64_t>::max()},
           "resolver allows a future register and full-width offset");
    expect(!vm.resolve_name(ubcm::RegisterClass::global,
                           address_name(std::uint64_t{1} << 62, 0)),
           "resolved ID must fit the physical handle");
    auto actual = f.bank.create(ubcm::RegisterClass::global, ubcm::BitVector{});
    auto expected = before.create(ubcm::RegisterClass::global, ubcm::BitVector{});
    expect(actual && expected && *actual == *expected &&
               f.bank.size(f.target) == 32,
           "resolver discards temporary allocations and allocator changes");

    Fixture context;
    ubcm::VirtualMachine stack(context.bank, std::vector<ubcm::ActivationFrame>{
        {context.root, {.global = &context.globals}},
        {context.root, {.global = &context.globals}}});
    const auto value = *ubcm::encode_value(std::uint64_t{17});
    auto root_result = (*stack.activation_at_depth(1))->result_register;
    auto top_result = stack.current_activation().result_register;
    expect(context.bank.replace(root_result, value) &&
               context.bank.replace(top_result, *ubcm::encode_value(std::uint64_t{23})),
           "store distinct activation context values");
    auto contextual_name = ubcm::encode_source({
        ubcm::ForeignReference{1, false, {}, 0}, value.size(), false});
    contextual_name.append(ubcm::encode_source({std::uint64_t{0}, 0, true}));
    expect(stack.resolve_name(ubcm::RegisterClass::local, contextual_name, 1) ==
               ubcm::RegisterAddress{{ubcm::RegisterClass::global, 17}, 0} &&
               stack.resolve_name(ubcm::RegisterClass::local, contextual_name) ==
               ubcm::RegisterAddress{{ubcm::RegisterClass::global, 23}, 0},
           "resolver foreign depth one reads the requested activation context");
    expect(!stack.resolve_name(ubcm::RegisterClass::local, contextual_name, 2),
           "invalid requested context depth fails");
}

void test_writes_and_termination() {
    const auto name = *ubcm::BitVector::from_bit_string("1");
    const ubcm::DestinationOperand dest{ubcm::DirectReference{
        {{ubcm::RegisterClass::global, name}, 0}}};
    const ubcm::SourceOperand one{std::uint64_t{1}, 0, true};
    for (auto command : {ubcm::BuiltinCommand::copy, ubcm::BuiltinCommand::compute,
                         ubcm::BuiltinCommand::resize_register,
                         ubcm::BuiltinCommand::get_register_size,
                         ubcm::BuiltinCommand::return_result}) {
        Fixture f(command);
        ubcm::VirtualMachine vm(f.bank, f.root, {.global = &f.globals});
        ubcm::BitVector code;
        if (command == ubcm::BuiltinCommand::compute) {
            code = ubcm::BitVector(5);
            code.append(ubcm::encode_source(one));
        }
        if (command == ubcm::BuiltinCommand::resize_register ||
            command == ubcm::BuiltinCommand::get_register_size) {
            code.append(ubcm::encode_register_selector({ubcm::RegisterClass::global, name}));
        }
        if (command != ubcm::BuiltinCommand::get_register_size) {
            code.append(ubcm::encode_source(one));
        }
        if (command == ubcm::BuiltinCommand::copy ||
            command == ubcm::BuiltinCommand::compute ||
            command == ubcm::BuiltinCommand::get_register_size) {
            code.append(ubcm::encode_destination(dest));
        }
        code.push_back(true);
        const auto activation = *vm.activation_storage_at_depth(0);
        const auto before = **f.bank.view(activation.handle);
        auto result = vm.resolve_name(ubcm::RegisterClass::local, code);
        expect(!result && result.error().message.find("cannot write") != std::string::npos &&
                   **f.bank.view(f.target) == ubcm::BitVector(32) &&
                   **f.bank.view(activation.handle) == before,
               "resolver write attempts fail without changing caller storage");
    }
    Fixture finish(ubcm::BuiltinCommand::finish_call);
    ubcm::VirtualMachine vm(finish.bank, finish.root);
    expect(!vm.resolve_name(ubcm::RegisterClass::local, name),
           "finish-call cannot replace RESOLVE_RETURN");

    Fixture loop(ubcm::BuiltinCommand::set_procedure_position);
    ubcm::Node node;
    node.command = static_cast<std::uint8_t>(ubcm::BuiltinCommand::set_procedure_position);
    node.next1 = loop.resolver;
    expect(loop.bank.write({loop.resolver.handle, 0}, *ubcm::encode_node(node)).has_value(),
           "install looping resolver");
    ubcm::VirtualMachine looping(loop.bank, loop.root);
    auto code = ubcm::encode_source({std::uint64_t{0}, 0, true});
    code.push_back(true);
    auto result = looping.resolve_name(ubcm::RegisterClass::local, code);
    expect(!result && result.error().code == ubcm::OperandErrorCode::resource_exhausted,
           "resolver loops have a bounded execution budget");
}

void test_branching_recursion_and_protection() {
    Fixture f;
    ubcm::Node finish;
    finish.command = static_cast<std::uint8_t>(ubcm::BuiltinCommand::finish_call);
    const auto exit = install(f.bank, finish);
    ubcm::NodeReference branches[2];
    for (std::uint64_t bit = 0; bit < 2; ++bit) {
        auto handler = *f.bank.create(ubcm::RegisterClass::global,
                                      address_name(f.target.id, bit * 4), true);
        ubcm::Node call;
        call.kind = ubcm::NodeKind::procedure_call;
        call.procedure = handler;
        call.entry = f.resolver;
        call.next0 = exit;
        branches[bit] = install(f.bank, call);
    }
    ubcm::Node branch;
    branch.command = static_cast<std::uint8_t>(ubcm::BuiltinCommand::branch);
    branch.next0 = branches[0];
    branch.next1 = branches[1];
    f.root.local_resolver = install(f.bank, branch);
    ubcm::VirtualMachine vm(f.bank, f.root);
    expect(vm.resolve_name(ubcm::RegisterClass::local,
                           *ubcm::BitVector::from_bit_string("0")) ==
               ubcm::RegisterAddress{f.target, 0} &&
               vm.resolve_name(ubcm::RegisterClass::local,
                               *ubcm::BitVector::from_bit_string("1")) ==
               ubcm::RegisterAddress{f.target, 4},
           "resolver network interprets name bits and calls read-only handlers");

    Fixture recursive;
    auto code = ubcm::encode_source({ubcm::DirectReference{
        {{ubcm::RegisterClass::global, *ubcm::BitVector::from_bit_string("1")}, 0}},
        16, false});
    code.append(ubcm::encode_source({std::uint64_t{0}, 0, true}));
    auto handler = *recursive.bank.create(ubcm::RegisterClass::global, code, true);
    ubcm::Node call;
    call.kind = ubcm::NodeKind::procedure_call;
    call.procedure = handler;
    call.entry = recursive.resolver;
    call.next0 = recursive.resolver;
    const auto entry = install(recursive.bank, call);
    ubcm::VirtualMachine recursion(recursive.bank, recursive.root);
    expect(recursion.set_global_resolver(entry).has_value(), "configure recursive resolver");
    auto error = recursion.resolve_name(ubcm::RegisterClass::global,
                                       *ubcm::BitVector::from_bit_string("1"));
    expect(!error && error.error().code == ubcm::OperandErrorCode::resource_exhausted,
           "recursive resolver calls share bounded depth");

    Fixture protected_context;
    ubcm::Node modify;
    modify.command = static_cast<std::uint8_t>(ubcm::BuiltinCommand::modify_prefix);
    modify.next1 = protected_context.resolver;
    protected_context.root.local_resolver = install(protected_context.bank, modify);
    ubcm::Node choose;
    choose.command = static_cast<std::uint8_t>(ubcm::BuiltinCommand::branch);
    choose.next1 = protected_context.resolver;
    expect(protected_context.bank.write({protected_context.resolver.handle, 0},
        *ubcm::encode_node(choose)).has_value(), "install control-write attempt");
    auto input = ubcm::encode_size(1);
    input.push_back(true);
    input.push_back(true);
    ubcm::VirtualMachine protected_vm(protected_context.bank, protected_context.root);
    const auto before = **protected_context.bank.view(protected_context.root.network_state.handle);
    auto denied = protected_vm.resolve_name(ubcm::RegisterClass::local, input);
    expect(!denied &&
               **protected_context.bank.view(protected_context.root.network_state.handle) == before,
           "modify prefix cannot write a caller network cursor");
}

void test_instruction_integration() {
    Fixture f;
    const auto name = address_name(f.target.id, 2);
    auto program = ubcm::encode_source({std::uint64_t{5}, 0, true});
    program.append(ubcm::encode_destination({ubcm::DirectReference{
        {{ubcm::RegisterClass::local, name}, 1}}}));
    program.push_back(true);
    f.root.procedure = *f.bank.create(ubcm::RegisterClass::global, program, true);
    ubcm::Node finish;
    finish.command = static_cast<std::uint8_t>(ubcm::BuiltinCommand::finish_call);
    auto exit = install(f.bank, finish);
    ubcm::Node copy;
    copy.command = static_cast<std::uint8_t>(ubcm::BuiltinCommand::copy);
    copy.next1 = exit;
    auto entry = install(f.bank, copy);
    expect(f.bank.write({f.root.network_state.handle, 0},
                        *ubcm::encode_runtime_reference(entry)).has_value(), "select copy node");
    ubcm::VirtualMachine vm(f.bank, f.root);
    auto result = vm.run(2);
    expect(result && result->halted &&
               f.bank.read({f.target, 0}, 6)->to_bit_string() == "000101",
           "ordinary COPY uses resolver address plus encoded offset");

    Fixture outside;
    outside.root.procedure = *outside.bank.create(
        ubcm::RegisterClass::global, address_name(0, 0), true);
    outside.root.network_state = ubcm::RuntimeReference::at(
        *outside.bank.create(ubcm::RegisterClass::global,
                             *ubcm::encode_runtime_reference(outside.resolver)), 0);
    ubcm::VirtualMachine invalid(outside.bank, outside.root);
    expect(!invalid.step(), "RESOLVE_RETURN is invalid in an ordinary procedure");

    Fixture bad(ubcm::BuiltinCommand::copy);
    auto bad_name = ubcm::encode_source({std::uint64_t{1}, 0, true});
    bad_name.append(ubcm::encode_destination({ubcm::DirectReference{
        {{ubcm::RegisterClass::global, *ubcm::BitVector::from_bit_string("1")}, 0}}}));
    bad_name.push_back(true);
    auto body = ubcm::encode_source({std::uint64_t{5}, 0, true});
    body.append(ubcm::encode_destination({ubcm::DirectReference{
        {{ubcm::RegisterClass::local, bad_name}, 0}}}));
    body.push_back(true);
    bad.root.procedure = *bad.bank.create(ubcm::RegisterClass::global, body, true);
    copy.next1 = install(bad.bank, finish);
    auto main_entry = install(bad.bank, copy);
    expect(bad.bank.write({bad.root.network_state.handle, 0},
        *ubcm::encode_runtime_reference(main_entry)).has_value(), "select failing copy");
    ubcm::VirtualMachine failing(bad.bank, bad.root, {.global = &bad.globals});
    auto failed = failing.step();
    expect(!failed && failed.error().message.find("cannot write") != std::string::npos &&
               failing.current_activation().procedure_position == 0 &&
               **bad.bank.view(bad.target) == ubcm::BitVector(32) &&
               **bad.bank.view(bad.root.network_state.handle) ==
                   *ubcm::encode_runtime_reference(main_entry),
           "resolver write error rolls back the entire calling command");
}

} // namespace

int main() {
    try {
        test_address_and_context();
        test_writes_and_termination();
        test_branching_recursion_and_protection();
        test_instruction_integration();
        std::cout << "resolver tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
