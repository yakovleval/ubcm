#include "ubcm/vm.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace ubcm {
namespace {

VmError error(VmErrorCode code, std::string message) {
    return {code, std::move(message)};
}

VmError register_error(const RegisterError& source) {
    return error(VmErrorCode::register_access, source.message);
}

VmError codec_error(const CodecError& source) {
    return error(VmErrorCode::decode_error, source.message);
}

VmError operand_error(const OperandError& source) {
    return error(source.code == OperandErrorCode::decode_error
                     ? VmErrorCode::decode_error
                     : VmErrorCode::register_access,
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
    : registers_(&registers), activation_(std::move(activation)), resolvers_(resolvers) {}

const ActivationRecord& VirtualMachine::current_activation() const noexcept {
    return activation_;
}

VmResult<NodeReference> VirtualMachine::read_network_state() const {
    if (activation_.network_state.null) {
        return std::unexpected(error(VmErrorCode::invalid_state,
                                     "activation has no network state"));
    }
    auto bits = registers_->read(
        {activation_.network_state.handle, activation_.network_state.bit_offset},
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

VmResult<void> VirtualMachine::validate_network_state_write() const {
    if (activation_.network_state.null) {
        return std::unexpected(error(VmErrorCode::invalid_state,
                                     "activation has no writable network state"));
    }
    auto immutable = registers_->is_immutable(activation_.network_state.handle);
    if (!immutable) return std::unexpected(register_error(immutable.error()));
    if (*immutable) {
        return std::unexpected(error(VmErrorCode::register_access,
                                     "network state register is immutable"));
    }
    auto size = registers_->size(activation_.network_state.handle);
    if (!size) return std::unexpected(register_error(size.error()));
    if (activation_.network_state.bit_offset > *size ||
        runtime_reference_bit_size > *size - activation_.network_state.bit_offset) {
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
    if (bit_size == 0U &&
        (handle == activation_.procedure || handle == activation_.network_state.handle ||
         handle == current.handle || handle == activation_.result_register ||
         (!activation_.local_resolver.null && handle == activation_.local_resolver.handle))) {
        return std::unexpected(error(VmErrorCode::invalid_state,
                                     "cannot delete a register used by the active VM state"));
    }
    const auto fits = [handle, bit_size](const RuntimeReference& reference,
                                         std::uint64_t required_size) {
        return reference.null || handle != reference.handle ||
               reference.bit_offset <= bit_size &&
                   required_size <= bit_size - reference.bit_offset;
    };
    if (!fits(activation_.network_state, runtime_reference_bit_size) ||
        !fits(current, node_bit_size) || !fits(next, node_bit_size)) {
        return std::unexpected(error(VmErrorCode::invalid_state,
                                     "resize would invalidate an active VM reference"));
    }
    return {};
}

OperandResolver VirtualMachine::operand_resolver() const {
    return OperandResolver(*registers_, activation_.procedure, resolvers_);
}

VmResult<StepResult> VirtualMachine::step() {
    if (activation_.prefix.kind != PrefixKind::none) {
        return std::unexpected(error(VmErrorCode::unsupported_command,
                                     "prefix execution belongs to the next VM stage"));
    }

    auto current_reference = read_network_state();
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

    auto procedure = registers_->view(activation_.procedure);
    if (!procedure) {
        return std::unexpected(register_error(procedure.error()));
    }
    if (activation_.procedure_position > (*procedure)->size()) {
        return std::unexpected(error(VmErrorCode::invalid_state,
                                     "procedure position is out of range"));
    }
    BitCursor cursor(**procedure, activation_.procedure_position);
    auto instruction = decode_builtin(node->command, cursor);
    if (!instruction) {
        return std::unexpected(codec_error(instruction.error()));
    }
    if (!instruction->branch) {
        return std::unexpected(error(VmErrorCode::unsupported_command,
                                     "builtin command has no branch transition yet"));
    }
    const auto next = *instruction->branch ? node->next1 : node->next0;
    if (auto valid = validate_node_target(next); !valid) {
        return std::unexpected(valid.error());
    }
    if (auto valid = validate_network_state_write(); !valid) {
        return std::unexpected(valid.error());
    }
    auto encoded_next = encode_runtime_reference(next);
    if (!encoded_next) {
        return std::unexpected(codec_error(encoded_next.error()));
    }

    const auto resolver = operand_resolver();
    std::optional<PreparedWrite> data_write;
    std::optional<std::pair<RegisterHandle, std::uint64_t>> resize;
    auto next_position = instruction->next_procedure_position;

    switch (instruction->command) {
        case BuiltinCommand::branch:
            break;
        case BuiltinCommand::compute: {
            const auto& arguments = std::get<ComputeArguments>(instruction->arguments);
            std::vector<Value> operands;
            operands.reserve(arguments.sources.size());
            for (const auto& source : arguments.sources) {
                auto value = resolver.read_source_value(source);
                if (!value) return std::unexpected(operand_error(value.error()));
                operands.push_back(std::move(*value));
            }
            auto result = evaluate_operation(arguments.operation, operands);
            if (!result) return std::unexpected(arithmetic_error(result.error()));
            auto value = encode_value(*result);
            if (!value) return std::unexpected(codec_error(value.error()));
            auto address = resolver.resolve_destination(arguments.destination);
            if (!address) return std::unexpected(operand_error(address.error()));
            auto size = registers_->size(address->handle);
            if (!size) return std::unexpected(register_error(size.error()));
            if (address->bit_offset > *size) {
                return std::unexpected(error(VmErrorCode::register_access,
                                             "destination offset is out of range"));
            }
            if (auto valid = resolver.validate_destination(*address, value->size()); !valid) {
                return std::unexpected(operand_error(valid.error()));
            }
            BitVector stored = *value;
            stored.resize(*size - address->bit_offset);
            data_write = PreparedWrite{*address, std::move(stored)};
            break;
        }
        case BuiltinCommand::copy: {
            const auto& arguments = std::get<CopyArguments>(instruction->arguments);
            auto value = resolver.read_source_bits(arguments.source);
            if (!value) return std::unexpected(operand_error(value.error()));
            auto address = resolver.resolve_destination(arguments.destination);
            if (!address) return std::unexpected(operand_error(address.error()));
            auto size = registers_->size(address->handle);
            if (!size) return std::unexpected(register_error(size.error()));
            if (address->bit_offset > *size) {
                return std::unexpected(error(VmErrorCode::register_access,
                                             "destination offset is out of range"));
            }
            if (auto valid = resolver.validate_destination(*address, value->size()); !valid) {
                return std::unexpected(operand_error(valid.error()));
            }
            BitVector stored = *value;
            stored.resize(*size - address->bit_offset);
            data_write = PreparedWrite{*address, std::move(stored)};
            break;
        }
        case BuiltinCommand::set_procedure_position: {
            const auto& arguments = std::get<OneSourceArguments>(instruction->arguments);
            auto position = resolver.read_source_uint(arguments.source);
            if (!position) return std::unexpected(operand_error(position.error()));
            if (*position > (*procedure)->size()) {
                return std::unexpected(error(VmErrorCode::invalid_state,
                                             "new procedure position is out of range"));
            }
            next_position = *position;
            break;
        }
        case BuiltinCommand::resize_register: {
            const auto& arguments = std::get<ResizeArguments>(instruction->arguments);
            auto address = resolver.resolve_address({arguments.selector, 0});
            if (!address) return std::unexpected(operand_error(address.error()));
            auto size = resolver.read_source_uint(arguments.size);
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
            auto address = resolver.resolve_address({arguments.selector, 0});
            if (!address) return std::unexpected(operand_error(address.error()));
            auto register_size = registers_->size(address->handle);
            if (!register_size) return std::unexpected(register_error(register_size.error()));
            auto value = encode_value(*register_size);
            if (!value) return std::unexpected(codec_error(value.error()));
            auto destination = resolver.resolve_destination(arguments.destination);
            if (!destination) return std::unexpected(operand_error(destination.error()));
            auto destination_size = registers_->size(destination->handle);
            if (!destination_size) {
                return std::unexpected(register_error(destination_size.error()));
            }
            if (destination->bit_offset > *destination_size) {
                return std::unexpected(error(VmErrorCode::register_access,
                                             "destination offset is out of range"));
            }
            if (auto valid = resolver.validate_destination(*destination, value->size()); !valid) {
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

    if (data_write) {
        auto written = registers_->write(data_write->address, data_write->bits);
        if (!written) return std::unexpected(register_error(written.error()));
    }
    if (resize) {
        auto resized = registers_->resize(resize->first, resize->second);
        if (!resized) return std::unexpected(register_error(resized.error()));
    }

    auto written = registers_->write(
        {activation_.network_state.handle, activation_.network_state.bit_offset},
        *encoded_next);
    if (!written) {
        return std::unexpected(register_error(written.error()));
    }
    activation_.procedure_position = next_position;
    return StepResult{instruction->command, *instruction->branch, next,
                      activation_.procedure_position};
}

}  // namespace ubcm
