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
    return bind(name, RegisterAddress{handle, 0});
}

RegisterResult<void> NameResolver::bind(const BitVector& name, RegisterAddress address) {
    const auto handle = address.handle;
    if (handle.class_id != RegisterClass::global ||
        handle.id >= (std::uint64_t{1} << 62U)) {
        return std::unexpected(error(RegisterError::Code::invalid_handle,
                                      "resolver requires a physical global register"));
    }
    const auto [_, inserted] = names_.emplace(key(name), address);
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
    auto address = resolve_address(name);
    if (!address) return std::unexpected(address.error());
    return address->handle;
}

RegisterResult<RegisterAddress> NameResolver::resolve_address(
    const BitVector& name) const {
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
    if (class_id != RegisterClass::global && class_id != RegisterClass::procedure) {
        return std::unexpected(error(RegisterError::Code::invalid_handle,
                                      "local registers have no separate storage"));
    }
    RegisterHandle handle{class_id, 0U};
    if (class_id != RegisterClass::procedure) {
        constexpr auto max_register_id = (std::uint64_t{1} << 62U) - 1U;
        if (next_id_ > max_register_id) {
            return std::unexpected(error(RegisterError::Code::invalid_handle,
                                          "register id space is exhausted"));
        }
        handle.id = next_id_;
    }
    const auto stored_handle = handle;
    if (entries_.contains(stored_handle)) {
        return std::unexpected(error(RegisterError::Code::invalid_handle,
                                      "procedure register already exists"));
    }
    try {
        entries_.emplace(stored_handle, Entry{contents, immutable});
    } catch (const std::exception&) {
        return std::unexpected(error(RegisterError::Code::resource_exhausted,
                                      "register creation failed"));
    }
    if (class_id != RegisterClass::procedure) {
        ++next_id_;
    }
    return handle;
}

RegisterResult<void> RegisterBank::erase(RegisterHandle handle) {
    if (entries_.erase(handle) == 0U) {
        return std::unexpected(error(RegisterError::Code::not_found, "register does not exist"));
    }
    return {};
}

RegisterResult<void> RegisterBank::insert(std::uint64_t id, const BitVector& contents,
                                         bool immutable) {
    const RegisterHandle handle{RegisterClass::global, id};
    if (id >= (std::uint64_t{1} << 62) || entries_.contains(handle)) {
        return std::unexpected(error(RegisterError::Code::invalid_handle,
                                      "invalid or duplicate register ID"));
    }
    try {
        entries_.emplace(handle, Entry{contents, immutable});
    } catch (const std::exception&) {
        return std::unexpected(error(RegisterError::Code::resource_exhausted,
                                      "register import failed"));
    }
    if (id >= next_id_) next_id_ = id + 1;
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

RegisterResult<void> RegisterBank::resize_or_create(RegisterHandle handle,
                                                     std::uint64_t bit_size) {
    auto found = find(handle);
    if (found) return resize(handle, bit_size);
    if (found.error().code != RegisterError::Code::not_found) {
        return std::unexpected(found.error());
    }
    if (bit_size == 0U) return {};
    constexpr auto max_register_id = (std::uint64_t{1} << 62U) - 1U;
    if (handle.class_id != RegisterClass::global || handle.id > max_register_id) {
        return std::unexpected(error(RegisterError::Code::invalid_handle,
                                      "cannot create the requested global register"));
    }
    try {
        entries_.emplace(handle, Entry{BitVector(bit_size), false});
    } catch (const std::exception&) {
        return std::unexpected(error(RegisterError::Code::resource_exhausted,
                                      "register creation failed"));
    }
    if (handle.id >= next_id_) {
        next_id_ = handle.id + 1U;
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
    try {
        return (*found)->contents.slice(address.bit_offset, bit_count);
    } catch (const std::exception&) {
        return std::unexpected(error(RegisterError::Code::resource_exhausted,
                                      "register read allocation failed"));
    }
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

RegisterResult<void> RegisterBank::replace(RegisterHandle handle, const BitVector& value) {
    auto entry = find(handle);
    if (!entry) return std::unexpected(entry.error());
    if ((*entry)->immutable) {
        return std::unexpected(error(RegisterError::Code::immutable, "register is immutable"));
    }
    try {
        BitVector copy = value;
        std::swap((*entry)->contents, copy);
    } catch (const std::exception&) {
        return std::unexpected(error(RegisterError::Code::resource_exhausted,
                                      "register replacement failed"));
    }
    return {};
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

void RegisterBank::swap(RegisterBank& other) noexcept {
    entries_.swap(other.entries_);
    std::swap(next_id_, other.next_id_);
}

}  // namespace ubcm
