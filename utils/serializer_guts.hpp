#ifndef SERIALIZER_GUTS_HPP
#define SERIALIZER_GUTS_HPP

#include <typeindex>
#include <unordered_map>
#include "vaslib/vas_cpp_utils.hpp"
class File;

struct SerialTag_None {};

template <typename T, typename Tag = SerialTag_None>
struct SerialFunc;



struct SerialType_Void {};
template<> struct SerialFunc<SerialType_Void, SerialTag_None> {
	static void write(SerialType_Void, File&) {}
	static void read(SerialType_Void&, File&) {}
};

struct SerialType_IndexDef {constexpr SerialType_IndexDef(int) {}};
template<> struct SerialFunc<SerialType_IndexDef, SerialTag_None> {
	static void write(SerialType_IndexDef, File&) {}
	static void read(SerialType_IndexDef&, File&) {}
};

struct SerialType_InitDef {};
template<> struct SerialFunc<SerialType_InitDef, SerialTag_None> {
	static void write(SerialType_InitDef, File&) {}
	static void read(SerialType_InitDef&, File&) {}
};

namespace serial_detail
{

using string = std::string_view;

template <typename T>
struct TypeObj {};



#define CATCH(MID)\
	catch (std::exception& e) {\
		throw std::runtime_error(std::string(name) + MID + e.what());\
	}

template <class C, typename T, typename Tag>
struct Field
{
	string name;
	T C::* ptr;
	
	constexpr Field(string name, T C::* ptr, Tag): name(name), ptr(ptr) {}
	
	void write(const C& c, File& f) const
	{
		try {SerialFunc<T, Tag>::write(c.*ptr, f);}
		CATCH(": ");
	}
	void read(C& c, File& f) const
	{
		try {SerialFunc<T, Tag>::read(c.*ptr, f);}
		CATCH(": ");
	}
};

template <class C, typename T, typename Tag>
Field(string, T C::*, Tag) -> Field<C, T, Tag>;



template <class C> using WriteF = void(*)(const C& c, File& f);
template <class C> using ReadF  = void(*)(      C& c, File& f);

template <class W, class R = W>
struct FPair
{
	WriteF<W> wf;
	ReadF <R> rf;
	
	constexpr FPair() = default;
	constexpr FPair(WriteF<W> wf, ReadF<R> rf): wf(wf), rf(rf) {}
	
	void write(const W& c, File& f) const {if (wf) wf(c, f);}
	void read       (R& c, File& f) const {if (rf) rf(c, f);}
};



// non-placement

template <class C, class Cons, class Init, std::enable_if_t<
              std::is_constructible_v<C, Cons, Init>
              ,int> = 0>
C* make_new(Cons&& ci, callable_ref<Init()> f) {
	return new C{std::forward<Cons>(ci), f()};
}

template <class C, class Cons, class Init, std::enable_if_t<
              std::is_constructible_v<C, Init>
              ,int> = 0>
C* make_new(Cons&&, callable_ref<Init()> f) {
	return new C{f()};
}

template <class C, class Cons, class Init, std::enable_if_t<
              std::is_constructible_v<C, Cons>
              ,int> = 0>
C* make_new(Cons&& ci, callable_ref<Init()>) {
	return new C{std::forward<Cons>(ci)};
}

template <class C, class Cons, class Init, std::enable_if_t<
              std::is_default_constructible_v<C> &&
              !std::is_constructible_v<C, Init> &&
              !std::is_constructible_v<C, Cons> &&
              !std::is_constructible_v<C, Cons, Init>
              ,int> = 0>
C* make_new(Cons&&, callable_ref<Init()>) {
	return new C{};
}

// placement

template <class C, class Cons, class Init, std::enable_if_t<
              std::is_constructible_v<C, Cons, Init>
              ,int> = 0>
C* make_new(void *cp, Cons&& ci, callable_ref<Init()> f) {
	return new(cp) C{std::forward<Cons>(ci), f()};
}

template <class C, class Cons, class Init, std::enable_if_t<
              std::is_constructible_v<C, Init>
              ,int> = 0>
C* make_new(void *cp, Cons&&, callable_ref<Init()> f) {
	return new(cp) C{f()};
}

template <class C, class Cons, class Init, std::enable_if_t<
              std::is_constructible_v<C, Cons>
              ,int> = 0>
C* make_new(void *cp, Cons&& ci, callable_ref<Init()>) {
	return new(cp) C{std::forward<Cons>(ci)};
}

template <class C, class Cons, class Init, std::enable_if_t<
              std::is_default_constructible_v<C> &&
              !std::is_constructible_v<C, Init> &&
              !std::is_constructible_v<C, Cons> &&
              !std::is_constructible_v<C, Cons, Init>
              ,int> = 0>
C* make_new(void *cp, Cons&&, callable_ref<Init()>) {
	return new(cp) C{};
}



template <typename IdType, class ConsInit>
struct ClassBase
{
	virtual ~ClassBase() = default;
	virtual void write(const void* c, File& f) const = 0;
	virtual void* read(File& f, ConsInit&& ci) const = 0;
	virtual void* read(void* cp, File& f, ConsInit&& ci) const = 0;
	virtual std::type_index get_typeindex() const = 0;
	virtual IdType get_id() const = 0;
};

template <typename IdType, class C, class ConsInit, class Init, typename... Ts>
struct Class : ClassBase<IdType, ConsInit>
{
	string name;
	IdType clsid;
	std::tuple<Ts...> sers;
	FPair<C> f_class;
	FPair<C, Init> f_init;
	
