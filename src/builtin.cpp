#include "ubcm/builtin.hpp"

#include <utility>

namespace ubcm {
namespace {

CodecError error(CodecErrorCode code, std::uint64_t position, const char* message) {
    return {code, position, message};
}

template <typename T>
CodecResult<T> forward_error(const CodecError& source) {
    return std::unexpected(source);
}

}  // namespace

CodecResult<DecodedBuiltin> decode_builtin(std::uint8_t command_value,
                                           BitCursor& procedure) {
    if (command_value > static_cast<std::uint8_t>(BuiltinCommand::get_register_size)) {
        return std::unexpected(error(CodecErrorCode::invalid_type, procedure.position(),
                                     "reserved builtin command"));
    }

    const auto command = static_cast<BuiltinCommand>(command_value);
    BitCursor input = procedure;
    BuiltinArguments arguments = NoArguments{};

    switch (command) {
        case BuiltinCommand::read_prefix:
        case BuiltinCommand::modify_prefix: {
            auto depth = decode_size(input);
            if (!depth) return forward_error<DecodedBuiltin>(depth.error());
            arguments = DepthArguments{*depth};
            break;
        }
        case BuiltinCommand::condition_prefix: {
            auto destination = decode_destination(input);
            if (!destination) return forward_error<DecodedBuiltin>(destination.error());
            arguments = ConditionArguments{std::move(*destination)};
            break;
        }
        case BuiltinCommand::branch:
        case BuiltinCommand::finish_call:
            break;
        case BuiltinCommand::compute: {
            auto operation = input.read_uint(5);
            if (!operation) return forward_error<DecodedBuiltin>(operation.error());
            std::vector<SourceOperand> sources;
            const auto source_count = *operation < 16U ? 2U : 1U;
            sources.reserve(source_count);
            for (std::uint8_t index = 0; index < source_count; ++index) {
                auto source = decode_source(input);
                if (!source) return forward_error<DecodedBuiltin>(source.error());
                sources.push_back(std::move(*source));
            }
            auto destination = decode_destination(input);
            if (!destination) return forward_error<DecodedBuiltin>(destination.error());
            arguments = ComputeArguments{static_cast<std::uint8_t>(*operation),
                                         std::move(sources),
                                         std::move(*destination)};
            break;
        }
        case BuiltinCommand::copy: {
            auto source = decode_source(input);
            if (!source) return forward_error<DecodedBuiltin>(source.error());
            auto destination = decode_destination(input);
            if (!destination) return forward_error<DecodedBuiltin>(destination.error());
            arguments = CopyArguments{std::move(*source), std::move(*destination)};
            break;
        }
        case BuiltinCommand::switch_procedure:
        case BuiltinCommand::switch_network:
        case BuiltinCommand::return_result:
        case BuiltinCommand::set_procedure_position: {
            auto source = decode_source(input);
            if (!source) return forward_error<DecodedBuiltin>(source.error());
            arguments = OneSourceArguments{std::move(*source)};
            break;
        }
        case BuiltinCommand::switch_procedure_and_network: {
            auto first = decode_source(input);
            if (!first) return forward_error<DecodedBuiltin>(first.error());
            auto second = decode_source(input);
            if (!second) return forward_error<DecodedBuiltin>(second.error());
            arguments = TwoSourceArguments{std::move(*first), std::move(*second)};
            break;
        }
        case BuiltinCommand::resize_register: {
            auto selector = decode_register_selector(input);
            if (!selector) return forward_error<DecodedBuiltin>(selector.error());
            auto size = decode_source(input);
            if (!size) return forward_error<DecodedBuiltin>(size.error());
            arguments = ResizeArguments{std::move(*selector), std::move(*size)};
            break;
        }
        case BuiltinCommand::get_register_size: {
            auto selector = decode_register_selector(input);
            if (!selector) return forward_error<DecodedBuiltin>(selector.error());
            auto destination = decode_destination(input);
            if (!destination) return forward_error<DecodedBuiltin>(destination.error());
            arguments = GetSizeArguments{std::move(*selector),
                                         std::move(*destination)};
            break;
        }
    }

    std::optional<bool> branch;
    if (command != BuiltinCommand::finish_call) {
        auto branch_bit = input.read_bit();
        if (!branch_bit) return forward_error<DecodedBuiltin>(branch_bit.error());
        branch = *branch_bit;
    }

    const auto next_position = input.position();
    if (auto committed = procedure.seek(next_position); !committed) {
        return forward_error<DecodedBuiltin>(committed.error());
    }
    return DecodedBuiltin{command, std::move(arguments), branch, next_position};
}

}  // namespace ubcm
