#ifndef SERIALIZER_DSL_HPP
#define SERIALIZER_DSL_HPP

/*
	read/write:
		SERIALFUNC_WRITE( var, file )
		SERIALFUNC_WRITE_T( var, file, tag )
		SERIALFUNC_READ( var, file )
		SERIALFUNC_READ_T( var, file, tag )
		SERIALFUNC_WRITE_H( var, file, basetype )
		SERIALFUNC_READ_NEW( type, file )
  
	function definition:
		SERIALFUNC_ALLOC_HIER( base_type, index_type, type definitions )
		SERIALFUNC_ALLOC_HIER_INIT( base_type, index_type, init_type, type definitions )
		
		SERIALFUNC_ALLOC_1( type, fields )
		SERIALFUNC_PLACEMENT_1( type, fields )
	
	type definition:
		type_header
			fields (SER_FD*)
		SER_END
		
	type headers:
		SER_BEGIN( index, type )
		SER_BEGIN_FS( index, type, post-write, post-read )
		SER_BEGIN_INIT( index, type, init_type, init-write, init-read )
		SER_BEGIN_INIT_FS( index, type, init_type, init-write, init-read, post-write, post-read )
*/

#include "serializer_guts.hpp"

namespace serial_detail
{
template <typename T, typename Tag>
void autowrite(const T& t, File& f, Tag) {SerialFunc<T, Tag>::write(t, f);}

template <typename T, typename Tag>
void autowrite(const T* t, File& f, Tag) {SerialFunc<T*, Tag>::write(t, f);}

template <typename T, typename Tag>
void autoread(T& t, File& f, Tag) {SerialFunc<T, Tag>::read(t, f);}

template <typename T, typename Tag>
void autoread(T*& t, File& f, Tag) {SerialFunc<T*, Tag>::read(t, f);}
}



#define SERIALFUNC_WRITE(VAR, FILE)\
	SERIALFUNC_WRITE_T(VAR, FILE, None)

#define SERIALFUNC_READ(VAR, FILE)\
	SERIALFUNC_READ_T(VAR, FILE, None)

#define SERIALFUNC_WRITE_T(VAR, FILE, ...)\
	serial_detail::autowrite(VAR, FILE, SerialTag_## __VA_ARGS__ {})

#define SERIALFUNC_READ_T(VAR, FILE, ...)\
	serial_detail::autoread(VAR, FILE, SerialTag_## __VA_ARGS__ {})

#define SERIALFUNC_WRITE_H(VAR, FILE, BASE)\
	SerialFunc<BASE, SerialTag_None>::write(std::addressof(VAR), FILE);

#define SERIALFUNC_READ_NEW(TYPE, FILE, ...)\
	SerialFunc<TYPE*>::read(FILE ,## __VA_ARGS__)



#define SERIALFUNC_ALLOC_HIER(INDEX_T, BASE_T, ...)\
	SERIALFUNC_ALLOC_HIER_INIT(INDEX_T, BASE_T, SerialType_Void, __VA_ARGS__)

#define SERIALFUNC_ALLOC_HIER_INIT(INDEX_T, BASE_T, INIT_T, ...)\
	template<> struct SerialFunc<BASE_T*, SerialTag_None>\
	{\
		inline static const auto ser = SER_BEGIN_HIER(INDEX_T, BASE_T, INIT_T) __VA_ARGS__ )SER_END;\
		static void write(const BASE_T* p, File& f) {ser.write(*p, f);}\
		\
		template <typename... Init>\
		static void read(BASE_T*& p, File& f, Init&&... init) {p = read(f, std::forward<Init>(init)...);}\
		\
		template <typename... Init>\
		[[nodiscard]] static BASE_T* read(File& f, Init&&... init) {\
			return static_cast<BASE_T*>(ser.read(f, std::forward<Init>(init)...));}\
	}
	
#define SERIALFUNC_ALLOC_1(TYPE, ...)\
	template<> struct SerialFunc<TYPE*, SerialTag_None>\
	{\
		inline static const auto ser = SER_BEGIN(0, TYPE) __VA_ARGS__ SER_END;\
		static void write(const TYPE* p, File& f) {ser.write(p, f);}\
		static void read(TYPE*& p, File& f) {p = read(f);}\
		[[nodiscard]] static TYPE* read(File& f) {return static_cast<TYPE*>(ser.read(f, SerialType_InitDef{}));}\
	}
	
#define SERIALFUNC_PLACEMENT_1(TYPE, ...)\
	template<> struct SerialFunc<TYPE, SerialTag_None>\
	{\
		inline static const auto ser = SER_BEGIN(0, TYPE) __VA_ARGS__ SER_END;\
		static void write(const TYPE& p, File& f) {ser.write(&p, f);}\
		static void read(TYPE& p, File& f) {ser.read(&p, f, SerialType_InitDef{});}\
	}



#define SER_BEGIN_HIER(INDEX_TYPE, BASETYPE, INITTYPE)\
	[]{ using SerialType_IndexDef = INDEX_TYPE;\
	    using SerialType_InitDef = INITTYPE;\
	return serial_detail::Hierarchy{ #BASETYPE, SerialType_IndexDef(0),\
				static_cast<BASETYPE*>(nullptr),\
				serial_detail::TypeObj<INITTYPE>{},\
				serial_detail::check_indices( #BASETYPE, SerialType_IndexDef(0),\
					std::make_tuple(

#define SER_FD(NAME) SER_FDT(NAME, None)
#define SER_FDT(NAME, ...) serial_detail::Field{#NAME, &Tn::NAME, SerialTag_## __VA_ARGS__ {}}

#define SER_END\
	)};}()



#define SER_BEGIN(INDEX, TYPENAME)\
	SER_BEGIN_INIT_FS(INDEX, TYPENAME, SerialType_Void, nullptr, nullptr, nullptr, nullptr)

#define SER_BEGIN_FS(INDEX, TYPENAME, POSTWRITE, POSTREAD)\
	SER_BEGIN_INIT_FS(INDEX, TYPENAME, SerialType_Void, nullptr, nullptr, POSTWRITE, POSTREAD)

#define SER_BEGIN_INIT(INDEX, TYPENAME, INITTYPE, INITWRITE, INITREAD)\
	SER_BEGIN_INIT_FS(INDEX, TYPENAME, INITTYPE, INITWRITE, INITREAD, nullptr, nullptr)

#define SER_BEGIN_INIT_FS(INDEX, TYPENAME, INITTYPE, INITWRITE, INITREAD, POSTWRITE, POSTREAD)\
	[]{ using Tn = TYPENAME; \
	return serial_detail::Class{ #TYPENAME, SerialType_IndexDef(INDEX),\
				static_cast<TYPENAME*>(nullptr),\
				serial_detail::TypeObj<SerialType_InitDef>{},\
				serial_detail::FPair<TYPENAME>{\
					static_cast<serial_detail::WriteF<TYPENAME>>(POSTWRITE),\
					static_cast<serial_detail::ReadF <TYPENAME>>(POSTREAD)},\
				serial_detail::FPair<TYPENAME, INITTYPE>{\
					static_cast<serial_detail::WriteF<TYPENAME>>(INITWRITE),\
					static_cast<serial_detail::ReadF <INITTYPE>>(INITREAD)},\
				std::make_tuple(

#endif // SERIALIZER_DSL_HPP
