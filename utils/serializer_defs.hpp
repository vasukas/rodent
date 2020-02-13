#ifndef SERIALIZER_DEFS_HPP
#define SERIALIZER_DEFS_HPP

#define SERIALIZER_DSL

#include <variant>
#include "vaslib/vas_math.hpp"
#include "vaslib/vas_time.hpp"
#include "serializer.hpp"



template <typename T> struct remove_const_ptr : std::remove_const<T> {};
template <typename T> using remove_const_ptr_t = typename remove_const_ptr<T>::type;

template <typename T> struct remove_const_ptr<T*> {
	typedef remove_const_ptr_t<T>* type;
};
template <typename T> struct remove_const_ptr<T* const> {
	typedef remove_const_ptr_t<T>* type;
};

#define SER_REMOVE_CONSTREF(TYPE)\
	remove_const_ptr_t <std::remove_reference_t <TYPE>>

#define SERIALFUNC_WRITE(VAR, FILE)\
	SerialFunc <SER_REMOVE_CONSTREF(decltype(VAR))> ::write(VAR, FILE)

#define SERIALFUNC_READ(VAR, FILE)\
	SerialFunc <SER_REMOVE_CONSTREF(decltype(VAR))> ::read(VAR, FILE)

#define SERIALFUNC_WRITE_T(VAR, FILE, ...)\
	SerialFunc <SER_REMOVE_CONSTREF(decltype(VAR))>, SerialTag_##__VA_ARGS__> ::write(VAR, FILE)

#define SERIALFUNC_READ_T(VAR, FILE, ...)\
	SerialFunc <SER_REMOVE_CONSTREF(decltype(VAR))>, SerialTag_##__VA_ARGS__> ::read(VAR, FILE)

#define SERIALFUNC_READ_NEW(TYPE, FILE)\
	SerialFunc <TYPE*>::read_new(FILE)



template<> struct SerialFunc<bool, SerialTag_None> {
	static void write(bool p, File& f) {f.w8(p);}
	static void read(bool& p, File& f) {p = f.r8();}
};

template<> struct SerialFunc<char, SerialTag_None> {
	static void write(char p, File& f) {f.w8(p);}
	static void read(char& p, File& f) {p = f.r8();}
};

template<> struct SerialFunc<uint8_t, SerialTag_None> {
	static void write(uint8_t p, File& f) {f.w8(p);}
	static void read(uint8_t& p, File& f) {p = f.r8();}
};

template<> struct SerialFunc<uint16_t, SerialTag_None> {
	static void write(uint16_t p, File& f) {f.w16L(p);}
	static void read(uint16_t& p, File& f) {p = f.r16L();}
};

template<> struct SerialFunc<uint32_t, SerialTag_None> {
	static void write(uint32_t p, File& f) {f.w32L(p);}
	static void read(uint32_t& p, File& f) {p = f.r32L();}
};

template<> struct SerialFunc<float, SerialTag_None> {
	static void write(float p, File& f) {f.wpif(p);}
	static void read(float& p, File& f) {p = f.rpif();}
};



/// Fixed-size array. Uses index operator
template <size_t N, typename Base, typename Tag = SerialTag_None>
struct SerialTag_FixedArray {};

/// Stores size as 32-bit value
template <typename Tag = SerialTag_None>
struct SerialTag_Array32 {};

template <size_t N>
struct SerialTag_Enum {};



template <typename T, size_t N, typename Base, typename Tag>
struct SerialFunc<T, SerialTag_FixedArray<N, Base, Tag>> {
	static void write(const T& p, File& f) {
		for (size_t i=0; i<N; ++i) SerialFunc<Base, Tag>::write(p[i], f);
	}
	static void read(T& p, File& f) {
		for (size_t i=0; i<N; ++i) SerialFunc<Base, Tag>::read(p[i], f);
	}
};

template <typename T, typename Tag>
struct SerialFunc<T, SerialTag_Array32<Tag>> {
	static void write(const T& p, File& f) {
		auto size = p.size();
		f.w32L(size);
		for (auto& v : p) SerialFunc<typename T::value_type, Tag>::write(v, f);
	}
	static void read(T& p, File& f) {
		auto size = f.r32L();
		p.resize(size);
		for (auto& v : p) SerialFunc<typename T::value_type, Tag>::read(v, f);
	}
};

