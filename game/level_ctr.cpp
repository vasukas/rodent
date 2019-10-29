#include "utils/path_search.hpp"
#include "level_ctr.hpp"
#include "level_gen.hpp"

#include "utils/noise.hpp"
#include "game_core.hpp"
#include "s_objs.hpp"



PathRequest::PathRequest(vec2fp from, vec2fp to)
{
	const auto k = LevelControl::get().cell_size;
	vec2i pa = (from / k).int_floor();
	vec2i pb = (to   / k).int_floor();
	
	if (pa == pb)
	{
		res = Result{{from, from}, 0, false};
		return;
	}
	
	i = LevelControl::get().get_aps().add_task(pa, pb, {});
	p0 = {from, to};
}
PathRequest::~PathRequest()
{
	if (i) LevelControl::get().get_aps().rem_task(*i);
}
PathRequest::PathRequest(PathRequest&& p)
{
	*this = std::move(p);
}
PathRequest& PathRequest::operator=(PathRequest&& p)
{
	if (i) LevelControl::get().get_aps().rem_task(*i);
	
	p0 = p.p0;
	i = p.i;
	res = std::move(p.res);
	
	if (p.i) p.i.reset();
	if (p.res) p.res.reset();
	
	return *this;
}
bool PathRequest::is_ok() const
{
	return i || res;
}
bool PathRequest::is_ready() const
{
	if (res) return true;
	if (!i) return false;
	
	auto r = LevelControl::get().get_aps().get_task(*i);
	if (!r) return false;
	
	i.reset();
	
	res = Result{};
	res->len = 0;
	
	if (r->ps.empty())
	{
		res->not_found = true;
		res->ps.push_back(p0.first);
	}
	else
	{
		res->not_found = false;
		res->ps.reserve( r->ps.size() + 1 );
		
		const auto k = LevelControl::get().cell_size;
		const auto k2 = vec2fp::one(k/2);
		
		res->ps.push_back(p0.first);
		for (auto& p : r->ps)
			res->ps.push_back(vec2fp(p) * k + k2);
		
		res->ps.back() = p0.second;
	}
	return true;
}
vec2fp PathRequest::get_endpoint() const
{
	return p0.second;
}
std::optional<PathRequest::Result> PathRequest::result()
{
	if (!is_ready()) return {};
	auto p = std::move(res);
	res.reset();
	return p;
}



LevelControl::LevelControl(const LevelTerrain& lt)
	: cell_size(lt.cell_size)
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
	
	//
	
	std::vector<uint8_t> aps_ps;
	aps_ps.resize( cells.size() );
	for (size_t i=0; i < cells.size(); ++i) aps_ps[i] = cells[i].is_wall ? 0 : 1;
	
	aps.reset( AsyncPathSearch::create_default() );
	aps->update(size, std::move(aps_ps));
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
		case SP_TEST_TURRET:
			new ETurret(p.pos, TEAM_BOTS);
			break;
			
		case SP_TEST_BOX:
			new EPhyBox(p.pos);
			break;
			
		case SP_TEST_DRONE:
			new EEnemyDrone(p.pos);
			break;
			
		case SP_SUPPLY_RND:
			{
				size_t n = GameCore::get().get_random().range_index(3);
				if (n == 0) new ESupply(p.pos, {AmmoType::Bullet, 100.f});
				if (n == 1) new ESupply(p.pos, {AmmoType::Rocket, 6.f});
				if (n == 2) new ESupply(p.pos, {AmmoType::Energy, 15.f});
			}
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
