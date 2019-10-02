#include "vas_containers.hpp"

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
	size_t old_size = obj_size;
	
	par = new_par;
	id_off = std::max(sizeof(Id), par.obj_align);
	obj_size = par.obj_size + id_off;
	
	if (old_size != obj_size || old_off != id_off)
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
	
	mem = new uint8_t [size * pa->obj_size];
	fs = new size_t [size];
	f_num = size;

	for (size_t i=0; i<size; ++i)
	{
		size_t offset = i * pa->obj_size + pa->id_off;
		auto& id = *reinterpret_cast<Id*>(mem + i * pa->obj_size);
		id.pool = self_index;
		id.index = offset;
		fs[size - i - 1] = offset;
	}
}
PoolAllocator::Pool::~Pool()
{
	delete[] mem;
	delete[] fs;
}