template <typename T, size_t N>
struct SerialFunc<T, SerialTag_Enum<N>> {
	static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
	static void write(T p, File& f) {
		if      constexpr (N < (1ULL << 8))  f.w8  (static_cast<uint8_t >(p));
		else if constexpr (N < (1ULL << 16)) f.w16L(static_cast<uint16_t>(p));
		else if constexpr (N < (1ULL << 32)) f.w32L(static_cast<uint32_t>(p));
		else                                 f.w64L(static_cast<uint64_t>(p));
	}
	static void read(T& p, File& f) {
		if      constexpr (N < (1ULL << 8))  p = static_cast<T>(f.r8  ());
		else if constexpr (N < (1ULL << 16)) p = static_cast<T>(f.r16L());
		else if constexpr (N < (1ULL << 32)) p = static_cast<T>(f.r32L());
		else                                 p = static_cast<T>(f.r64L());
		if (static_cast<size_t>(p) >= N)
			throw std::runtime_error("Invalid enum value");
	}
};

template <typename... Ts>
struct SerialFunc<std::variant<Ts...>, SerialTag_None>
{
	using Var = std::variant<Ts...>;
	static constexpr auto N = std::variant_size_v<Var>;
	static_assert(N < 256);
	
	template <size_t I>
	static void init(Var& v, size_t i) {
		if (i == I) v = std::variant_alternative_t<I, Var>{};
	}
	template <std::size_t... Is>
	static void iter(std::index_sequence<Is...>, Var& v, size_t i) {
		(init<Is>(v, i), ...);
	}
	static void init_by_id(Var& v, size_t i) {
		if (i >= N) throw std::runtime_error("Invalid variant index");
		iter(std::make_index_sequence<N>{}, v, i);
	}
	
	static void write(const Var& p, File& f) {
		if (p.valueless_by_exception()) throw std::runtime_error("Variant is valueless-by-exception");
		f.w8(p.index());
		std::visit([&](auto& v){
			SerialFunc<SER_REMOVE_CONSTREF(decltype(v)), SerialTag_None>::write(v, f); }, p);
	}
	static void read(Var& p, File& f) {
		init_by_id(p, f.r8());
		std::visit([&](auto& v){
			SerialFunc<SER_REMOVE_CONSTREF(decltype(v)), SerialTag_None>::read(v, f); }, p);
	}
};



struct SerialTag_8rad {}; ///< [0; 2*pi] mapped to 8 bits
struct SerialTag_fp_8_8 {}; ///< Fixed-point 8.8
struct SerialTag_fp_16_8 {}; ///< Fixed-point 16.8

template<> struct SerialFunc<float, SerialTag_8rad> {
	static void write(float p, File& f) {f.w8( wrap_angle_2(p) * 255 / (2*M_PI) );}
	static void read(float& p, File& f) {p = f.r8() * (2*M_PI) / 255;}
};

template<> struct SerialFunc<float, SerialTag_fp_8_8> {
	static void write(float p, File& f) {f.w8(p); f.w8(int(p) << 8 & 0xff);}
	static void read(float& p, File& f) {p = f.r8(); p += f.r8() / 256.f;}
};

template<> struct SerialFunc<float, SerialTag_fp_16_8> {
	static void write(float p, File& f) {f.w16L(p); f.w8(int(p) << 8 & 0xff);}
	static void read(float& p, File& f) {p = f.r16L(); p += f.r8() / 256.f;}
};

template <typename Tag>
struct SerialFunc<vec2fp, Tag> {
	static void write(const vec2fp& p, File& f) {
		SerialFunc<float, Tag>::write(p.x, f);
		SerialFunc<float, Tag>::write(p.y, f);
	}
	static void read(vec2fp& p, File& f) {
		SerialFunc<float, Tag>::read(p.x, f);
		SerialFunc<float, Tag>::read(p.y, f);
	}
};

template<> struct SerialFunc<TimeSpan, SerialTag_None> {
	static void write(TimeSpan p, File& f) {f.w64L(p.micro());}
	static void read(TimeSpan& p, File& f) {p.set_micro(f.r64L());}
};

#endif // SERIALIZER_DEFS_HPP
