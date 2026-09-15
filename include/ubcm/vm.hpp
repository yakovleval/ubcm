#pragma once

#include "ubcm/activation.hpp"
#include "ubcm/arithmetic.hpp"
#include "ubcm/builtin.hpp"
#include "ubcm/operand.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <string>
#include <utility>
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
    std::optional<BuiltinCommand> command;
    bool branch{};
    NodeReference next_node{};
    std::uint64_t procedure_position{};
    bool called{};
    bool halted{};
};

struct ActivationFrame {
    ActivationRecord activation;
    OperandResolvers resolvers;
    RuntimeReference storage;
};

class VirtualMachine {
public:
    VirtualMachine(RegisterBank& registers, ActivationRecord activation,
                   OperandResolvers resolvers = {});
    VirtualMachine(RegisterBank& registers, std::vector<ActivationFrame> activations);

    // These accessors expose the snapshot from construction or the last
    // successful step. Each step rebuilds it from the stored activation chain.
    [[nodiscard]] const ActivationRecord& current_activation() const;
    [[nodiscard]] bool halted() const noexcept;
    [[nodiscard]] VmResult<const ActivationRecord*> activation_at_depth(
        std::uint64_t depth) const;
    [[nodiscard]] VmResult<RuntimeReference> activation_storage_at_depth(
        std::uint64_t depth) const;
    [[nodiscard]] std::size_t activation_count() const noexcept;
    [[nodiscard]] VmResult<StepResult> step();

private:
    [[nodiscard]] ActivationFrame& current_frame() noexcept;
    [[nodiscard]] const ActivationFrame& current_frame() const noexcept;
    [[nodiscard]] VmResult<std::size_t> activation_index(
        std::uint64_t depth) const;
    [[nodiscard]] VmResult<StepResult> step_impl();
    [[nodiscard]] VmResult<void> reload_activations();
    [[nodiscard]] VmResult<void> persist_activation(std::size_t index);
    [[nodiscard]] VmResult<void> enter_call(RegisterHandle procedure,
        std::optional<NodeReference> entry, bool owns_procedure = false);
    [[nodiscard]] VmResult<StepResult> finish_call();
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
    RuntimeReference current_storage_;
    std::map<std::pair<std::uint64_t, std::uint64_t>, OperandResolvers> resolvers_;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::vector<RegisterHandle>> owned_;
    std::vector<ActivationFrame> activations_;
};

}  // namespace ubcm
