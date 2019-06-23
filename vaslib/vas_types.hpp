#ifndef VAS_TYPES_HPP
#define VAS_TYPES_HPP

#include <cinttypes>
#include <cstddef>
#include <limits>
#include <type_traits>

/// Combination of flags
using flags_t = uint32_t;

/// -1 for size_t
const size_t size_t_inval = std::numeric_limits< size_t >::max();



using uint = unsigned int;

#ifdef _MSC_VER
using ssize_t = std::make_signed<size_t>::type;
#endif

#endif // VAS_TYPES_HPP
