#include "ubcm/ubcm.hpp"

#include <iostream>
#include <charconv>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {

std::uint64_t number(std::string_view text) {
    std::uint64_t result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        throw std::runtime_error("invalid unsigned number: " + std::string(text));
    }
    return result;
}

ubcm::BitVector read_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open " + path);
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
    if (input.bad()) throw std::runtime_error("cannot read " + path);
    if (bytes.size() > std::numeric_limits<std::uint64_t>::max() / 8) {
        throw std::runtime_error("file is too large");
    }
    const auto size = static_cast<std::uint64_t>(bytes.size()) * 8;
    auto bits = ubcm::BitVector::from_bytes(std::move(bytes), size);
    if (!bits) throw std::runtime_error(bits.error().message);
    return std::move(*bits);
}

ubcm::NodeReference node_address(std::string_view text) {
    const auto colon = text.find(':');
    if (colon == text.npos) throw std::runtime_error("expected resolver ID:BIT_OFFSET");
    const auto id = number(text.substr(0, colon));
    const auto offset = number(text.substr(colon + 1));
    if (id >= (std::uint64_t{1} << 62) || offset % ubcm::node_bit_size != 0) {
        throw std::runtime_error("invalid resolver node address");
    }
    return ubcm::RuntimeReference::at({ubcm::RegisterClass::global, id}, offset);
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--version")) {
            std::cout << "ubcm " << ubcm::version() << '\n';
            return 0;
        }
        std::vector<std::string> paths;
        std::uint64_t steps = 1000000, depth = 256;
        std::optional<std::uint64_t> dump;
        ubcm::NodeReference global_resolver, local_resolver;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--help") {
                std::cout << "ubcm [procedure.ubcp] network.ubcm "
                             "[--steps N] [--depth N] [--dump ID] "
                             "[--global-resolver ID:OFFSET] [--local-resolver ID:OFFSET]\n";
                return 0;
            }
            if (arg == "--global-resolver" || arg == "--local-resolver") {
                if (++i == argc) throw std::runtime_error("missing resolver address");
                const auto ref = node_address(argv[i]);
                if (arg == "--global-resolver") global_resolver = ref;
                else local_resolver = ref;
            } else if (arg == "--steps" || arg == "--depth" || arg == "--dump") {
                if (++i == argc) throw std::runtime_error("missing option value");
                const auto value = number(argv[i]);
                if (arg == "--steps") steps = value;
                else if (arg == "--depth") depth = value;
                else dump = value;
            } else if (arg.starts_with("--")) {
                throw std::runtime_error("unknown option: " + std::string(arg));
            } else paths.emplace_back(arg);
        }
        if (paths.empty() || paths.size() > 2) {
            throw std::runtime_error("expected a network file, optionally preceded by a procedure");
        }
        auto program = ubcm::decode_program(read_file(paths.back()));
        if (!program) throw std::runtime_error(program.error().message);
        if (paths.size() == 2) {
            auto procedure = ubcm::decode_procedure(read_file(paths.front()));
            if (!procedure) throw std::runtime_error(procedure.error().message);
            for (auto& record : program->registers) {
                if (record.id != program->procedure.id) continue;
                if (record.name != procedure->name) {
                    throw std::runtime_error("procedure name does not match the network root");
                }
                record.contents = std::move(procedure->contents);
            }
        }
        auto loaded = ubcm::load_program(*program);
        if (!loaded) throw std::runtime_error(loaded.error().message);
        auto state = loaded->registers.create(ubcm::RegisterClass::global,
                                              *ubcm::encode_runtime_reference(loaded->entry));
        if (!state) throw std::runtime_error(state.error().message);
        ubcm::ActivationRecord activation;
        activation.procedure = loaded->procedure;
        activation.network_state = ubcm::RuntimeReference::at(*state, 0);
        activation.local_resolver = local_resolver;
        if (!local_resolver.null) {
            auto bits = loaded->registers.read({local_resolver.handle, local_resolver.bit_offset},
                                               ubcm::node_bit_size);
            if (!bits || !ubcm::decode_node(*bits)) {
                throw std::runtime_error("invalid local resolver node");
            }
        }
        ubcm::VirtualMachine vm(loaded->registers, activation, {.global = &loaded->names});
        auto configured = vm.set_global_resolver(global_resolver);
        if (!configured) throw std::runtime_error(configured.error().message);
        auto result = vm.run(steps, depth);
        if (!result) throw std::runtime_error(result.error().message);
        std::cout << (result->halted ? "halted" : "step limit reached")
                  << " after " << result->steps << " steps\n";
        if (dump) {
            auto bits = loaded->registers.view({ubcm::RegisterClass::global, *dump});
            if (!bits) throw std::runtime_error(bits.error().message);
            std::cout << "register " << *dump << ": " << (*bits)->to_bit_string() << '\n';
        }
        return result->halted ? 0 : 2;
    } catch (const std::exception& e) {
        std::cerr << "ubcm: " << e.what() << '\n';
        return 1;
    }
}
