#pragma once

#include "ubcm/activation.hpp"
#include "ubcm/arithmetic.hpp"
#include "ubcm/builtin.hpp"
#include "ubcm/operand.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace ubcm {

enum class VmErrorCode {
    invalid_state,
    register_access,
    decode_error,
    mathematical_error,
    unsupported_command,
    resource_exhausted,
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

struct ActivationFrame {
    ActivationRecord activation;
    OperandResolvers resolvers;
};

class VirtualMachine {
public:
    VirtualMachine(RegisterBank& registers, ActivationRecord activation,
                   OperandResolvers resolvers = {});
    VirtualMachine(RegisterBank& registers, std::vector<ActivationFrame> activations);

    [[nodiscard]] const ActivationRecord& current_activation() const noexcept;
    [[nodiscard]] std::size_t activation_count() const noexcept;
    [[nodiscard]] VmResult<StepResult> step();

private:
    [[nodiscard]] ActivationFrame& current_frame() noexcept;
    [[nodiscard]] const ActivationFrame& current_frame() const noexcept;
    [[nodiscard]] VmResult<std::size_t> activation_index(
        std::uint64_t depth) const;
    [[nodiscard]] VmResult<StepResult> step_impl();
    [[nodiscard]] VmResult<NodeReference> read_network_state(
        const ActivationRecord& activation) const;
    [[nodiscard]] VmResult<Node> read_node(const NodeReference& reference) const;
    [[nodiscard]] VmResult<void> validate_node_target(
        const NodeReference& reference) const;
    [[nodiscard]] VmResult<void> validate_network_state_write(
        const ActivationRecord& activation) const;
    [[nodiscard]] VmResult<void> validate_resize_target(RegisterHandle handle,
                                                        std::uint64_t bit_size,
                                                        const NodeReference& current,
                                                        const NodeReference& next) const;
    [[nodiscard]] OperandResolver operand_resolver(const ActivationFrame& frame) const;

    RegisterBank* registers_;
    std::vector<ActivationFrame> activations_;
};

}  // namespace ubcm
