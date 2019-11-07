#include "game_ai/ai_group.hpp"
#include "utils/noise.hpp"
#include "utils/path_search.hpp"
#include "vaslib/vas_log.hpp"
#include "game_core.hpp"
#include "level_ctr.hpp"
#include "level_gen.hpp"
#include "player_mgr.hpp"
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
		
		if (lc.room)
			nc.room_i = lc.room - lt.rooms.data();
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
		if (lr.type == LevelTerrain::RM_TERMINAL) nr.is_final_term = true;
		
		if (lr.type == LevelTerrain::RM_ABANDON) nr.name = "ERROR";
		else if (typenm) nr.name = FMT_FORMAT("{}-{}", typenm, ++rm_cou[lr.type]);
		else nr.name = FMT_FORMAT("Unknown [{}{}]", int('A' + lr.type), ++rm_cou[lr.type]);
	}
	
	//
	
	std::vector<uint8_t> aps_ps;
	aps_ps.resize( cells.size() );
	for (size_t i=0; i < cells.size(); ++i) aps_ps[i] = cells[i].is_wall ? 0 : 1;
	
	aps.reset( AsyncPathSearch::create_default() );
	aps->update(size, std::move(aps_ps));
}
void LevelControl::fin_init(LevelTerrain& lt)
{
	auto lt_cref = [&](vec2i pos) -> auto& {return lt.cs[pos.y * size.x + pos.x];};
	
	// add spawns
	
	auto room_ctr = [&](LevelTerrain::Room& r)
	{
		vec2fp pos = cell_size * (vec2fp(r.area.lower()) + vec2fp(r.area.size()) /2);
		if (lt_cref(to_cell_coord(pos)).is_wall)
		{
			size_t n = 0;
			r.area.map([&](vec2i p){ if (!lt_cref(p).is_wall) ++n; });
			
			n = GameCore::get().get_random().range_index(n);
			r.area.map_check([&](vec2i p)
			{
				if (!lt_cref(p).is_wall) {
					if (!n) {
						pos = to_center_coord(p);
						return false;
					}
					--n;
				}
				return true;
			});
		}
		return pos;
	};
	auto add_ctr_spwn = [&](auto type, auto& room)
	{
		spps.push_back({ type, room_ctr(room) });
	};
	
	add_ctr_spwn(SP_PLAYER, lt.rooms[0]);
	
	// spawn keys & terminals
	
	auto spawn_key = [&](auto& room)
	{
		EPickable::Func f;
		f.f = [](Entity& ent)
		{
			if (!GameCore::get().get_pmg().is_player(&ent)) return false;
			GameCore::get().get_pmg().inc_objective();
			return true;
		};
		f.model = MODEL_TERMINAL_KEY;
		f.clr   = FColor(1, 0.8, 0.4);
		new EPickable( room_ctr(room), f );
	};
	
	std::vector<LevelTerrain::Room*> key_rooms;
	size_t key_count = 0;
	
	for (auto& r : lt.rooms)
	{
		if		(r.type == LevelTerrain::RM_KEY)     {++key_count; spawn_key(r);}
		else if (r.type == LevelTerrain::RM_TERMINAL) add_ctr_spwn(SP_FINAL_TERMINAL, r);
		else if (r.type == LevelTerrain::RM_LIVING)   key_rooms.push_back(&r);
	}
	
	for (; key_count < GameConst::total_key_count && !key_rooms.empty(); ++key_count)
	{
		size_t i = GameCore::get().get_random().range_index( key_rooms.size() );
		spawn_key(*key_rooms[i]);
		key_rooms.erase( key_rooms.begin() + i );
	}
	
	// prepare enemy spawn
	
	auto pars_workr = std::make_shared<AI_DroneParams>();
	pars_workr->speed = {2, 3, 4};
	pars_workr->dist_minimal = 3;
	pars_workr->dist_optimal = 8;
	pars_workr->dist_visible = 12;
	pars_workr->dist_suspect = 13;
	
	auto pars_drone = std::make_shared<AI_DroneParams>();
	pars_drone->speed = {4, 7, 9};
	pars_drone->dist_minimal = 8;
	pars_drone->dist_optimal = 14;
	pars_drone->dist_visible = 20;
	pars_drone->dist_suspect = 25;
	
	// random enemy spawn
	
	size_t total_enemy_count = 0;
	
	for (auto& r : lt.rooms)
	{
		float dl_count = 1;
		float dl_work  = 0;
		float dl_drone = 1;
		float dl_heavy = 0;
		bool ext_area = true;
		
		switch (r.type)
		{
		case LevelTerrain::RM_WORKER:
			dl_count = 0.5;
			dl_work = 2;
			break;
			
		case LevelTerrain::RM_FACTORY:
			dl_count = 2;
			dl_heavy = 0.4;
			ext_area = false;
			break;
			
		case LevelTerrain::RM_LAB:
			dl_count = 0.7;
			dl_heavy = 0.7;
			ext_area = false;
			break;
			
		case LevelTerrain::RM_STORAGE:
			dl_count = 0.6;
			dl_heavy = 0.4;
			break;
			
		case LevelTerrain::RM_KEY:
			dl_count = 0.2;
			dl_heavy = 0.1;
			break;
			
		default:
			dl_count = 0;
			break;
		}
		
		size_t num = dl_count * (r.area.size().area() / 12);
		if (!num) continue;
		
		std::vector<vec2i> ps;
		ps.reserve( r.area.size().area() );
		r.area.map([&](vec2i p)
		{
			if (!lt_cref(p).is_wall)
				ps.push_back(p);
		});
		num = std::min(num, ps.size());
		
		float dl_total = dl_work + dl_drone + dl_heavy;
		dl_work  = dl_work  / dl_total;
		dl_drone = dl_drone / dl_total + dl_work;
		dl_heavy = dl_heavy / dl_total + dl_drone;
		
		Rect grp_area = r.area;
		if (ext_area)
		{
			auto hext = int_round(20 / cell_size);
			grp_area.off = max(grp_area.off - vec2i::one(hext), {1, 1});
			grp_area.sz  = min(grp_area.sz  + vec2i::one(hext*2), size - grp_area.off - vec2i::one(1));
		}
		auto grp = std::make_shared<AI_Group>(grp_area);
		
		for (size_t i=0; i<num; ++i)
		{
			vec2fp at = GameCore::get().get_random().random_el(ps);
			at = at * cell_size + vec2fp::one(cell_size /2);
			
			at.x += GameCore::get().get_random().range_n2() * cell_size /2;
			at.y += GameCore::get().get_random().range_n2() * cell_size /2;
			
			float rnd_k = GameCore::get().get_random().range_n();
			if		(rnd_k < dl_work)
			{
				new EEnemyDrone(at, {grp, pars_workr});
			}
			else if (rnd_k < dl_drone)
			{
				new EEnemyDrone(at, {grp, pars_drone});
			}
			else //if (rnd_k < dl_heavy)
			{
				new ETurret(at, grp, TEAM_BOTS);
			}
		}
		
		total_enemy_count += num;
	}
	
	VLOGD("Total enemy count: {}", total_enemy_count);
	
	// spawn doors
	
	for (int y=1; y < size.y - 1; ++y)
	for (int x=1; x < size.x - 1; ++x)
	{
		if (lt_cref({x, y}).is_door)
		{
			vec2i ext;
			if		(lt_cref({x + 1, y}).is_door && lt_cref({x - 1, y}).is_wall)
			{
				int i = 1;
				for (;; ++i) { // headcrab increment
					auto& c = lt_cref({x + i, y});
					if (!c.is_door) break;
					c.is_door = false;
				}
				ext = {i, 0};
			}
			else if (lt_cref({x, y + 1}).is_door && lt_cref({x, y - 1}).is_wall)
			{
				int i = 1;
				for (;; ++i) {
					auto& c = lt_cref({x, y + i});
					if (!c.is_door) break;
					c.is_door = false;
				}
				ext = {0, i};
			}
			else if (lt_cref({x + 1, y}).is_wall && lt_cref({x - 1, y}).is_wall) ext = {1, 0};
			else if (lt_cref({x, y + 1}).is_wall && lt_cref({x, y - 1}).is_wall) ext = {0, 1};
			else continue;
			
			vec2i room = {};
			if (ext.x) room.y = lt_cref({x, y+1}).room ? 1 : -1;
			else       room.x = lt_cref({x+1, y}).room ? 1 : -1;
			
			lt_cref({x, y}).is_door = false;
			new EDoor({x, y}, ext, room);
		}
	}
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
LevelControl::Room* LevelControl::get_room(vec2fp pos)
{
	auto ri = cref(to_cell_coord(pos)).room_i;
	if (ri) return &rooms[*ri];
	return nullptr;
}



static LevelControl* rni;
LevelControl* LevelControl::init(const LevelTerrain& lt) {return rni = new LevelControl (lt);}
LevelControl& LevelControl::get() {return *rni;}
LevelControl::~LevelControl() {rni = nullptr;}
