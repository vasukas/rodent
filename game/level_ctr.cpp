#include "level_ctr.hpp"
#include "level_gen.hpp"
#include "s_objs.hpp"



LevelControl::LevelControl(const LevelTerrain& lt)
{
	size = lt.grid_size;
	cells.resize( size.area() );
	
	for (int y=0; y<size.y; ++y)
	for (int x=0; x<size.x; ++x)
	{
		auto& lc = lt.cs[y * size.x + x];
		auto& nc = cells[y * size.x + x];
		nc.is_wall = lc.is_wall;
		nc.pos = {x, y};
	}
	
	spps.reserve( lt.spps.size() );
	for (auto& p : lt.spps)
		spps.push_back({ p.first, vec2fp(p.second) * lt.cell_size + vec2fp::one(lt.cell_size /2) });
}
LevelControl::Cell* LevelControl::cell(vec2i pos) noexcept
{
	if (Rect{{}, size, true}.contains_le(pos)) return &cells[pos.y * size.x + pos.x];
	return nullptr;
}
LevelControl::Cell& LevelControl::cref(vec2i pos)
{
	if (auto c = cell(pos)) return *c;
	throw std::runtime_error("LevelControl::cref() null");
}
void LevelControl::fin_init()
{
	for (auto& p : spps)
	{
		switch (p.type)
		{
		case SP_TEST_AI:
			new ETurret(p.pos, TEAM_BOTS);
			break;
			
		case SP_TEST_BOX:
			new EPhyBox(p.pos);
			break;
			
		case SP_PLAYER:
			break;
		}
	}
}



static LevelControl* rni;
LevelControl* LevelControl::init(const LevelTerrain& lt) {return rni = new LevelControl (lt);}
LevelControl& LevelControl::get() {return *rni;}
LevelControl::~LevelControl() {rni = nullptr;}
