#include "ubcm/vm.hpp"

#include <utility>

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

}  // namespace

VirtualMachine::VirtualMachine(RegisterBank& registers, ActivationRecord activation)
    : registers_(&registers), activation_(std::move(activation)) {}

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
    if (instruction->command != BuiltinCommand::branch) {
        return std::unexpected(error(VmErrorCode::unsupported_command,
                                     "builtin effect belongs to the next VM stage"));
    }

    const auto next = *instruction->branch ? node->next1 : node->next0;
    if (auto valid = validate_node_target(next); !valid) {
        return std::unexpected(valid.error());
    }
    auto encoded_next = encode_runtime_reference(next);
    if (!encoded_next) {
        return std::unexpected(codec_error(encoded_next.error()));
    }

    auto written = registers_->write(
        {activation_.network_state.handle, activation_.network_state.bit_offset},
        *encoded_next);
    if (!written) {
        return std::unexpected(register_error(written.error()));
    }
    activation_.procedure_position = instruction->next_procedure_position;
    return StepResult{instruction->command, *instruction->branch, next,
                      activation_.procedure_position};
}

}  // namespace ubcm
