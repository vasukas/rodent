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

#endif // VAS_CONTAINERS_HPP
