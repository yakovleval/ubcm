#pragma once

#include "ubcm/bit_vector.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <unordered_map>

namespace ubcm {

enum class RegisterClass : std::uint8_t {
    procedure = 0,
    superlocal = 1,
    local = 2,
    global = 3,
};

struct RegisterHandle {
    RegisterClass class_id{RegisterClass::global};
    std::uint64_t id{};

    friend bool operator==(const RegisterHandle&, const RegisterHandle&) = default;
};

struct RegisterHandleHash {
    std::size_t operator()(const RegisterHandle& handle) const noexcept;
};

struct RegisterError {
    enum class Code {
        not_found,
        duplicate_name,
        invalid_name,
        immutable,
        out_of_range,
        invalid_handle,
    } code{};
    std::string message;
};

template <typename T>
using RegisterResult = std::expected<T, RegisterError>;

class NameResolver {
public:
    RegisterResult<void> bind(const BitVector& name, RegisterHandle handle);
    RegisterResult<void> unbind(const BitVector& name);
    [[nodiscard]] RegisterResult<RegisterHandle> resolve(const BitVector& name) const;
    [[nodiscard]] bool contains(const BitVector& name) const;

private:
    std::unordered_map<std::string, RegisterHandle> names_;
};

struct RegisterAddress {
    RegisterHandle handle;
    std::uint64_t bit_offset{};

    friend bool operator==(const RegisterAddress&, const RegisterAddress&) = default;
};

class RegisterBank {
public:
    RegisterResult<RegisterHandle> create(RegisterClass class_id,
                                           const BitVector& contents,
                                           bool immutable = false);
    RegisterResult<void> erase(RegisterHandle handle);
    RegisterResult<void> resize(RegisterHandle handle, std::uint64_t bit_size,
                                bool fill_value = false);

    [[nodiscard]] RegisterResult<std::uint64_t> size(RegisterHandle handle) const;
    [[nodiscard]] RegisterResult<BitVector> read(RegisterAddress address,
                                                  std::uint64_t bit_count) const;
    RegisterResult<void> write(RegisterAddress address, const BitVector& value);
    [[nodiscard]] RegisterResult<const BitVector*> view(RegisterHandle handle) const;
    [[nodiscard]] RegisterResult<bool> is_immutable(RegisterHandle handle) const;

private:
    struct Entry {
        BitVector contents;
        bool immutable{};
    };

    [[nodiscard]] RegisterResult<Entry*> find(RegisterHandle handle);
    [[nodiscard]] RegisterResult<const Entry*> find(RegisterHandle handle) const;

    std::uint64_t next_id_{};
    std::unordered_map<RegisterHandle, Entry, RegisterHandleHash> entries_;
};

}  // namespace ubcm
