#pragma once

#include "ubcm/bit_cursor.hpp"
#include "ubcm/bit_vector.hpp"
#include "ubcm/codec.hpp"
#include "ubcm/codec_error.hpp"

#include <string_view>

namespace ubcm {

[[nodiscard]] std::string_view version() noexcept;

}  // namespace ubcm
