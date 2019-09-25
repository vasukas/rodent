#ifndef VAS_CONTAINERS_HPP
#define VAS_CONTAINERS_HPP

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
	size_t block_size; ///< internal arrays are expanded in blocks of such size
	
	
	
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



template <typename T>
class FixedPoolAllocator
{
	struct Id {
		unsigned pool  : 16;
		unsigned index : 16;
	};
	static constexpr size_t id_off = std::max(sizeof(Id), alignof(T));
	static constexpr size_t obj_size = sizeof(T) + id_off;
	
	struct Pool
	{
		uint8_t* mem;
		size_t* fs; // byte offsets to T
		size_t f_num; // number of free offsets
		
		Pool(size_t size, size_t self_index)
		{
			mem = new uint8_t [size * obj_size];
			fs = new size_t [size];
			f_num = size;

			for (size_t i=0; i<size; ++i)
			{
				size_t offset = i * obj_size + id_off;
				auto& id = *reinterpret_cast<Id*>(mem + i * obj_size);
				id.pool = self_index;
				id.index = offset;
				fs[size - i - 1] = offset;
			}
		}
		~Pool()
		{
			delete[] mem;
			delete[] fs;
		}
	};
	std::vector<Pool> ps;
	
public:
	size_t pool_size;
	
	
	FixedPoolAllocator(size_t pool_size = 128)
		: pool_size(pool_size)
	{
		ps.emplace_back(pool_size, ps.size());
	}
	void* alloc()
	{
		Pool* ap = nullptr;
		for (auto& p : ps) {
			if (p.f_num) {
				ap = &p;
				break;
			}
		}
		if (!ap)
			ap = &ps.emplace_back(pool_size, ps.size());
		
		--ap->f_num;
		auto m = ap->mem + ap->fs[ap->f_num];
		return m;
	}
	void free(void* ptr)
	{
		if (!ptr) return;
		auto m = reinterpret_cast<uint8_t*>(ptr);
		auto& id = *reinterpret_cast<Id*>(m - id_off);
		auto& p = ps[id.pool];
		p.fs[p.f_num] = id.index;
		++p.f_num;
	}
	
	
	template<typename... Args>
	T* emplace(Args&&... args)
	{
		return new(alloc()) T( std::forward<Args>(args)... );
	}
	void free(T* ptr)
	{
		if (ptr) ptr->~T();
		free(static_cast<void*>(ptr));
	}
};

#endif // VAS_CONTAINERS_HPP
