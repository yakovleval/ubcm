#include "ubcm/vm.hpp"

#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ubcm {
namespace {

VmError error(VmErrorCode code, std::string message) {
    return {code, std::move(message)};
}

VmError register_error(const RegisterError& source) {
    return error(source.code == RegisterError::Code::resource_exhausted
                     ? VmErrorCode::resource_exhausted
                     : VmErrorCode::register_access,
                 source.message);
}

VmError codec_error(const CodecError& source) {
    return error(VmErrorCode::decode_error, source.message);
}

VmError operand_error(const OperandError& source) {
    const auto code = source.code == OperandErrorCode::decode_error
                          ? VmErrorCode::decode_error
                      : source.code == OperandErrorCode::resource_exhausted
                          ? VmErrorCode::resource_exhausted
                          : VmErrorCode::register_access;
    return error(code,
                 source.message);
}

VmError arithmetic_error(const ArithmeticError& source) {
    return error(source.code == ArithmeticErrorCode::mathematical_error
                     ? VmErrorCode::mathematical_error
                     : VmErrorCode::decode_error,
                 source.message);
}

struct PreparedWrite {
    RegisterAddress address;
    BitVector bits;
};

}  // namespace

VirtualMachine::VirtualMachine(RegisterBank& registers, ActivationRecord activation,
                               OperandResolvers resolvers)
    : VirtualMachine(registers,
                     std::vector<ActivationFrame>{{std::move(activation), resolvers}}) {}

VirtualMachine::VirtualMachine(RegisterBank& registers,
                               std::vector<ActivationFrame> activations)
    : registers_(&registers), activations_(std::move(activations)) {
    if (activations_.empty()) {
        throw std::invalid_argument("VM requires at least one activation");
    }
}

ActivationFrame& VirtualMachine::current_frame() noexcept {
    return activations_.back();
}

const ActivationFrame& VirtualMachine::current_frame() const noexcept {
    return activations_.back();
}

VmResult<std::size_t> VirtualMachine::activation_index(std::uint64_t depth) const {
    if (depth >= static_cast<std::uint64_t>(activations_.size())) {
        return std::unexpected(error(VmErrorCode::invalid_state,
                                     "activation depth is out of range"));
    }
    return activations_.size() - 1U - static_cast<std::size_t>(depth);
}

const ActivationRecord& VirtualMachine::current_activation() const noexcept {
    return current_frame().activation;
}

std::size_t VirtualMachine::activation_count() const noexcept {
    return activations_.size();
}

VmResult<NodeReference> VirtualMachine::read_network_state(
    const ActivationRecord& activation) const {
    if (activation.network_state.null) {
        return std::unexpected(error(VmErrorCode::invalid_state,
                                     "activation has no network state"));
    }
    auto bits = registers_->read(
        {activation.network_state.handle, activation.network_state.bit_offset},
        runtime_reference_bit_size);
    if (!bits) {
        return std::unexpected(register_error(bits.error()));
    }
    BitCursor cursor(*bits);
    auto reference = decode_runtime_reference(cursor);
    if (!reference) {
        return std::unexpected(codec_error(reference.error()));
    }
    if (reference->null) {
        return std::unexpected(error(VmErrorCode::invalid_state,
                                     "network state contains a null node reference"));
    }
    return *reference;
}

VmResult<Node> VirtualMachine::read_node(const NodeReference& reference) const {
    if (reference.null || reference.bit_offset % node_bit_size != 0U) {
        return std::unexpected(error(VmErrorCode::invalid_state,
                                     "current node reference is invalid"));
    }
    auto bits = registers_->read({reference.handle, reference.bit_offset}, node_bit_size);
    if (!bits) {
        return std::unexpected(register_error(bits.error()));
    }
    auto node = decode_node(*bits);
    if (!node) {
        return std::unexpected(codec_error(node.error()));
    }
    return *node;
}

VmResult<void> VirtualMachine::validate_node_target(
    const NodeReference& reference) const {
    auto node = read_node(reference);
    if (!node) {
        return std::unexpected(node.error());
    }
    return {};
}

