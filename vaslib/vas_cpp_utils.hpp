#ifndef VAS_CPP_UTILS_HPP
#define VAS_CPP_UTILS_HPP

#include <algorithm>
#include <functional>
#include <vector>



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
	
	RAII_Guard( RAII_Guard&& g ) { foo = std::move(g.foo); g.foo = {}; }
	void operator =( RAII_Guard&& g ) { foo = std::move(g.foo); g.foo = {}; }
	
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
template <typename F>
class callable_ref;

/// Non-owning reference to callable object
template <typename Ret, typename... Args>
class callable_ref<Ret(Args...)>
{
	// P0792R0 (open-std.org)
    void* _ptr;
	Ret(*_erased_fn)(void*, Args...);

public:
	template <typename T, std::enable_if_t<
		std::is_invocable_r<Ret, T, Args...>::value &&
		!std::is_same<std::decay_t<T>, callable_ref<Ret(Args...)>>::value, int > = 0>
	callable_ref(T&& f) noexcept {
		_ptr = static_cast<void*>(std::addressof(f));
		_erased_fn = [](void* ptr, Args... xs) -> Ret {
			return (*reinterpret_cast<std::add_pointer_t<T>>(ptr))(std::forward<Args>(xs)...);
		};
	}
	
	auto operator()(Args... xs) {
		return _erased_fn(_ptr, std::forward<Args>(xs)...);
	}
};

#endif // VAS_CPP_UTILS_HPP
