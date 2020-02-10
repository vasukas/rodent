#include "vas_containers.hpp"

#ifdef _WIN32
#include <malloc.h>
#define ALIGN_ALLOC(ALIGN, SIZE) _aligned_malloc(SIZE, ALIGN)
#define ALIGN_FREE(PTR) _aligned_free(PTR)
#else
#include <cstdlib>
#define ALIGN_ALLOC(ALIGN, SIZE) std::aligned_alloc(ALIGN, SIZE)
#define ALIGN_FREE(PTR) std::free(PTR)
#endif



PoolAllocator::PoolAllocator(Param new_par)
    : par(0, 1)
{
	set_params(new_par);
}
void* PoolAllocator::alloc()
{
	Pool* ap = nullptr;
	for (auto& p : ps) {
		if (p.f_num && !p.old) {
			ap = &p;
			break;
		}
	}
	if (!ap)
		ap = &ps.emplace_back(this, ps.size());
	
	--ap->f_num;
	auto m = ap->mem + ap->fs[ap->f_num];
	return m;
}
void PoolAllocator::free(void* ptr)
{
	if (!ptr) return;
	auto m = reinterpret_cast<uint8_t*>(ptr);
	auto& id = *reinterpret_cast<Id*>(m - id_off);
	auto& p = ps[id.pool];
	p.fs[p.f_num] = id.index;
	++p.f_num;
	
	if (p.old && p.f_num == p.size)
		p = Pool(this, id.pool);
}
void PoolAllocator::set_params(Param new_par)
{
	size_t old_off = id_off;
	size_t old_size = bc_size;
	
	par = new_par;
	id_off = std::max(id_sizeof, par.obj_align);
	bc_size = par.obj_size + id_off;
	
	if (old_size != bc_size || old_off != id_off)
	{
		size_t i = 0;
		for (auto& p : ps) {
			if (p.f_num == p.size) p = Pool(this, i);
			else p.old = true;
			++i;
		}
		ps.emplace_back(this, ps.size());
	}
	else if (ps.empty())
		ps.emplace_back(this, ps.size());
}
PoolAllocator::Pool::Pool(PoolAllocator* pa, size_t self_index)
{
	size = pa->par.pool_size;

	mem = static_cast<uint8_t*>( ALIGN_ALLOC(pa->par.obj_align, size * pa->bc_size) );
	if (!mem) throw std::bad_alloc();

	fs = new size_t [size];
	f_num = size;

	for (size_t i=0; i<size; ++i)
	{
		size_t offset = i * pa->bc_size + pa->id_off; // to actual data
		auto& id = *reinterpret_cast<Id*>(mem + i * pa->bc_size);
		id.pool = self_index;
		id.index = offset;
		fs[size - i - 1] = offset;
	}
}
PoolAllocator::Pool::~Pool()
{
	ALIGN_FREE(mem);
	delete[] fs;
}
void PoolAllocator::Pool::operator=(Pool&& p) noexcept
{
	std::swap(mem,   p.mem);
	std::swap(fs,    p.fs);
	std::swap(f_num, p.f_num);
	std::swap(size,  p.size);
	std::swap(old,   p.old);
}
