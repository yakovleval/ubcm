#include "ubcm/registers.hpp"

#include <limits>
#include <functional>
#include <exception>

namespace ubcm {
namespace {

RegisterError error(RegisterError::Code code, const char* message) {
    return RegisterError{code, message};
}

std::string key(const BitVector& name) { return name.to_bit_string(); }

}  // namespace

std::size_t RegisterHandleHash::operator()(const RegisterHandle& handle) const noexcept {
    const auto class_value = static_cast<std::uint8_t>(handle.class_id);
    return std::hash<std::uint64_t>{}((handle.id << 2U) ^ class_value);
}

RegisterResult<void> NameResolver::bind(const BitVector& name, RegisterHandle handle) {
    if (handle.class_id == RegisterClass::procedure && handle.id != 0U) {
        return std::unexpected(error(RegisterError::Code::invalid_handle,
                                      "procedure handle must have id zero"));
    }
    const auto [_, inserted] = names_.emplace(key(name), handle);
    if (!inserted) {
        return std::unexpected(error(RegisterError::Code::duplicate_name,
                                      "register name is already bound"));
    }
    return {};
}

RegisterResult<void> NameResolver::unbind(const BitVector& name) {
    if (names_.erase(key(name)) == 0U) {
        return std::unexpected(error(RegisterError::Code::not_found,
                                      "register name is not bound"));
    }
    return {};
}

RegisterResult<RegisterHandle> NameResolver::resolve(const BitVector& name) const {
    const auto found = names_.find(key(name));
    if (found == names_.end()) {
        return std::unexpected(error(RegisterError::Code::not_found,
                                      "register name is not bound"));
    }
    return found->second;
}

bool NameResolver::contains(const BitVector& name) const { return names_.contains(key(name)); }

RegisterResult<RegisterHandle> RegisterBank::create(RegisterClass class_id,
                                                     const BitVector& contents,
                                                     bool immutable) {
    RegisterHandle handle{class_id, 0U};
    if (class_id != RegisterClass::procedure) {
        if (next_id_ == std::numeric_limits<std::uint64_t>::max()) {
            return std::unexpected(error(RegisterError::Code::invalid_handle,
                                          "register id space is exhausted"));
        }
        handle.id = next_id_++;
    }
    if (entries_.contains(handle)) {
        return std::unexpected(error(RegisterError::Code::invalid_handle,
                                      "procedure register already exists"));
    }
    entries_.emplace(handle, Entry{contents, immutable});
    return handle;
}

RegisterResult<void> RegisterBank::erase(RegisterHandle handle) {
    if (entries_.erase(handle) == 0U) {
        return std::unexpected(error(RegisterError::Code::not_found, "register does not exist"));
    }
    return {};
}

RegisterResult<void> RegisterBank::resize(RegisterHandle handle, std::uint64_t bit_size,
                                          bool fill_value) {
    const auto found = find(handle);
    if (!found) {
        return std::unexpected(found.error());
    }
    if ((*found)->immutable) {
        return std::unexpected(error(RegisterError::Code::immutable,
                                      "immutable register cannot be resized"));
    }
    if (bit_size == 0U) {
        entries_.erase(handle);
        return {};
    }
    try {
        (*found)->contents.resize(bit_size, fill_value);
    } catch (const std::exception&) {
        return std::unexpected(error(RegisterError::Code::out_of_range,
                                      "register resize failed"));
    }
    return {};
}

RegisterResult<std::uint64_t> RegisterBank::size(RegisterHandle handle) const {
    const auto found = find(handle);
    if (!found) {
        return std::unexpected(found.error());
    }
    return (*found)->contents.size();
}

RegisterResult<BitVector> RegisterBank::read(RegisterAddress address,
                                              std::uint64_t bit_count) const {
    const auto found = find(address.handle);
    if (!found) {
        return std::unexpected(found.error());
    }
    if (address.bit_offset > (*found)->contents.size() ||
        bit_count > (*found)->contents.size() - address.bit_offset) {
        return std::unexpected(error(RegisterError::Code::out_of_range,
                                      "register read is out of range"));
    }
    return (*found)->contents.slice(address.bit_offset, bit_count);
}

RegisterResult<void> RegisterBank::write(RegisterAddress address, const BitVector& value) {
    const auto found = find(address.handle);
    if (!found) {
        return std::unexpected(found.error());
    }
    if ((*found)->immutable) {
        return std::unexpected(error(RegisterError::Code::immutable,
                                      "immutable register cannot be written"));
    }
    if (address.bit_offset > (*found)->contents.size() ||
        value.size() > (*found)->contents.size() - address.bit_offset) {
        return std::unexpected(error(RegisterError::Code::out_of_range,
                                      "register write is out of range"));
    }
    (*found)->contents.write(address.bit_offset, value);
    return {};
}

RegisterResult<const BitVector*> RegisterBank::view(RegisterHandle handle) const {
    const auto found = find(handle);
    if (!found) {
        return std::unexpected(found.error());
    }
    return &(*found)->contents;
}

RegisterResult<bool> RegisterBank::is_immutable(RegisterHandle handle) const {
    const auto found = find(handle);
    if (!found) {
        return std::unexpected(found.error());
    }
    return (*found)->immutable;
}

RegisterResult<RegisterBank::Entry*> RegisterBank::find(RegisterHandle handle) {
    const auto found = entries_.find(handle);
    if (found == entries_.end()) {
        return std::unexpected(error(RegisterError::Code::not_found, "register does not exist"));
    }
    return &found->second;
}

RegisterResult<const RegisterBank::Entry*> RegisterBank::find(RegisterHandle handle) const {
    const auto found = entries_.find(handle);
    if (found == entries_.end()) {
        return std::unexpected(error(RegisterError::Code::not_found, "register does not exist"));
    }
    return &found->second;
}

}  // namespace ubcm
