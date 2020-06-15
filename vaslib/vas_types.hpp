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

template <typename T> struct always_false : std::false_type {};
template <typename T> inline constexpr bool always_false_v = always_false<T>::value;



template <typename T, std::enable_if_t<!std::is_pointer_v<std::remove_cv_t<T>>, int> = 0>
constexpr int pointer_level() {return 0;}
template <typename T, std::enable_if_t<std::is_pointer_v<std::remove_cv_t<T>>, int> = 0>
constexpr int pointer_level() {return 1 + pointer_level<std::remove_pointer_t<std::remove_cv_t<T>>>();}

template <typename T> struct is_pointer_to_const : std::is_const<std::remove_pointer_t<std::remove_cv_t<T>>> {};
template <typename T> inline constexpr bool is_pointer_to_const_v = is_pointer_to_const<T>::value;

template <typename T, typename U, std::enable_if_t<is_pointer_to_const_v<T> && is_pointer_to_const_v<U>, int> = 0>
T pointer_cast(U p) {
	static_assert(pointer_level<T>() == pointer_level<U>() && pointer_level<T>() != 0,
		"pointer_cast() used on pointers of different levels or on non-pointers");
	return reinterpret_cast<T>(p);
}

template <typename T, typename U, std::enable_if_t<!is_pointer_to_const_v<T> && !is_pointer_to_const_v<U>, int> = 0>
T pointer_cast(U p) {
	static_assert(pointer_level<T>() == pointer_level<U>() && pointer_level<T>() != 0,
		"pointer_cast() used on pointers of different levels or on non-pointers");
	return reinterpret_cast<T>(p);
}

#endif // VAS_TYPES_HPP
