#include "utils/path_search.hpp"
#include "vaslib/vas_log.hpp"
#include "game_core.hpp"
#include "level_ctr.hpp"
#include "level_gen.hpp"



PathRequest::PathRequest(GameCore& core, vec2fp from, vec2fp to,
                         std::optional<float> max_length,
                         std::optional<Evade> evade)
{
	auto& lc = core.get_lc();
	vec2i pa = lc.to_nonwall_coord(from);
	vec2i pb = lc.to_nonwall_coord(to);
	
	if (pa == pb || lc.cref(pb).is_wall)
	{
		res = {{from, from}, false};
		return;
	}
	if (!max_length)
		max_length = default_max_length;
	
	PathSearch::Args args;
	args.src = pa;
	args.dst = pb;
	args.max_length = std::ceil(*max_length / GameConst::cell_size);
	if (evade) {
		args.evade = lc.to_cell_coord(evade->pos);
		args.evade_radius = std::ceil(evade->radius / GameConst::cell_size);
		args.evade_cost = evade->added_cost;
	}
	
	auto r = lc.get_aps().find_path(args);
	res = Result{};
	
	if (r.ps.empty())
	{
		res->not_found = true;
		res->ps.push_back(from);
	}
	else
	{
		res->not_found = false;
		res->ps.reserve( r.ps.size() + 1 );
		
		const auto k2 = vec2fp::one(GameConst::cell_size / 2);
		
		res->ps.push_back(from);
		for (auto& p : r.ps)
			res->ps.push_back(vec2fp(p) * GameConst::cell_size + k2);
		
		res->ps.back() = to;
	}
}
std::optional<PathRequest::Result> PathRequest::result()
{
	auto r = std::move(res);
	res.reset();
	return r;
}



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
		
		if (lc.room) {
			nc.room_i = lc.room - lt.rooms.data();
			nc.room_nearest = *nc.room_i;
		}
		else nc.room_nearest = size_t_inval;
	}
	
	//
	
	std::array<size_t, LevelTerrain::RM_TYPE_TOTAL_COUNT> rm_cou = {};
	
	rooms.reserve( lt.rooms.size() );
	for (auto& lr : lt.rooms)
	{
		const char* typenm;
		switch (lr.type)
		{
		case LevelTerrain::RM_CONNECT:  typenm = "Joint"; break;
		case LevelTerrain::RM_LIVING:   typenm = "Quarters"; break;
//		case LevelTerrain::RM_WORKER:   typenm = ""; break;
		case LevelTerrain::RM_REPAIR:   typenm = "Bot dock"; break;
		case LevelTerrain::RM_FACTORY:  typenm = "Factory"; break;
		case LevelTerrain::RM_LAB:      typenm = "Lab"; break;
		case LevelTerrain::RM_STORAGE:  typenm = "Storage"; break;
		case LevelTerrain::RM_KEY:      typenm = "Security terminal"; break;
		case LevelTerrain::RM_TERMINAL: typenm = "Level control"; break;
		case LevelTerrain::RM_TRANSIT:  typenm = "Terminal"; break;
		default:                        typenm = nullptr; break;
		}
		
		auto& nr = rooms.emplace_back();
		if		(lr.type == LevelTerrain::RM_TERMINAL) nr.type = LevelCtrRoom::T_FINAL_TERM;
		else if (lr.type == LevelTerrain::RM_TRANSIT)  nr.type = LevelCtrRoom::T_TRANSIT;
		
		if (lr.type == LevelTerrain::RM_ABANDON) nr.name = "ERROR";
		else if (typenm) nr.name = FMT_FORMAT("{}-{}", typenm, ++rm_cou[lr.type]);
		else nr.name = FMT_FORMAT("Unknown [{}{}]", int('A' + lr.type), ++rm_cou[lr.type]);
		
// see AI_Const::msg_engage_dist
		if (lr.type == LevelTerrain::RM_FACTORY || lr.type == LevelTerrain::RM_LAB || lr.type == LevelTerrain::RM_STORAGE)
			nr.ai_radio_cost = 4;
		else if (lr.area.size().area() > 20*18)
			nr.ai_radio_cost = 4;
		else if (lr.area.size().area() > 16*14)
			nr.ai_radio_cost = 3;
		else if (lr.area.size().area() > 12*10)
			nr.ai_radio_cost = 2;
		
		nr.area = lr.area;
	}
	
	//
	
	for (auto& c : lt.corrs)
	{
		for (size_t r0 : c.rs)
		for (size_t nr : c.rs)
		{
			if (nr == r0) continue;
			auto& ns = rooms[r0].neis;
			if (ns.end() == std::find( ns.begin(), ns.end(), nr ))
				ns.push_back(nr);
		}
		
		for (auto& p : c.cs)
			mut_cell(p).room_nearest = c.rs[0];
	}
	
	for (auto& c0 : cells)
	{
		if (c0.room_nearest == size_t_inval)
		{
			struct Dir {
				vec2i p, dir;
				bool ok = true;
			};
			Dir dirs[4];
			int num = 4;
			
			dirs[0].dir = { 1, 0};
			dirs[1].dir = { 0, 1};
			dirs[2].dir = {-1, 0};
			dirs[3].dir = { 0,-1};
			for (auto& d : dirs)
				d.p = c0.pos + d.dir;
			
			while (num)
			{
				for (auto& d : dirs)
				{
					if (!d.ok) continue;
					auto dc = cell(d.p);
					if (!dc) {
						d.ok = false;
						--num;
					}
					else {
						if (dc->room_i) {
							c0.room_nearest = *dc->room_i;
							num = 0;
							break;
						}
						d.p += d.dir;
					}
				}
			}
		}
	}
	
	for (auto& c0 : cells) {
		if (c0.room_nearest == size_t_inval)
			c0.room_nearest = 0;
	}
}
LevelControl::~LevelControl() = default;
void LevelControl::fin_init(const LevelTerrain& lt)
{
	for (size_t i=0; i < cells.size(); ++i)
		cells[i].is_wall |= lt.cs[i].is_wall;
	
	aps.reset( PathSearch::create() );
	update_aps();
}
LevelControl::Cell& LevelControl::mut_cell(vec2i pos)
{
	if (!is_valid(pos)) throw std::runtime_error("LevelControl::mut_cell() null");
	aps_req_update = true;
	return cells[pos.y * size.x + pos.x];
}
const LevelControl::Cell* LevelControl::cell(vec2i pos) const noexcept
{
	if (!is_valid(pos)) return nullptr;
	return &cells[pos.y * size.x + pos.x];
}
const LevelControl::Cell& LevelControl::cref(vec2i pos) const
{
	if (auto c = cell(pos)) return *c;
	throw std::runtime_error("LevelControl::cref() null");
}
const LevelCtrRoom* LevelControl::get_room(vec2fp pos) const noexcept
{
	auto ri = cref(to_cell_coord(pos)).room_i;
	if (ri) return &rooms[*ri];
	return nullptr;
}
const LevelCtrRoom& LevelControl::ref_room(size_t index) const
{
	return rooms.at(index);
}
vec2fp LevelControl::get_closest(SpawnType type, vec2fp from) const
{
	vec2fp pt = from;
	float dist = std::numeric_limits<float>::max();
	
	for (auto& s : spps)
	{
		if (s.type == type)
		{
			float d = s.pos.dist_squ(from);
			if (d < dist)
			{
				pt = s.pos;
				dist = d;
			}
		}
	}
	
	return pt;
}
vec2i LevelControl::to_nonwall_coord(vec2fp p) const
{
	p /= GameConst::cell_size;
	vec2i c = p.int_floor();
	
	if (cref(c).is_wall)
	{
		float fx = std::fmod(p.x, 1) - 0.5;
		float fy = std::fmod(p.y, 1) - 0.5;
	
#define CHECK(X, Y) {if (auto cl = cell(c + vec2i(X,Y)); cl && !cl->is_wall) return c + vec2i(X,Y);}
		if (std::abs(fx) > std::abs(fy))
		{
			if (fx < 0) CHECK(-1, 0)
			else        CHECK( 1, 0)
			if (fy < 0) CHECK(0, -1)
			else        CHECK(0,  1)
		}
		else
		{
			if (fy < 0) CHECK(0, -1)
			else        CHECK(0,  1)
			if (fx < 0) CHECK(-1, 0)
			else        CHECK( 1, 0)
		}
#undef CHECK
	}
	
	return c;
}
void LevelControl::add_spawn(Spawn sp)
{
	spps.emplace_back(std::move(sp));
}
void LevelControl::update_aps(bool forced)
{
	if (!forced && !aps_req_update) return;
	aps_req_update = false;
	
	std::vector<uint8_t> aps_ps;
	aps_ps.resize( cells.size() );
	for (size_t i=0; i < cells.size(); ++i)
		aps_ps[i] = cells[i].is_wall ? 0 : 1;
	
	aps->update(size, std::move(aps_ps));
}
void LevelControl::set_wall(vec2i pos, bool is_wall)
{
	if (!is_valid(pos)) return;
	auto& c = mut_cell(pos);
	if (c.is_wall != is_wall)
	{
		c.is_wall = is_wall;
		update_aps();
	}
}
LevelControl* LevelControl::create(const LevelTerrain& lt) {
	return new LevelControl(lt);
}
