#include "ubcm/vm.hpp"

#include <new>
#include <stdexcept>

namespace ubcm {
namespace {

OperandError failure(const char* message) {
    return {OperandErrorCode::invalid_reference, message};
}

OperandError from_vm(const VmError& error) {
    return {error.code == VmErrorCode::resource_exhausted
                ? OperandErrorCode::resource_exhausted
                : OperandErrorCode::invalid_reference, error.message};
}

} // namespace

VmResult<void> VirtualMachine::set_global_resolver(NodeReference entry) {
    if (!entry.null) {
        if (auto valid = validate_node_target(entry); !valid) return valid;
    }
    global_resolver_ = entry;
    return {};
}

OperandResolvers VirtualMachine::operand_context(std::size_t index) const {
    auto context = activations_[index].resolvers;
    context.result = activations_[index].activation.result_register;
    context.network = [this, index](RegisterClass scope, const BitVector& name) {
        return resolve_register(index, scope, name);
    };
    return context;
}

OperandResult<RegisterAddress> VirtualMachine::resolve_name(
    RegisterClass scope, const BitVector& name, std::uint64_t depth) const {
    try {
        auto vm = *this;
        if (vm.halted()) return std::unexpected(failure("VM has halted"));
        if (auto reloaded = vm.reload_activations(); !reloaded) {
            return std::unexpected(from_vm(reloaded.error()));
        }
        auto index = vm.activation_index(depth);
        if (!index) return std::unexpected(from_vm(index.error()));
        return vm.operand_resolver(vm.activations_[*index]).resolve_address({{scope, name}, 0});
    } catch (const std::bad_alloc&) {
        return std::unexpected(OperandError{OperandErrorCode::resource_exhausted,
                                            "resolver ran out of memory"});
    } catch (const std::length_error&) {
        return std::unexpected(OperandError{OperandErrorCode::resource_exhausted,
                                            "resolver allocation is too large"});
    }
}

OperandResult<RegisterAddress> VirtualMachine::resolve_register(
    std::size_t index, RegisterClass scope, const BitVector& name) const {
    const auto& frame = activations_[index];
    NodeReference entry;
    const NameResolver* fallback = nullptr;
    switch (scope) {
        case RegisterClass::local:
            entry = frame.activation.local_resolver;
            fallback = frame.resolvers.local;
            break;
        case RegisterClass::superlocal: {
            auto state = read_network_state(frame.activation);
            if (!state) return std::unexpected(from_vm(state.error()));
            auto node = read_node(*state);
            if (!node) return std::unexpected(from_vm(node.error()));
            entry = node->superlocal_resolver;
            fallback = frame.resolvers.superlocal;
            break;
        }
        case RegisterClass::global:
            entry = global_resolver_;
            fallback = frame.resolvers.global;
            break;
        case RegisterClass::procedure:
            return RegisterAddress{frame.activation.procedure, 0};
    }
    if (!entry.null) return run_resolver(index, entry, name);
    if (!fallback) {
        return std::unexpected(OperandError{OperandErrorCode::unresolved_name,
                                            "register resolver is unavailable"});
    }
    auto address = fallback->resolve_address(name);
    if (!address) {
        return std::unexpected(OperandError{OperandErrorCode::unresolved_name,
                                            address.error().message});
    }
    return *address;
}

OperandResult<RegisterAddress> VirtualMachine::run_resolver(
    std::size_t index, NodeReference entry, const BitVector& name) const {
    if (resolver_depth_ >= 32) {
        return std::unexpected(OperandError{OperandErrorCode::resource_exhausted,
                                            "resolver recursion exceeds 32 levels"});
    }
    // Keep all caller data unchanged, including allocator state. Only private
    // resolver activations and network cursors may be updated in this copy.
    auto bank = *registers_;
    bank.protect_existing();
    auto vm = *this;
    vm.registers_ = &bank;
    vm.resolved_address_.reset();
    vm.resolver_depth_ = resolver_depth_ + 1;
    vm.resolver_steps_ = resolver_steps_ ? resolver_steps_
                                       : std::make_shared<std::uint64_t>(10000);
    vm.activations_.resize(index + 1);
    vm.current_storage_ = vm.activations_.back().storage;
    auto procedure = bank.create(RegisterClass::global, name, true);
    if (!procedure) {
        return std::unexpected(OperandError{OperandErrorCode::resource_exhausted,
                                            procedure.error().message});
    }
    if (auto entered = vm.enter_call(*procedure, entry, true); !entered) {
        return std::unexpected(from_vm(entered.error()));
    }
    while (!vm.resolved_address_) {
        if (*vm.resolver_steps_ == 0) {
            return std::unexpected(OperandError{OperandErrorCode::resource_exhausted,
                                                "resolver step limit exceeded"});
        }
        --*vm.resolver_steps_;
        auto step = vm.step(index + 1 + 256);
        if (!step) return std::unexpected(from_vm(step.error()));
        if (vm.halted() || vm.activation_count() <= index + 1) {
            return std::unexpected(failure("resolver ended without RESOLVE_RETURN"));
        }
    }
    return *vm.resolved_address_;
}

} // namespace ubcm
