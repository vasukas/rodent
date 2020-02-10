#ifndef VAS_CONTAINERS_HPP
#define VAS_CONTAINERS_HPP

#include <numeric>
#include "vaslib/vas_cpp_utils.hpp"

template <typename T>
struct SparseArray_DefaultIsNull
{
	bool operator()(const T& x)
	{
		return static_cast<bool>(x) == false;
	}
};

template <typename T, class IsNull = SparseArray_DefaultIsNull<T>>
class SparseArray
{
	std::vector<T> vals; // data
	std::vector<size_t> fixs; // stack of free indices
	IsNull is_null;
	
public:
	typedef T value_type;
	typedef T& reference;
	typedef const T& const_reference;
	
	size_t block_size; ///< internal arrays are expanded in blocks of such size
	
	// Note: free and new don't invalidate existing iterators
	
	
	
	SparseArray(size_t block_size = 128)
	    : block_size(block_size)
	{}
	size_t new_index()
	{
		if (fixs.empty()) {
			::reserve_more_block(vals, block_size);
			vals.emplace_back();
			return vals.size() - 1;
		}
		size_t i = fixs.back();
		fixs.pop_back();
		return i;
	}
	void free_index(size_t i)
	{
		::reserve_more_block(fixs, block_size);
		fixs.push_back(i);
	}
	std::vector<T>& raw_values()
	{
		return vals;
	}
	std::vector<size_t>& raw_free_indices()
	{
		return fixs;
	}
	
	
	
	bool empty() const
	{
		return vals.size() == fixs.size();
	}
	size_t size() const
	{
		return vals.size();
	}
	size_t capacity() const
	{
		return vals.size();
	}
	size_t existing_count() const
	{
		return vals.size() - fixs.size();
	}
	
	
	
	T& operator[](size_t i)
	{
		return vals[i];
	}
	const T& operator[](size_t i) const
	{
		return vals[i];
	}
	
	
	
	template<typename... Args>
	size_t emplace_new(Args&&... args)
	{
		size_t i = new_index();
		vals[i] = T( std::forward<Args>(args)... );
		return i;
	}
	void free_and_reset(size_t i)
	{
		free_index(i);
		vals[i] = {};
	}
	
	
	
	struct Iterator
	{
		Iterator(Iterator&&) = default;
		Iterator(const Iterator&) = default;
		Iterator(SparseArray* arr_, size_t i_): arr(arr_), i(i_)
		{
			while (i != arr->vals.size() && arr->is_null(arr->vals[i]))
				++i;
		}
		Iterator& operator++()
		{
			do {++i;}
			while (i != arr->vals.size() && arr->is_null(arr->vals[i]));
			return *this;
		}
		Iterator& operator--()
		{
			do {--i;}
			while (i && arr->is_null(arr->vals[i]));
			return *this;
		}
		Iterator operator++(int)
		{
			Iterator it = *this; ++ *this; return it;
		}
		Iterator operator--(int)
		{
			Iterator it = *this; -- *this; return it;
		}
		T& operator*() const
		{
			if (i >= arr->vals.size() || arr->is_null(arr->vals[i]))
				throw std::runtime_error("SparseArray::Iterator:: out of bounds");
			return arr->vals[i];
		}
		bool operator==(const Iterator& it) const
		{
			return i == it.i;
		}
		bool operator!=(const Iterator& it) const
		{
			return i != it.i;
		}
		T* operator->() const {return &*(*this);}
		size_t index() const {return i;}
		
	private:
		SparseArray* arr;
		size_t i;
	};
	Iterator begin()
	{
		return {this, 0};
	}
	Iterator end()
	{
		return {this, vals.size()};
	}
};



template <typename... Ts>
struct PoolAllocator_AutoParam;

template <typename T>
struct PoolAllocator_AutoParam<T> {
	static constexpr size_t size  = sizeof(T);
	static constexpr size_t align = alignof(T);
};

template <typename T, typename... Ts>
struct PoolAllocator_AutoParam<T, Ts...> {
	static constexpr size_t size  = std::max(sizeof(T),  PoolAllocator_AutoParam<Ts...>::size);
	static constexpr size_t align = std::lcm(alignof(T), PoolAllocator_AutoParam<Ts...>::align);
};

class PoolAllocator
{
public:
	struct Param
	{
		size_t obj_size; ///< Object byte size (without alignment padding)
		size_t obj_align; ///< Object alignment
		size_t pool_size; ///< Object count per pool
		
		Param(size_t obj_size, size_t obj_align, size_t pool_size = 128)
			: obj_size(obj_size), obj_align(obj_align), pool_size(pool_size)
		{}
	};
	
	template <typename... Ts>
	static Param genparams(size_t pool_size) {
		return {
			PoolAllocator_AutoParam<Ts...>::size,
			PoolAllocator_AutoParam<Ts...>::align,
			pool_size
		};
	}
	
	PoolAllocator(Param new_par = {0, 1});
	
	PoolAllocator(const PoolAllocator&) = delete;
	PoolAllocator(PoolAllocator&&) = default;
	
	void* alloc();
	void free(void* ptr);
	
	/// Sets new parameters. 
	/// Old pools not required to be cleared
	void set_params(Param new_par);
	
	/// Returns current parameters
	Param get_params() const {return par;}
	
private:
	static constexpr size_t id_sizeof = 4;
	struct Id {
		unsigned pool  : 16;
		unsigned index : 16;
	};
	size_t id_off;
	size_t bc_size; // total object size in pool
	
	struct Pool
	{
		uint8_t* mem = nullptr;
		size_t* fs = nullptr; // byte offsets to T, free
		size_t f_num; // number of free offsets
		size_t size; // obj count
		bool old = false; // must be deleted
		
		Pool(PoolAllocator* pa, size_t self_index);
		Pool(const Pool&) = delete;
		Pool(Pool&& p) noexcept {*this = std::move(p);}
		~Pool();
		void operator=(Pool&& p) noexcept;
	};
	
	std::vector<Pool> ps;
	Param par;
};



template <typename T>
class TypedPoolAllocator
{
	PoolAllocator pa;
public:
	
	TypedPoolAllocator(size_t pool_size = 128)
		: pa( PoolAllocator::genparams<T>(pool_size) )
	{}
	
	void* alloc() {return pa.alloc();}
	void free(T* ptr)
	{
		if (ptr) {
			ptr->~T();
			pa.free(ptr);
		}
	}
	
	template<typename... Args>
	T* emplace(Args&&... args) {
		return new(pa.alloc()) T( std::forward<Args>(args)... );
	}
	
	void set_pool_size(size_t pool_size) {
		pa.set_params( PoolAllocator::genparams<T>(pool_size) );
	}
	size_t get_pool_size() const {
		return pa.get_params().pool_size;
	}
};

#endif // VAS_CONTAINERS_HPP
