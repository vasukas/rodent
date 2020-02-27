#ifndef SERIALIZER_DEFS_HPP
#define SERIALIZER_DEFS_HPP

#include "serializer_dsl.hpp"

#include <bitset>
#include <variant>
#include "utils/color_manip.hpp"
#include "vaslib/vas_file.hpp"
#include "vaslib/vas_math.hpp"
#include "vaslib/vas_time.hpp"



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

template <int Bits>
struct SerialTag_Int {};

template <typename Tag = SerialTag_None>
struct SerialTag_Optional {};



template<> struct SerialFunc<std::string, SerialTag_None> {
	static void write(const std::string& p, File& f) {
		if (p.size() >= (1<<16)) throw std::runtime_error("String is too long");
		f.w16L(p.size());
		f.write(p.data(), p.size());
	}
	static void read(std::string& p, File& f) {
		p.resize(f.r16L());
		f.read(p.data(), p.size());
	}
};

template <typename T, std::size_t N, typename Tag>
struct SerialFunc<std::array<T, N>, Tag> {
	static void write(const T& p, File& f) {
		for (size_t i=0; i<N; ++i) SerialFunc<T, Tag>::write(p[i], f);
	}
	static void read(T& p, File& f) {
		for (size_t i=0; i<N; ++i) SerialFunc<T, Tag>::read(p[i], f);
	}
};

template <std::size_t N>
struct SerialFunc<std::bitset<N>, SerialTag_None>
{
	static constexpr auto bytes = N % 8 ? (N/8 + 1) : N/8;
	static void write(const std::bitset<N>& p, File& f) {
		for (size_t b=0; b<bytes; ++b) {
			uint8_t x = 0;
			for (size_t i=b*8; i<std::min(N,(b+1)*8); ++i) {
				x <<= 1;
				x |= p[i];
			}
			SERIALFUNC_WRITE(x, f);
		}
	}
	static void read(std::bitset<N>& p, File& f) {
		for (size_t b=0; b<bytes; ++b) {
			uint8_t x;
			SERIALFUNC_READ(x, f);
			for (int i=int(std::min(N, (b+1)*8)) - 1; i>=int(b*8); --i) {
				p[i] = x&1;
				x >>= 1;
			}
		}
	}
};



template <typename T, typename Tag>
struct SerialFunc<T, SerialTag_Optional<Tag>> {
	using ValT = typename T::value_type;
	static void write(const T& p, File& f) {
		if (p.has_value()) {
			f.w8(1);
			SerialFunc<ValT, Tag>::write(*p, f);
		}
		else f.w8(0);
	}
	static void read(T& p, File& f) {
		if (f.r8()) {
			p = ValT{};
			SerialFunc<ValT, Tag>::read(*p, f);
		}
		else p.reset();
	}
};

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
		if (static_cast<size_t>(p) >= N)
			throw std::runtime_error("Invalid enum value");
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
			SerialFunc<std::remove_const_t<std::remove_reference_t<decltype(v)>>, SerialTag_None>::write(v, f); }, p);
	}
	static void read(Var& p, File& f) {
		init_by_id(p, f.r8());
		std::visit([&](auto& v){
			SerialFunc<std::remove_reference_t<decltype(v)>, SerialTag_None>::read(v, f); }, p);
	}
};

template <typename T, int N>
struct SerialFunc<T, SerialTag_Int<N>> {
	static_assert(std::is_integral_v<T>);
	static constexpr auto bound = 1ULL << N;
	
	template <typename U = T, std::enable_if_t<std::is_unsigned_v<U>, int> = 0>
	static void write(U p, File& f) {
		if constexpr (N > 32 && N <= 64) f.w64L(p);
		else {
			if (p >= bound) throw std::runtime_error("Value is out of range");
			if      constexpr (N <= 8)  f.w8  (p);
			else if constexpr (N <= 16) f.w16L(p);
			else if constexpr (N <= 32) f.w32L(p);
			else static_assert(always_false_v<U>);
		}
	}
	
	template <typename U = T, std::enable_if_t<std::is_unsigned_v<U>, int> = 0>
	static void read(U& p, File& f) {
		if      constexpr (N <= 8)  p = f.r8  ();
		else if constexpr (N <= 16) p = f.r16L();
		else if constexpr (N <= 32) p = f.r32L();
		else if constexpr (N <= 64) p = f.r64L();
		else static_assert(always_false_v<U>);
	}
	
	template <typename U = T, std::enable_if_t<std::is_signed_v<U>, int> = 0>
	static void write(U signd, File& f) {
		auto p = std::make_unsigned_t<U>(std::abs(signd));
		if (p >= bound) throw std::runtime_error("Value is out of range");
		if (signd < 0) p |= bound;
		
		if      constexpr (N < 8)  f.w8(p);
		else if constexpr (N < 16) f.w16L(p);
		else if constexpr (N < 32) f.w32L(p);
		else if constexpr (N < 64) f.w64L(p);
		else static_assert(always_false_v<U>);
	}
	
	template <typename U = T, std::enable_if_t<std::is_signed_v<U>, int> = 0>
	static void read(U& signd, File& f) {
		std::make_unsigned_t<U> p;
		if      constexpr (N < 8)  p = f.r8  ();
		else if constexpr (N < 16) p = f.r16L();
		else if constexpr (N < 32) p = f.r32L();
		else if constexpr (N < 64) p = f.r64L();
		else static_assert(always_false_v<U>);
		
		signd = U(p & (bound - 1));
		if (p & bound) signd = -signd;
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

template<> struct SerialFunc<Transform, SerialTag_None> {
	static void write(const Transform& p, File& f) {
		SERIALFUNC_WRITE(p.pos, f);
		SERIALFUNC_WRITE(p.rot, f);
	}
	static void read(Transform& p, File& f) {
		SERIALFUNC_READ(p.pos, f);
		SERIALFUNC_READ(p.rot, f);
	}
};

template<> struct SerialFunc<vec2i, SerialTag_None> {
	static void write(const vec2i& p, File& f) {
		SerialFunc<int, SerialTag_Int<15>>::write(p.x, f);
		SerialFunc<int, SerialTag_Int<15>>::write(p.y, f);
	}
	static void read(vec2i& p, File& f) {
		SerialFunc<int, SerialTag_Int<15>>::read(p.x, f);
		SerialFunc<int, SerialTag_Int<15>>::read(p.y, f);
	}
};

template<> struct SerialFunc<Rect, SerialTag_None> {
	static void write(const Rect& p, File& f) {
		SerialFunc<vec2i, SerialTag_None>::write(p.off, f);
		SerialFunc<vec2i, SerialTag_None>::write(p.sz,  f);
	}
	static void read(Rect& p, File& f) {
		SerialFunc<vec2i, SerialTag_None>::read(p.off, f);
		SerialFunc<vec2i, SerialTag_None>::read(p.sz,  f);
	}
};

template<> struct SerialFunc<TimeSpan, SerialTag_None> {
	static void write(TimeSpan p, File& f) {f.w64L(p.micro());}
	static void read(TimeSpan& p, File& f) {p.set_micro(f.r64L());}
};

template<> struct SerialFunc<FColor, SerialTag_None> {
	using Ser = SerialFunc<FColor, SerialTag_FixedArray<4, float, SerialTag_fp_8_8>>;
	static void write(const FColor& p, File& f) {Ser::write(p, f);}
	static void read (      FColor& p, File& f) {Ser::read (p, f);}
};

#endif // SERIALIZER_DEFS_HPP
