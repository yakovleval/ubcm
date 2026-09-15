#include "ubcm/ubcm.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

void save(const std::filesystem::path& path, const ubcm::BitVector& bits) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bits.bytes().data()), bits.bytes().size());
    if (!output) throw std::runtime_error("cannot write " + path.string());
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: ubcm_demo OUTPUT_DIRECTORY");
        const std::filesystem::path directory(argv[1]);
        std::filesystem::create_directories(directory);
        const auto name = *ubcm::BitVector::from_bit_string("01010000"); // P
        const auto code = *ubcm::BitVector::from_bit_string(
            "00000000000000001110100000000000101110110000000100001010010000000000001");
        ubcm::Node compute;
        compute.command = static_cast<std::uint8_t>(ubcm::BuiltinCommand::compute);
        compute.next1 = ubcm::RuntimeReference::at({ubcm::RegisterClass::global, 1},
                                                  ubcm::node_bit_size);
        ubcm::Node finish;
        finish.command = static_cast<std::uint8_t>(ubcm::BuiltinCommand::finish_call);
        auto nodes = *ubcm::encode_node(compute);
        nodes.append(*ubcm::encode_node(finish));
        ubcm::ProgramImage image{
            {ubcm::RegisterClass::global, 0},
            ubcm::RuntimeReference::at({ubcm::RegisterClass::global, 1}, 0),
            {{0, 1, name, {}},
             {1, 2, *ubcm::BitVector::from_bit_string("01001110"), nodes},
             {2, 0, *ubcm::BitVector::from_bit_string("01010010"), ubcm::BitVector(32)}}};
        auto network = ubcm::encode_program(image);
        if (!network) throw std::runtime_error(network.error().message);
        save(directory / "network.ubcm", *network);
        save(directory / "procedure.ubcp", ubcm::encode_procedure({name, code}));
        image.registers[0].contents = code;
        save(directory / "combined.ubcm", *ubcm::encode_program(image));
        auto handler = ubcm::encode_source({std::uint64_t{2}, 0, true});
        handler.append(ubcm::encode_source({std::uint64_t{0}, 0, true}));
        ubcm::Node resolve;
        resolve.command = static_cast<std::uint8_t>(ubcm::BuiltinCommand::resolve_return);
        ubcm::Node call;
        call.kind = ubcm::NodeKind::procedure_call;
        call.procedure = {ubcm::RegisterClass::global, 3};
        call.entry = ubcm::RuntimeReference::at({ubcm::RegisterClass::global, 4},
                                               ubcm::node_bit_size);
        call.next0 = ubcm::RuntimeReference::at({ubcm::RegisterClass::global, 4},
                                               ubcm::node_bit_size * 2);
        auto resolver_nodes = *ubcm::encode_node(call);
        resolver_nodes.append(*ubcm::encode_node(resolve));
        resolver_nodes.append(*ubcm::encode_node(finish));
        image.registers.push_back({3, 1, *ubcm::BitVector::from_bit_string("01001000"), handler});
        image.registers.push_back({4, 2, *ubcm::BitVector::from_bit_string("01010011"),
                                    resolver_nodes});
        save(directory / "resolver.ubcm", *ubcm::encode_program(image));
        std::cout << "example: 5 + 3, result in global register 2\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