VmResult<void> VirtualMachine::validate_network_state_write(
    const ActivationRecord& activation) const {
    if (activation.network_state.null) {
        return std::unexpected(error(VmErrorCode::invalid_state,
                                     "activation has no writable network state"));
    }
    auto immutable = registers_->is_immutable(activation.network_state.handle);
    if (!immutable) return std::unexpected(register_error(immutable.error()));
    if (*immutable) {
        return std::unexpected(error(VmErrorCode::register_access,
                                     "network state register is immutable"));
    }
    auto size = registers_->size(activation.network_state.handle);
    if (!size) return std::unexpected(register_error(size.error()));
    if (activation.network_state.bit_offset > *size ||
        runtime_reference_bit_size > *size - activation.network_state.bit_offset) {
        return std::unexpected(error(VmErrorCode::invalid_state,
                                     "network state cell is out of range"));
    }
    return {};
}

VmResult<void> VirtualMachine::validate_resize_target(RegisterHandle handle,
                                                       std::uint64_t bit_size,
                                                       const NodeReference& current,
                                                       const NodeReference& next) const {
    auto immutable = registers_->is_immutable(handle);
    if (!immutable) return std::unexpected(register_error(immutable.error()));
    if (*immutable) {
        return std::unexpected(error(VmErrorCode::register_access,
                                     "register is immutable"));
    }
    const auto fits = [handle, bit_size](const RuntimeReference& reference,
                                         std::uint64_t required_size) {
        return reference.null || handle != reference.handle ||
               (reference.bit_offset <= bit_size &&
                required_size <= bit_size - reference.bit_offset);
    };
    for (const auto& frame : activations_) {
        const auto& activation = frame.activation;
        if (bit_size == 0U &&
            (handle == activation.procedure || handle == activation.network_state.handle ||
             handle == activation.result_register ||
             (!activation.local_resolver.null &&
              handle == activation.local_resolver.handle) ||
             (!activation.previous_activation.null &&
              handle == activation.previous_activation.handle))) {
            return std::unexpected(error(
                VmErrorCode::invalid_state,
                "cannot delete a register used by an active activation"));
        }
        if (!fits(activation.network_state, runtime_reference_bit_size) ||
            !fits(activation.local_resolver, node_bit_size) ||
            !fits(activation.previous_activation, activation_bit_size)) {
            return std::unexpected(error(
                VmErrorCode::invalid_state,
                "resize would invalidate an active activation reference"));
        }
        auto active_node = read_network_state(activation);
        if (!active_node) return std::unexpected(active_node.error());
        if (!fits(*active_node, node_bit_size)) {
            return std::unexpected(error(
                VmErrorCode::invalid_state,
                "resize would invalidate an active node reference"));
        }
    }
    if (!fits(current, node_bit_size) || !fits(next, node_bit_size)) {
        return std::unexpected(error(VmErrorCode::invalid_state,
                                     "resize would invalidate an active VM reference"));
    }
    return {};
}

OperandResolver VirtualMachine::operand_resolver(const ActivationFrame& frame) const {
    return OperandResolver(*registers_, frame.activation.procedure, frame.resolvers);
}

VmResult<StepResult> VirtualMachine::step() {
    try {
        return step_impl();
    } catch (const std::bad_alloc&) {
        return std::unexpected(error(VmErrorCode::resource_exhausted,
                                     "VM step ran out of memory"));
    } catch (const std::length_error&) {
        return std::unexpected(error(VmErrorCode::resource_exhausted,
                                     "VM step requires an unsupported allocation size"));
    } catch (const std::out_of_range&) {
        return std::unexpected(error(VmErrorCode::invalid_state,
                                     "VM step accessed an invalid bit range"));
    }
}

