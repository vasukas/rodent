#ifndef VAS_CONTAINERS_HPP
#define VAS_CONTAINERS_HPP

#include "vas_cpp_utils.hpp"

template <typename T>
struct SparseArray
{
	std::vector<T> vs; // data
	std::vector<size_t> fi; // stack of free indices
	size_t fi_expand = 128; // stack is expanded in blocks of such size
	
	
	size_t new_index()
	{
		if (fi.empty()) {
			vs.emplace_back();
			return vs.size() - 1;
		}
		size_t i = fi.back();
		fi.pop_back();
		return i;
	}
	void free_index(size_t i)
	{
		::reserve_more_block(fi, fi_expand);
		fi.push_back(i);
	}
	
	
	bool empty() const
	{
		return vs.size() == fi.size();
	}
	size_t size() const
	{
		return vs.size();
	}
	
	
	T* data()
	{
		return vs.data();
	}
	T& operator[](size_t i)
	{
		return vs[i];
	}
	const T* data() const
	{
		return vs.data();
	}
	const T& operator[](size_t i) const
	{
		return vs[i];
	}
	
	
	void reserve_more(size_t required)
	{
		size_t avail = fi.size() + (vs.capacity() - vs.size());
		if (required > avail)
		{
			required -= avail;
			vs.reserve( vs.capacity() + required );
		}
	}
	void reserve_more_block(size_t required)
	{
		size_t avail = fi.size() + (vs.capacity() - vs.size());
		if (!avail)
			vs.reserve( vs.capacity() + required );
	}
	
	
	size_t insert_new(const T& x)
	{
		size_t i = new_index();
		vs[i] = x;
		return i;
	}
	size_t insert_new(T&& x)
	{
		size_t i = new_index();
		vs[i] = std::move(x);
		return i;
	}
};

#endif // VAS_CONTAINERS_HPP
