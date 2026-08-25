#pragma once

#include "ubcm/bit_cursor.hpp"
#include "ubcm/bit_vector.hpp"
#include "ubcm/codec.hpp"
#include "ubcm/codec_error.hpp"
#include "ubcm/addressing.hpp"
#include "ubcm/registers.hpp"
#include "ubcm/runtime_reference.hpp"
#include "ubcm/node.hpp"
#include "ubcm/activation.hpp"
#include "ubcm/builtin.hpp"
#include "ubcm/operand.hpp"
#include "ubcm/vm.hpp"

#include <string_view>

namespace ubcm {

[[nodiscard]] std::string_view version() noexcept;

}  // namespace ubcm
