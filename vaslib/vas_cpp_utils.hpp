#ifndef VAS_CPP_UTILS_HPP
#define VAS_CPP_UTILS_HPP

#include <functional>
#include <vector>



/// Just class with virtual destructor
class Deletable
{
public:
	virtual ~Deletable() = default;
};



/// Calls function before exiting the scope
struct RAII_Guard
{
	RAII_Guard() {}
	RAII_Guard( std::function <void()> foo ): foo( std::move(foo) ) {}
	~RAII_Guard()  { if (foo) foo(); }
	void trigger() { if (foo) foo(); cancel(); } ///< Calls function and resets it
	void cancel()  { foo = nullptr; } ///< Resets function without executing
	
	RAII_Guard( const RAII_Guard& ) = delete;
	void operator =( const RAII_Guard& ) = delete;
	
	RAII_Guard( RAII_Guard&& g ) { std::swap( foo, g.foo ); }
	void operator =( RAII_Guard&& g ) { std::swap( foo, g.foo ); }
	
	operator bool() {return foo.operator bool();}
	
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
void append( T& vs, const T& as )
{
	vs.insert( vs.end(), as.begin(), as.end() );
}

#endif // VAS_CPP_UTILS_HPP