VmResult<StepResult> VirtualMachine::step_impl() {
    const auto owner_index = activations_.size() - 1U;
    const auto active_prefix = current_activation().prefix;
    auto read_index = owner_index;
    auto modify_index = owner_index;
    if (active_prefix.kind == PrefixKind::read) {
        auto index = activation_index(active_prefix.depth);
        if (!index) return std::unexpected(index.error());
        read_index = *index;
    } else if (active_prefix.kind == PrefixKind::modify) {
        auto index = activation_index(active_prefix.depth);
        if (!index) return std::unexpected(index.error());
        modify_index = *index;
    }

    auto& owner_activation = activations_[owner_index].activation;
    auto& read_frame = activations_[read_index];
    auto& modify_frame = activations_[modify_index];
    auto& modify_activation = modify_frame.activation;

    auto current_reference = read_network_state(owner_activation);
    if (!current_reference) {
        return std::unexpected(current_reference.error());
    }
    auto node = read_node(*current_reference);
    if (!node) {
        return std::unexpected(node.error());
    }
    if (node->kind != NodeKind::builtin) {
        return std::unexpected(error(VmErrorCode::unsupported_command,
                                     "procedure calls belong to the next VM stage"));
    }

    auto procedure = registers_->view(read_frame.activation.procedure);
    if (!procedure) {
        return std::unexpected(register_error(procedure.error()));
    }
    if (read_frame.activation.procedure_position > (*procedure)->size()) {
        return std::unexpected(error(VmErrorCode::invalid_state,
                                     "procedure position is out of range"));
    }
    BitCursor cursor(**procedure, read_frame.activation.procedure_position);
    auto instruction = decode_builtin(node->command, cursor);
    if (!instruction) {
        return std::unexpected(codec_error(instruction.error()));
    }
    const auto is_prefix_command =
        instruction->command == BuiltinCommand::read_prefix ||
        instruction->command == BuiltinCommand::modify_prefix ||
        instruction->command == BuiltinCommand::condition_prefix;
    if (active_prefix.kind != PrefixKind::none && is_prefix_command) {
        return std::unexpected(error(VmErrorCode::invalid_state,
                                     "cannot combine active prefixes"));
    }
    if (!instruction->branch) {
        return std::unexpected(error(VmErrorCode::unsupported_command,
                                     "builtin command has no branch transition yet"));
    }
    const auto next = *instruction->branch ? node->next1 : node->next0;
    if (auto valid = validate_node_target(next); !valid) {
        return std::unexpected(valid.error());
    }
    if (auto valid = validate_network_state_write(modify_activation); !valid) {
        return std::unexpected(valid.error());
    }
    auto encoded_next = encode_runtime_reference(next);
    if (!encoded_next) {
        return std::unexpected(codec_error(encoded_next.error()));
    }

    const auto read_resolver = operand_resolver(read_frame);
    const auto modify_resolver = operand_resolver(modify_frame);
    auto modified_procedure = registers_->view(modify_activation.procedure);
    if (!modified_procedure) {
        return std::unexpected(register_error(modified_procedure.error()));
    }
    std::optional<PreparedWrite> data_write;
    std::optional<std::pair<RegisterHandle, std::uint64_t>> resize;
    std::optional<PrefixState> next_prefix;
    auto next_position = instruction->next_procedure_position;
    const auto suppress_effect = active_prefix.kind == PrefixKind::condition &&
                                 !active_prefix.condition;

    if (!suppress_effect) switch (instruction->command) {
        case BuiltinCommand::read_prefix:
        case BuiltinCommand::modify_prefix: {
            const auto& arguments = std::get<DepthArguments>(instruction->arguments);
            if (auto index = activation_index(arguments.depth); !index) {
                return std::unexpected(index.error());
            }
            const auto kind = instruction->command == BuiltinCommand::read_prefix
                                  ? PrefixKind::read
                                  : PrefixKind::modify;
            next_prefix = PrefixState{kind, arguments.depth, false};
            break;
        }
        case BuiltinCommand::condition_prefix: {
            const auto& arguments = std::get<ConditionArguments>(instruction->arguments);
            auto address = read_resolver.resolve_destination(arguments.condition);
            if (!address) return std::unexpected(operand_error(address.error()));
            auto condition = registers_->read(*address, 1);
            if (!condition) return std::unexpected(register_error(condition.error()));
            next_prefix = PrefixState{PrefixKind::condition, 0, condition->at(0)};
            break;
        }
        case BuiltinCommand::branch:
            break;
        case BuiltinCommand::compute: {
            const auto& arguments = std::get<ComputeArguments>(instruction->arguments);
            std::vector<Value> operands;
            operands.reserve(arguments.sources.size());
            for (const auto& source : arguments.sources) {
                auto value = read_resolver.read_source_value(source);
                if (!value) return std::unexpected(operand_error(value.error()));
                operands.push_back(std::move(*value));
            }
            auto result = evaluate_operation(arguments.operation, operands);
            if (!result) return std::unexpected(arithmetic_error(result.error()));
            auto value = encode_value(*result);
            if (!value) return std::unexpected(codec_error(value.error()));
            auto address = modify_resolver.resolve_destination(arguments.destination);
            if (!address) return std::unexpected(operand_error(address.error()));
            auto size = registers_->size(address->handle);
            if (!size) return std::unexpected(register_error(size.error()));
            if (address->bit_offset > *size) {
                return std::unexpected(error(VmErrorCode::register_access,
                                             "destination offset is out of range"));
            }
            if (auto valid = modify_resolver.validate_destination(*address, value->size());
                !valid) {
                return std::unexpected(operand_error(valid.error()));
            }
            BitVector stored = *value;
            stored.resize(*size - address->bit_offset);
            data_write = PreparedWrite{*address, std::move(stored)};
            break;
        }
        case BuiltinCommand::copy: {
            const auto& arguments = std::get<CopyArguments>(instruction->arguments);
            auto value = read_resolver.read_source_bits(arguments.source);
            if (!value) return std::unexpected(operand_error(value.error()));
            auto address = modify_resolver.resolve_destination(arguments.destination);
            if (!address) return std::unexpected(operand_error(address.error()));
            auto size = registers_->size(address->handle);
            if (!size) return std::unexpected(register_error(size.error()));
            if (address->bit_offset > *size) {
                return std::unexpected(error(VmErrorCode::register_access,
                                             "destination offset is out of range"));
            }
            if (auto valid = modify_resolver.validate_destination(*address, value->size());
                !valid) {
                return std::unexpected(operand_error(valid.error()));
            }
            BitVector stored = *value;
            stored.resize(*size - address->bit_offset);
            data_write = PreparedWrite{*address, std::move(stored)};
            break;
        }
        case BuiltinCommand::set_procedure_position: {
            const auto& arguments = std::get<OneSourceArguments>(instruction->arguments);
            auto position = read_resolver.read_source_uint(arguments.source);
            if (!position) return std::unexpected(operand_error(position.error()));
            if (*position > (*modified_procedure)->size()) {
                return std::unexpected(error(VmErrorCode::invalid_state,
                                             "new procedure position is out of range"));
            }
            next_position = *position;
            break;
        }
        case BuiltinCommand::resize_register: {
            const auto& arguments = std::get<ResizeArguments>(instruction->arguments);
            auto address = modify_resolver.resolve_address({arguments.selector, 0});
            if (!address) return std::unexpected(operand_error(address.error()));
            auto size = read_resolver.read_source_uint(arguments.size);
            if (!size) return std::unexpected(operand_error(size.error()));
            if (auto valid = validate_resize_target(address->handle, *size,
                                                    *current_reference, next);
                !valid) {
                return std::unexpected(valid.error());
            }
            resize = std::pair{address->handle, *size};
            break;
        }
        case BuiltinCommand::get_register_size: {
            const auto& arguments = std::get<GetSizeArguments>(instruction->arguments);
            auto address = read_resolver.resolve_address({arguments.selector, 0});
            if (!address) return std::unexpected(operand_error(address.error()));
            auto register_size = registers_->size(address->handle);
            if (!register_size) return std::unexpected(register_error(register_size.error()));
            auto value = encode_value(*register_size);
            if (!value) return std::unexpected(codec_error(value.error()));
            auto destination = modify_resolver.resolve_destination(arguments.destination);
            if (!destination) return std::unexpected(operand_error(destination.error()));
            auto destination_size = registers_->size(destination->handle);
            if (!destination_size) {
                return std::unexpected(register_error(destination_size.error()));
            }
            if (destination->bit_offset > *destination_size) {
                return std::unexpected(error(VmErrorCode::register_access,
                                             "destination offset is out of range"));
            }
            if (auto valid = modify_resolver.validate_destination(
                    *destination, value->size());
                !valid) {
                return std::unexpected(operand_error(valid.error()));
            }
            BitVector stored = *value;
            stored.resize(*destination_size - destination->bit_offset);
            data_write = PreparedWrite{*destination, std::move(stored)};
            break;
        }
        default:
            return std::unexpected(error(VmErrorCode::unsupported_command,
                                         "builtin effect belongs to a later VM stage"));
    }

    if (next_position > (*modified_procedure)->size()) {
        return std::unexpected(error(VmErrorCode::invalid_state,
                                     "new procedure position is out of range"));
    }

    if (data_write) {
        auto written = registers_->write(data_write->address, data_write->bits);
        if (!written) return std::unexpected(register_error(written.error()));
    }
    if (resize) {
        auto resized = registers_->resize(resize->first, resize->second);
        if (!resized) return std::unexpected(register_error(resized.error()));
    }

    auto written = registers_->write(
        {modify_activation.network_state.handle,
         modify_activation.network_state.bit_offset},
        *encoded_next);
    if (!written) {
        return std::unexpected(register_error(written.error()));
    }
    modify_activation.procedure_position = next_position;
    if (next_prefix) {
        owner_activation.prefix = *next_prefix;
    } else if (active_prefix.kind != PrefixKind::none) {
        owner_activation.prefix = {};
    }
    return StepResult{instruction->command, *instruction->branch, next,
                      modify_activation.procedure_position};
}

}  // namespace ubcm
