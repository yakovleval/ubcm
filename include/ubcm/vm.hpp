#pragma once

#include "ubcm/activation.hpp"
#include "ubcm/arithmetic.hpp"
#include "ubcm/builtin.hpp"
#include "ubcm/operand.hpp"

#include <cstdint>
#include <expected>
#include <string>

namespace ubcm {

enum class VmErrorCode {
    invalid_state,
    register_access,
    decode_error,
    mathematical_error,
    unsupported_command,
};

struct VmError {
    VmErrorCode code{};
    std::string message;
};

template <typename T>
using VmResult = std::expected<T, VmError>;

struct StepResult {
    BuiltinCommand command{};
    bool branch{};
    NodeReference next_node{};
    std::uint64_t procedure_position{};
};

class VirtualMachine {
public:
    VirtualMachine(RegisterBank& registers, ActivationRecord activation,
                   OperandResolvers resolvers = {});

    [[nodiscard]] const ActivationRecord& current_activation() const noexcept;
    [[nodiscard]] VmResult<StepResult> step();

private:
    [[nodiscard]] VmResult<NodeReference> read_network_state() const;
    [[nodiscard]] VmResult<Node> read_node(const NodeReference& reference) const;
    [[nodiscard]] VmResult<void> validate_node_target(
        const NodeReference& reference) const;
    [[nodiscard]] VmResult<void> validate_network_state_write() const;
    [[nodiscard]] VmResult<void> validate_resize_target(RegisterHandle handle,
                                                        std::uint64_t bit_size,
                                                        const NodeReference& current,
                                                        const NodeReference& next) const;
    [[nodiscard]] OperandResolver operand_resolver() const;

    RegisterBank* registers_;
    ActivationRecord activation_;
    OperandResolvers resolvers_;
};

}  // namespace ubcm
