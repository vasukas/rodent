#ifndef SERIALIZER_HPP
#define SERIALIZER_HPP

// #define SERIALIZER_DSL
// for simple init macros (defined at the end of the file, with example)

#include <typeindex>
#include <unordered_map>
#include <vector>
#include "vaslib/vas_file.hpp"

struct SerialType_Void {};
struct SerialTag_None {};

template <typename T, typename Tag = SerialTag_None>
struct SerialFunc;



struct SerialInfo_Class_Base
{
	virtual void write(const void* c, File& f) const = 0;
	virtual void* read(File& f) const = 0;
	virtual void read(void* cp, File& f) const = 0; ///< Placement new
};

template <class C, typename T, typename Tag>
struct SerialInfo_Field
{
	T C::* ptr;
	
	constexpr SerialInfo_Field(T C::* ptr): ptr(ptr) {}
	constexpr SerialInfo_Field(T C::* ptr, Tag): ptr(ptr) {}
	void write(const C& c, File& f) const {
		SerialFunc<T, Tag>::write(c.*ptr, f);
	}
	void read(C& c, File& f) const {
		SerialFunc<T, Tag>::read(c.*ptr, f);
	}
};

template <typename IdType, class C, typename... Ts>
struct SerialInfo_Class : SerialInfo_Class_Base
{
	IdType clsid;
	std::tuple<Ts...> sers;
	
	constexpr SerialInfo_Class(IdType clsid, C*, std::tuple<Ts...> sers): clsid(clsid), sers(sers) {}
	void write(const void* c, File& f) const override {
		SerialFunc<IdType, SerialTag_None>::write(clsid, f);
		std::apply([&](auto& ...s){ (s.write(*static_cast<const C*>(c), f), ...); }, sers);
	}
	void* read(File& f) const override {
		C* c = new C;
		std::apply([&](auto& ...s){ (s.read(*c, f), ...); }, sers);
		return c;
	}
	void read(void* cp, File& f) const override {
		C* c = new(cp) C;
		std::apply([&](auto& ...s){ (s.read(*c, f), ...); }, sers);
	}
	std::type_index get_typeindex() const {return typeid(C);}
};

template <class C, typename T>
SerialInfo_Field(T C::*) -> SerialInfo_Field<C, T, SerialTag_None>;

template <class C, typename T, typename Tag>
SerialInfo_Field(T C::*, Tag) -> SerialInfo_Field<C, T, Tag>;

template <typename IdType, class C, typename... Ts>
SerialInfo_Class(IdType, C*, std::tuple<Ts...>) -> SerialInfo_Class<IdType, C, Ts...>;



template <typename IdType, class Base, typename... Ts>
struct SerialInfo_Hierarchy
{
	constexpr SerialInfo_Hierarchy(IdType, Base*, std::tuple<Ts...> types) : types(types) {
		std::apply([&](auto& ...t){
			(msg_typemap_id.emplace(t.clsid, (SerialInfo_Class_Base*) std::addressof(t)), ...); }
			, types);
		std::apply([&](auto& ...t){
			(msg_typemap_type.emplace(t.get_typeindex(), (SerialInfo_Class_Base*) std::addressof(t)), ...); }
			, types);
	}
	Base* read(File& f) const {
		IdType id;
		SerialFunc<IdType, SerialTag_None>::read(id, f);
		return static_cast<Base*>( msg_typemap_id.at(id)->read(f) );
	}
	void write(const Base& p, File& f) const {
		msg_typemap_type.at(typeid(p))->write(&p, f);
	}
	
private:
	std::tuple<Ts...> types;
	std::unordered_map<uint8_t, SerialInfo_Class_Base*> msg_typemap_id;
	std::unordered_map<std::type_index, SerialInfo_Class_Base*> msg_typemap_type;
};

template <typename IdType, class Base, typename... Ts>
SerialInfo_Hierarchy(IdType, Base*, std::tuple<Ts...>) -> SerialInfo_Hierarchy<IdType, Base, Ts...>;



template<> struct SerialFunc<SerialType_Void, SerialTag_None> {
	static void write(SerialType_Void, File&) {}
	static void read(SerialType_Void&, File&) {}
};

#endif // SERIALIZER_HPP



#if defined(SERIALIZER_DSL) && !defined(SERIALIZER_DSL_DEFINED)
#define SERIALIZER_DSL_DEFINED

/*
   	Example:
		struct Data { int a, b; };
		
		SER_SERIALFUNC_PLACEMENT(Data,
			SER_BEGIN_NO_INDEX(Data)
				SER_FD(a),
				SER_FDT(b, int8_bit)
			SER_END)
			
		void write(const Data& d, File& f) {
			SerialFunc<Data>::write(d, f);
		}
		
	Example:
		struct Char { virtual ~Char() = default; };
		struct Alpha : Char { int   x; };
		struct Beta  : Char { float y; };
		
		SER_SERIALFUNC_ALLOC(Char,
			SER_BEGIN_HIER(uint8_t, Char)
				SER_BEGIN(0, Alpha)
					SER_FD(x)
				SER_END
				SER_BEGIN(1, Beta)
					SER_FD(y)
				SER_END
			SER_END)
*/

#define SER_BEGIN_HIER(INDEX_TYPE, BASE_TYPE)\
	[]{ using Ix = INDEX_TYPE; \
	return SerialInfo_Hierarchy{ Ix{}, static_cast<BASE_TYPE*>(nullptr), std::make_tuple(

#define SER_BEGIN(INDEX, TYPENAME)\
	[]{ using Tn = TYPENAME; \
	return SerialInfo_Class{ Ix(INDEX), static_cast<TYPENAME*>(nullptr), std::make_tuple(

#define SER_BEGIN_NO_INDEX(TYPENAME)\
	[]{ using Tn = TYPENAME; \
	return SerialInfo_Class{ SerialType_Void{}, static_cast<TYPENAME*>(nullptr), std::make_tuple(

#define SER_FD(NAME) SerialInfo_Field{&Tn::NAME}
#define SER_FDT(NAME, ...) SerialInfo_Field{&Tn::NAME, SerialTag_## __VA_ARGS__ {}}

#define SER_END\
	)};}()

#define SER_SERIALFUNC_ALLOC(TYPE, ...)\
	template<> struct SerialFunc<TYPE*, SerialTag_None> {\
		static constexpr auto ser = __VA_ARGS__;\
		static void write(const TYPE* p, File& f) {ser.write(p, f);}\
		static void read(TYPE*& p, File& f) {p = ser.read(f);}\
	};

#define SER_SERIALFUNC_PLACEMENT(TYPE, ...)\
	template<> struct SerialFunc<TYPE, SerialTag_None> {\
		static constexpr auto ser = __VA_ARGS__;\
		static void write(const TYPE& p, File& f) {ser.write(&p, f);}\
		static void read(TYPE& p, File& f) {ser.read(&p, f);}\
	};

#endif // SERIALIZER_DSL
