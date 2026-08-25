#pragma once

#include "ubcm/addressing.hpp"

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace ubcm {

enum class BuiltinCommand : std::uint8_t {
    read_prefix = 0x0,
    modify_prefix = 0x1,
    condition_prefix = 0x2,
    branch = 0x3,
    compute = 0x4,
    copy = 0x5,
    switch_procedure = 0x6,
    switch_network = 0x7,
    switch_procedure_and_network = 0x8,
    return_result = 0x9,
    set_procedure_position = 0xa,
    finish_call = 0xb,
    resize_register = 0xc,
    get_register_size = 0xd,
};

struct NoArguments {};
struct DepthArguments { std::uint64_t depth{}; };
struct ConditionArguments { DestinationOperand condition; };
struct ComputeArguments {
    std::uint8_t operation{};
    std::vector<SourceOperand> sources;
    DestinationOperand destination;
};
struct CopyArguments { SourceOperand source; DestinationOperand destination; };
struct OneSourceArguments { SourceOperand source; };
struct TwoSourceArguments { SourceOperand first; SourceOperand second; };
struct ResizeArguments { RegisterSelector selector; SourceOperand size; };
struct GetSizeArguments { RegisterSelector selector; DestinationOperand destination; };

using BuiltinArguments = std::variant<NoArguments, DepthArguments,
                                      ConditionArguments, ComputeArguments,
                                      CopyArguments, OneSourceArguments,
                                      TwoSourceArguments, ResizeArguments,
                                      GetSizeArguments>;

struct DecodedBuiltin {
    BuiltinCommand command{};
    BuiltinArguments arguments;
    std::optional<bool> branch;
    std::uint64_t next_procedure_position{};
};

[[nodiscard]] CodecResult<DecodedBuiltin> decode_builtin(
    std::uint8_t command, BitCursor& procedure);

}  // namespace ubcm