	constexpr Class(string name, IdType clsid, C*, TypeObj<ConsInit>, FPair<C> f_class, FPair<C, Init> f_init, std::tuple<Ts...> sers)
	    : name(name), clsid(clsid), sers(sers), f_class(f_class), f_init(f_init) {}
	
	void write(const void* c, File& f) const override {
		x_write(*static_cast<const C*>(c), f);
	}
	void* read(File& f, ConsInit&& ci) const override {
		return x_read(*new_read(f, std::forward<ConsInit>(ci)), f);
	}
	void* read(void* cp, File& f, ConsInit&& ci) const override {
		return x_read(*new_read(cp, f, std::forward<ConsInit>(ci)), f);
	}
	std::type_index get_typeindex() const override {
		return typeid(C);
	}
	IdType get_id() const override {
		return clsid;
	}
	
private:
	C* new_read(File& f, ConsInit&& ci) const {
		return make_new<C, ConsInit, Init>(std::forward<ConsInit>(ci), [&]{ Init di; initread(di, f); return di; });
	}
	C* new_read(void* cp, File& f, ConsInit&& ci) const {
		return make_new<C, ConsInit, Init>(cp, std::forward<ConsInit>(ci), [&]{ Init di; initread(di, f); return di; });
	}
	
	void x_write(const C& c, File& f) const
	{
		try {f_init.write(c, f);}
		CATCH(" - init: ");
		
		try {std::apply([&](auto& ...s){ (s.write(c, f), ...); }, sers);}
		CATCH("::");
		
		try {f_class.write(c, f);}
		CATCH(" - post: ");
	}
	void initread(Init& di, File& f) const
	{
		try {f_init.read(di, f);}
		CATCH(" - init: ");
	}
	C* x_read(C& c, File& f) const
	{
		try {std::apply([&](auto& ...s){ (s.read(c, f), ...); }, sers);}
		CATCH("::");
		
		try {f_class.read(c, f);}
		CATCH(" - post: ");
		
		return &c;
	}
};

template <typename IdType, class C, class ConsInit, class Init, typename... Ts>
Class(string, IdType, C*, TypeObj<ConsInit>, FPair<C>, FPair<C, Init>, std::tuple<Ts...>)
	-> Class<IdType, C, ConsInit, Init, Ts...>;



#undef CATCH
#define CATCH(PREF, MID)\
	catch (std::exception& e) {\
		throw std::runtime_error(std::string(PREF) + std::string(name) + MID + e.what());\
	}
#define THROW_NE(TYPE, PREF, POST)\
	throw std::TYPE##_error(std::string(PREF) + ": " + std::string(name) + ": " + POST)

template <typename IdType, class Base, class ConsInit, typename... Ts>
struct Hierarchy
{
	using SerBase = ClassBase<IdType, ConsInit>;
	
