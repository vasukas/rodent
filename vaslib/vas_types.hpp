#ifndef VAS_TYPES_HPP
#define VAS_TYPES_HPP

#include <cinttypes>
#include <cstddef>
#include <limits>

/// Combination of flags
using flags_t = uint32_t;

/// -1 for size_t
const size_t size_t_inval = std::numeric_limits< size_t >::max();



typedef unsigned int uint;

#ifdef _MSC_VER
#ifdef _WIN64
typedef __int64 ssize_t;
#else
typedef long ssize_t;
#endif
#endif

#endif // VAS_TYPES_HPP
