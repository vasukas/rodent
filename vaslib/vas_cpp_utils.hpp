#ifndef VAS_CPP_UTILS_HPP
#define VAS_CPP_UTILS_HPP

#include <algorithm>
#include <functional>
#include <optional>
#include <vector>
#include "vaslib/vas_types.hpp"



template <typename T>
struct ptr_range
{
	typedef T value_type;
	typedef T mut_t;
	typedef const T const_t;
	
	ptr_range(T* p, size_t n): p(p), n(n) {}
	
	template <typename Vec>
	ptr_range(Vec& vec): p(vec.data()), n(vec.size()) {}
	
	mut_t* begin() {return p;}
	mut_t* end()   {return p + n;}
	
	const_t* cbegin() const {return p;}
	const_t* cend()   const {return p + n;}
	
	bool  empty() const {return n == 0;}
	size_t size() const {return n;}
	
	mut_t&   operator[] (size_t i)       {return p[i];}
	const_t& operator[] (size_t i) const {return p[i];}
	
	mut_t*   data()       {return p;}
	const_t* data() const {return p;}
	
private:
	T* p;
	size_t n;
};

template <typename Vec>
ptr_range<typename Vec::value_type> ptr_range_offset(Vec& vec, size_t off, std::optional<size_t> num = {}) {
	return ptr_range(vec.data() + off, (num? *num : vec.size() - off)); }

template <typename Vec>
ptr_range<typename Vec::value_type> ptr_range_slice(Vec& vec, size_t begin, size_t end) {
	return ptr_range(vec.data() + begin, end - begin); }



/// Calls function before exiting the scope
struct RAII_Guard
{
	RAII_Guard() {}
	RAII_Guard( std::function <void()> foo ): foo( std::move(foo) ) {}
	~RAII_Guard()  { if (foo) foo(); }
	void trigger() { if (foo) foo(); cancel(); } ///< Calls function and resets it
	void cancel()  { foo = {}; } ///< Resets function without executing
	
	RAII_Guard( const RAII_Guard& ) = delete;
	void operator =( const RAII_Guard& ) = delete;
	
	RAII_Guard( RAII_Guard&& g ) noexcept { foo = std::move(g.foo); g.foo = {}; }
	void operator =( RAII_Guard&& g ) noexcept { foo = std::move(g.foo); g.foo = {}; }
	
	operator bool() {return foo.operator bool();}
	std::function <void()> copy_func() { return foo; }
	std::function <void()> release() { auto f = std::move(foo); foo = {}; return f; }
	
private:
	std::function <void()> foo;
};



/// Reserves more if capacity exhausted (STL containers)
template <typename T>
void reserve_more_block( T& vs, size_t required )
{
	if (vs.size() == vs.capacity())
		vs.reserve( vs.size() + required );
}

/// Reserves more in addition to current size (STL containers)
template <typename T>
void reserve_more( T& vs, size_t required )
{
	vs.reserve( vs.size() + required );
}

/// Inserts second container to the end of first
template <typename T>
void append( T& target, const T& from )
{
	target.insert( target.end(), from.begin(), from.end() );
}



/// Non-owning reference to callable object
template <bool AllowOptional, typename F>
class callable_ref_base;

/// Non-owning reference to callable object
template <bool AllowOptional, typename Ret, typename... Args>
class callable_ref_base<AllowOptional, Ret(Args...)>
{
	// P0792R0 (open-std.org)
	void* _ptr;
	Ret(*_erased_fn)(void*, Args...);
	
	template<bool, typename>
	friend class callable_ref_base;
	
	callable_ref_base(void* _ptr, Ret(*_erased_fn)(void*, Args...))
		: _ptr(_ptr), _erased_fn(_erased_fn) {}

public:
	template <typename T, std::enable_if_t<
		std::is_invocable_v<T, Args...> &&
		std::is_same_v<Ret, std::invoke_result_t<T, Args...>> &&
		!std::is_same_v<std::decay_t<T>, callable_ref_base<true,  Ret(Args...)>> &&
		!std::is_same_v<std::decay_t<T>, callable_ref_base<false, Ret(Args...)>>, int> = 0>
	callable_ref_base(T&& f) noexcept {
		_ptr = static_cast<void*>(std::addressof(f));
		_erased_fn = [](void* ptr, Args... xs) -> Ret {
			return (*reinterpret_cast<std::add_pointer_t<T>>(ptr))(std::forward<Args>(xs)...);
		};
	}
	
	template <typename T, std::enable_if_t<
		std::is_same_v<std::decay_t<T>, callable_ref_base<false, Ret(Args...)>>, int> = 0>
	callable_ref_base(T&& f) noexcept {
		_ptr = f._ptr;
		_erased_fn = f._erased_fn;
	}

	callable_ref_base(std::nullptr_t) noexcept {
		static_assert(AllowOptional, "non-opt callable_ref can't be initialized with nullptr");
		_ptr = nullptr;
		_erased_fn = nullptr;
	}
	
	operator bool() const {
		return AllowOptional ? _ptr != nullptr : true;
	}
	auto operator()(Args... xs) {
		if (!(*this)) throw std::logic_error("null opt_callable_ref invoked");
		return _erased_fn(_ptr, std::forward<Args>(xs)...);
	}
	
	callable_ref_base<false, Ret(Args...)> to_nonopt() const {
		if (!(*this)) throw std::logic_error("null opt_callable_ref conversion");
		return callable_ref_base<false, Ret(Args...)>(_ptr, _erased_fn);
	}
};

template <typename F>
using callable_ref = callable_ref_base<false, F>;

template <typename F>
using opt_callable_ref = callable_ref_base<true, F>;



template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

#endif // VAS_CPP_UTILS_HPP