	string name;
	std::tuple<Ts...> types;
	std::unordered_map<IdType, const SerBase*> id_map;
	std::unordered_map<std::type_index, const SerBase*> type_map;
	
	Hierarchy(string p_name, IdType, Base*, TypeObj<ConsInit>, std::tuple<Ts...> p_types)
		: name(p_name), types(p_types)
	{
		std::apply([&](auto& ...t){
			(id_map.emplace(t.clsid, static_cast<const SerBase*>(std::addressof(t))), ...);
		}, types);
		
		std::apply([&](auto& ...t){
			(type_map.emplace(t.get_typeindex(), static_cast<const SerBase*>(std::addressof(t))), ...);
		}, types);
	}
	bool write_opt(const Base& p, File& f) const
	{
		auto it = type_map.find(typeid(p));
		if (it != type_map.end()) {
			x_write(**it, p, f);
			return true;
		}
		return false;
	}
	void write(const Base& p, File& f) const
	{
		const SerBase* v;
		try {v = type_map.at(typeid(p));}
		catch (std::exception&) {THROW_NE(logic, "Invalid type", typeid(p).name());}
		x_write(*v, p, f);
	}
	
	template <typename... Args>
	Base* read(File& f, Args&&... ci) const
	{
		auto id = x_read(f);
		const SerBase* v;
		try {v = id_map.at(id);}
		catch (std::exception&) {THROW_NE(runtime, "Invalid CLSID", std::to_string(id));}
		return static_cast<Base*>(x_read(*v, f, std::forward<Args>(ci)...));
	}
	
private:
	void x_write(const SerBase& v, const Base& p, File& f) const
	{
		try {SerialFunc<IdType, SerialTag_None>::write(v.get_id(), f);}
		CATCH("CLSID: ", ": ");
		
		try {v.write(static_cast<const void*>(&p), f);}
		CATCH("", "::");
	}
	IdType x_read(File& f) const
	{
		IdType id;
		try {SerialFunc<IdType, SerialTag_None>::read(id, f);}
		CATCH("CLSID: ", ": ");
		return id;
	}
	
	template <typename Arg>
	void* x_read(const SerBase& v, File& f, Arg&& ci) const
	{
		void* ret = {};
		try {ret = v.read(f, std::forward<ConsInit>(ci));}
		CATCH("", "::");
		return ret;
	}
	void* x_read(const SerBase& v, File& f) const
	{
		void* ret = {};
		try {ret = v.read(f, SerialType_Void{});}
		CATCH("", "::");
		return ret;
	}
};

template <typename IdType, class Base, class ConsInit, typename... Ts>
Hierarchy(string, IdType, Base*, TypeObj<ConsInit>, std::tuple<Ts...>) -> Hierarchy<IdType, Base, ConsInit, Ts...>;



template <typename IdType, typename... Ts>
constexpr std::tuple<Ts...> check_indices(string name, IdType, std::tuple<Ts...> types)
{
	static_assert(std::is_integral_v<IdType>);
	
	auto c2 = [](int& id_rep, auto& t, auto& r){ id_rep += (r.clsid == t.clsid); };
	auto c1 = [&](int& id_rep, auto& t){ std::apply([&](auto& ...r){(c2(id_rep, t, r), ... );}, types); };
	auto c0 = [&](auto& t){
		int id_rep = 0;
		c1(id_rep, t);
		if (id_rep != 1) THROW_NE(logic, "same CLSID repeated", std::to_string(t.get_id()) + " (" + std::string(t.name) + ")");
	};
	std::apply([&](auto& ...t){(c0(t), ...);}, types);
	
	return types;
}

#undef CATCH
#undef THROW_NE

}

#endif // SERIALIZER_GUTS_HPP
