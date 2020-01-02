#include "game_ai/ai_group.hpp"
#include "utils/noise.hpp"
#include "utils/path_search.hpp"
#include "vaslib/vas_log.hpp"
#include "game_core.hpp"
#include "level_ctr.hpp"
#include "level_gen.hpp"
#include "player_mgr.hpp"
#include "game_objects/s_objs.hpp"



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
}
void LevelControl::fin_init(LevelTerrain& lt)
{
	if (lt.dbg_spawns.empty()) fin_init_normal(lt);
	else {
		VLOGW("LevelControl:: debug spawn only");
		fin_init_debug(lt);
	}
	
	for (size_t i=0; i < cells.size(); ++i)
		cells[i].is_wall |= lt.cs[i].is_wall;
	
	//
	
	std::vector<uint8_t> aps_ps;
	aps_ps.resize( cells.size() );
	for (size_t i=0; i < cells.size(); ++i)
		aps_ps[i] = cells[i].is_wall ? 0 : 1;
	
	aps.reset( AsyncPathSearch::create_default() );
	aps->update(size, std::move(aps_ps));
}
void LevelControl::fin_init_normal(LevelTerrain& lt)
{
	TimeSpan time_start = TimeSpan::since_start();
	
	/// Square grid non-diagonal directions (-x, +x, -y, +y)
	const std::array<vec2i, 4> sg_dirs{{{-1,0}, {1,0}, {0,-1}, {0,1}}};
	
	auto& rnd = GameCore::get().get_random();
	
	auto lt_cref = [&](vec2i pos) -> auto& {return lt.cs[pos.y * size.x + pos.x];};
	
	auto room_ctr = [&](LevelTerrain::Room& r, bool limited = false)
	{
		vec2fp pos = cell_size * r.area.fp_center();
		auto is_ok = [&](vec2i p){ return !lt_cref(p).is_wall && !lt_cref(p).decor_used; };
		
		if (!is_ok(to_cell_coord(pos)))
		{
			size_t n = 0;
			r.area.map([&](vec2i p){
				if (!is_ok(p)) return;
				if (limited) {for (auto& d : sg_dirs) if (!lt_cref(p+d).room) return;}
				++n;
			});
			
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
	
	// add resources and decor
	
	TimeSpan time_decor = TimeSpan::since_start();
	
	std::vector<int_fast8_t> room_check_depth;
	std::vector<vec2i> room_check_list;
	std::vector<vec2i> room_check_list2;
	room_check_list.reserve(1000);
	room_check_list2.reserve(1000);
	
	const float res_fail_chance = 0.3; // chance to fail spawning resource
	
	for (auto& r : lt.rooms)
	{
		bool isolation_check_init = false;
		auto isolation_check = [&](vec2i new_p)
		{
			auto& c_depth = room_check_depth;
			auto cd = [&](vec2i p) -> auto& {
				return c_depth[p.y * (r.area.size().x + 2) + p.x];
			};
			
			if (!isolation_check_init) {
				isolation_check_init = true;
				c_depth.resize( vec2i(r.area.size() + vec2i::one(2)).area() );
				
				for (int y=0; y<r.area.size().y; ++y) {
					cd({0, y + 1}) = -1;
					cd({r.area.size().x + 1, y + 1}) = -1;
				}
				for (int x=0; x<r.area.size().x; ++x) {
					cd({x + 1, 0}) = -1;
					cd({x + 1, r.area.size().y + 1}) = -1;
				}
			}
			
			std::optional<vec2i> p_first; // always valid after following loop
			int free_num = -1;
			
			for (int y=0; y<r.area.size().y; ++y)
			for (int x=0; x<r.area.size().x; ++x)
			{
				auto& c = lt_cref(vec2i{x, y} + r.area.lower());
				bool is_wall = c.is_wall;
				
				if (is_wall) cd({x+1, y+1}) = -1;
				else {
					cd({x+1, y+1}) = 1;
					if (!p_first) p_first = {x+1, y+1};
					++free_num;
				}
			}
			cd(new_p - r.area.lower() + vec2i::one(1)) = -1;
			
			auto& open = room_check_list;
			auto& next = room_check_list2;
			int visit_num = 0;
			
			open.push_back(*p_first);
			while (!open.empty())
			{
				next.clear();
				
				for (auto& p : open)
				{
					for (auto& d : sg_dirs) {
						vec2i np = p + d;
						auto& nv = cd(np);
						if (nv == 1) {
							nv = 0;
							next.push_back(np);
							++visit_num;
						}
					}
				}
				
				open.swap(next);
			}
			
			return free_num == visit_num;
		};
		
		auto is_ok = [&](vec2i p, bool check_isol = true)
		{
			if (lt_cref(p).is_wall || lt_cref(p).decor_used) return false;
			for (auto& d : sg_dirs) {
				auto& c = lt_cref(p+d);
				if (!c.room && !c.is_wall) return false;
			}
			if (check_isol && !isolation_check(p)) return false;
			return true;
		};
		auto add_rnd_res = [&](vec2i at)
		{
			if (rnd.range_n() < res_fail_chance) return; // BALANCE HACK
			
			if (!is_ok(at, false)) return;
			auto ap = EPickable::rnd_ammo();
			ap.amount *= rnd.range(0.1, 0.7);
			new EPickable( to_center_coord(at), ap );
			lt_cref(at).decor_used = true;
		};
		auto get_rot = [&](vec2i at, std::optional<bool> horiz = {}) -> std::optional<float>
		{
			if (!is_ok(at))
				return {};
			
			bool rots[4]; // -x, +x, -y, +y
			for (int i=0; i<4; ++i) {
				auto& c = lt_cref(at + sg_dirs[i]);
				rots[i] = c.is_wall && !c.decor_used;
			}
			
			int rot_sel;
			if (horiz)
			{
				if (*horiz) {
					if		(rots[0]) rot_sel = 0;
					else if (rots[1]) rot_sel = 1;
					else return {};
				}
				else {
					if		(rots[2]) rot_sel = 2;
					else if (rots[3]) rot_sel = 3;
					else return {};
				}
			}
			else {
				rot_sel = -1;
				for (int i=0; i<4; ++i)
				{
					if (rots[i]) {
						if (rot_sel != -1) return {};
						rot_sel = i;
					}
				}
				if (rot_sel == -1) return {};
			}
			
			switch (rot_sel)
			{
			case 0: return M_PI;
			case 1: return 0;
			case 2: return -M_PI/2;
			case 3: return M_PI/2;
			}
			return 0; // never reached
		};
		auto add_disp = [&](vec2i at, std::optional<bool> horiz = {}, bool increased = false)
		{
			if (rnd.range_n() < res_fail_chance) return; // BALANCE HACK
			
			if (auto rot = get_rot(at, horiz)) {
				new EDispenser( to_center_coord(at), *rot, increased );
				lt_cref(at).decor_used = true;
			}
		};
		auto add_mdock = [&](vec2i at, std::optional<bool> horiz = {})
		{
			if (auto rot = get_rot(at, horiz)) {
				new EMinidock( to_center_coord(at), *rot );
				lt_cref(at).decor_used = true;
			}
		};
		// mk returns false if it's ghost
		auto add_decor = [&](vec2i at, auto mk, bool directional)
		{
			float r;
			if (directional) {
				if (auto rot = get_rot(at)) r = *rot;
				else return;
			}
			else {
				if (!is_ok(at)) return;
				r = M_PI/2 * rnd.range_index(4);
			}
			
			lt_cref(at).decor_used = true;
			if (mk(at, r)) lt_cref(at).is_wall = true;
		};
		
		auto mk_cont = [](vec2i at, float r) {
			new EDecor( "Box", {at, {1,1}, true}, r, rnd_stat().range_n() < 0.3 ? MODEL_STORAGE_BOX_OPEN : MODEL_STORAGE_BOX );
			return true;
		};
		auto mk_abox = [](vec2i at, float r) {
			new EDecor( "Autobox", {at, {1,1}, true}, r, MODEL_STORAGE, FColor(0.7, 0.9, 0.7) );
			return true;
		};
		auto mk_mk = [](const char *name, ModelType model, bool is_ghost) {
			return [=](vec2i at, float r) {
				if (is_ghost) new EDecorGhost( Transform(LevelControl::get().to_center_coord(at), r), model );
				else new EDecor( name, {at, {1,1}, true}, r, model );
				return !is_ghost;
			};
		};
		
		//
		
		if		(r.type == LevelTerrain::RM_LIVING)
		{
			for (int i=0; i<6; ++i)
			{
				vec2i at;
				at.x = rnd.range( r.area.lower().x, r.area.upper().x - 1 );
				at.y = rnd.range( r.area.lower().y, r.area.upper().y - 1 );
				
				float t = rnd.range_n();
				if		(t < 0.4) add_rnd_res(at);
				else if (t < 0.7) add_disp(at);
				else              add_mdock(at);
			}
		}
		else if (r.type == LevelTerrain::RM_WORKER)
		{
			auto next_pos = [&]() -> std::optional<vec2i>
			{
				vec2i at;
				at.x = rnd.range( r.area.lower().x, r.area.upper().x - 1 );
				at.y = rnd.range( r.area.lower().y, r.area.upper().y - 1 );
				
				static const vec2i ds[] = {{0,0}, {1,1}, {-1,-1}, {-1,1}, {1,-1}};
				for (auto& d : ds)      if (lt_cref(at + d).is_wall) return {};
				for (auto& d : sg_dirs) if (lt_cref(at + d).is_wall) return {};
				if (!is_ok(at)) return {};
			                
				return at;
			};
			
			int n_max = std::min(1 + r.area.size().area() / 100, 3);
			for (int i=0, n=0; i<n_max*1.5; ++i)
			{
				if (auto p = next_pos())
				{
					add_decor(*p, mk_abox, false);
					if (++n == n_max) break;
				}
			}
			
			n_max = std::min(1 + r.area.size().area() / 100, 3);
			for (int i=0, n=0; i<n_max*1.5; ++i)
			{
				if (auto p = next_pos())
				{
					add_decor(*p, mk_mk("Drilling rig", MODEL_MINEDRILL_MINI, false), false);
					if (++n == n_max) break;
				}
			}
//			model("Drill", MODEL_MINEDRILL, {2,1});
			
			n_max = 3 + r.area.size().area() / 40;
			for (int i=0; i<n_max; ++i)
			{
				vec2i at;
				at.x = rnd.range( r.area.lower().x, r.area.upper().x - 1 );
				at.y = rnd.range( r.area.lower().y, r.area.upper().y - 1 );
				add_rnd_res(at);
			}
		}
		else if (r.type == LevelTerrain::RM_REPAIR)
		{
			r.area.map_inner([&](vec2i p)
			{
				if (rnd.range_n() > 0.5) return;
				
				float t = rnd.range_n();
				if		(t < 0.6) add_decor( p, mk_cont, true );
				else if (t < 0.7) add_mdock( p );
				else              add_decor( p, mk_mk({}, MODEL_DOCKPAD, true), false );
			});
		}
		else if (r.type == LevelTerrain::RM_FACTORY)
		{
			// generate conveyor lines
			
			struct BigCell
			{
				std::vector<vec2i> line; ///< Empty for non-column, non-local
				bool is_conv = false; ///< Has conveyor (impassable)
				bool is_hor; ///< Is line extended by x
				int depth; // tmp
			};
			std::vector<BigCell> bcs;
			
			std::vector<int> cs_xs, cs_ys; // local
			for (int y = 1; y < r.area.size().y - 1; ++y)
			for (int x = 1; x < r.area.size().x - 1; ++x)
			{
				if (lt_cref(vec2i{x,y} + r.area.lower()).structure == LevelTerrain::STR_COLUMN) {
					if (cs_ys.empty() || cs_ys.back() != y) cs_ys.push_back(y);
					if (cs_ys.size() == 1) cs_xs.push_back(x);
				}
			}
			if (cs_xs.empty() || cs_ys.empty())
				continue;
			
			vec2i lsz;
			lsz.x = cs_xs.size()*2 + 1;
			lsz.y = cs_ys.size()*2 + 1;
			
			auto bc = [&](vec2i p) -> BigCell* {
				if (p.x < 0 || p.y < 0 || p.x >= lsz.x || p.y >= lsz.y) return nullptr;
				return &bcs[p.y * lsz.x + p.x];
			};
			
			bcs.resize( lsz.area() );
			for (int y=0; y < int(cs_ys.size()) + 1; ++y)
			for (int x=0; x < int(cs_xs.size()) + 1; ++x)
			{
				vec2i p0, p1;
				p0.x = x ? cs_xs[x-1] + 1 : 0;
				p0.y = y ? cs_ys[y-1] + 1 : 0;
				p1.x = x != int(cs_xs.size()) ? cs_xs[x] : r.area.size().x;
				p1.y = y != int(cs_ys.size()) ? cs_ys[y] : r.area.size().y;
				
				if (y) {
					auto& c = *bc({x*2, y*2 - 1});
					c.is_hor = true;
					for (int lx = p0.x; lx < p1.x; ++lx)
						c.line.push_back(vec2i{ lx, cs_ys[y-1] } + r.area.lower());
				}
				if (x) {
					auto& c = *bc({x*2 - 1, y*2});
					c.is_hor = false;
					for (int ly = p0.y; ly < p1.y; ++ly)
						c.line.push_back(vec2i{ cs_xs[x-1], ly } + r.area.lower());
				}
				if (x && y) {
					auto& c = *bc({x*2 - 1, y*2 - 1});
					c.is_conv = true;
				}
			}
			
			const int depth_max = 100000;
			auto check = [&]
			{
				for (auto& c : bcs)
					c.depth = c.is_conv ? -1 : depth_max;
				
				int depth = 0;
				bcs[0].depth = 0;
				
				while (true)
				{
					bool any = false;
					for (int y=0; y < lsz.y; ++y)
					for (int x=0; x < lsz.x; ++x)
					{
						if (bc({x, y})->depth != depth) continue;
						for (auto& d : sg_dirs) {
							if (auto nc = bc(vec2i{x,y} + d);
							    nc && nc->depth > depth)
							{
								nc->depth = depth;
								any = true;
							}
						}
					}
					if (!any) break;
				}
				
				for (auto& c : bcs) if (c.depth == depth_max) return false;
				return true;
			};
			
			std::vector<vec2i> tries; ///< Not yet tried, bcs coords
			tries.reserve( cs_xs.size() * cs_ys.size() * 2 );
			
			for (size_t y=1; y <= cs_ys.size(); ++y)
			for (size_t x=1; x <= cs_xs.size(); ++x)
			{
				tries.push_back(vec2i( x*2 - 1, y*2 ));
				tries.push_back(vec2i( x*2, y*2 - 1 ));
			}
			rnd.shuffle(tries);
			
			for (auto& bcp : tries)
			{
				if (rnd.range_n() < 0.25) continue;
				
				auto& bigc = *bc(bcp);
				bigc.is_conv = true;
				if (!check()) bigc.is_conv = false;
				else {
					float rot = bigc.is_hor ? M_PI/2 : 0;
					if (rnd.flag()) rot += M_PI;
					
					for (auto& p : bigc.line)
					{
						float t = rnd.range_n();
						if		(t < 0.4) new EDecor( "Conveyor",  {p, {1,1}, true}, rot, MODEL_CONVEYOR );
						else if (t < 0.7) new EDecor( "Assembler", {p, {1,1}, true}, rot, MODEL_ASSEMBLER );
						else              mk_abox(p, rot);
						lt_cref(p).is_wall = true;
					}
				}
			}
			
			// add dispensers if place left
			
			r.area.map([&](vec2i p) {
				if (!lt_cref(p).structure) return;
				for (auto& d : sg_dirs) {if (rnd.range_n() < 0.5) add_disp(p+d);}
			});
		}
		else if (r.type == LevelTerrain::RM_LAB)
		{
			r.area.map_inner([&](vec2i p) {
				lt_cref(p).structure = {};
				if (rnd.range_n() < 0.2)
					add_decor( p, mk_cont, true );
			});
			r.area.map([&](vec2i p) {
				if (!lt_cref(p).structure) return;
				for (auto& d : sg_dirs)
				{
					if (rnd.range_n() > 0.8) continue;
					float t = rnd.range_n();
					if		(t < 0.1) add_decor( p+d, mk_mk("Cryopod", MODEL_HUMANPOD, false), true );
					else if (t < 0.6) add_decor( p+d, mk_mk("Lab device", MODEL_SCIENCE_BOX, false), true );
					else if (t < 0.9) add_disp ( p+d );
					else              add_mdock( p+d );
				}
			});
		}
		else if (r.type == LevelTerrain::RM_STORAGE)
		{
			auto spawn = [&](vec2i p)
			{
				if (rnd.range_n() > 0.45) return;
				float t = rnd.range_n();
				if		(t < 0.6) add_decor( p, mk_cont, true );
				else if (t < 0.7) add_decor( p, mk_abox, false );
				else if (t < 0.9) add_disp ( p, {}, true );
				else              add_rnd_res( p );
			};
			r.area.map_inner([&](vec2i p) {
				lt_cref(p).structure = {};
				spawn(p);
			});
			r.area.map([&](vec2i p) {
				if (!lt_cref(p).structure) return;
				for (auto& d : sg_dirs)
					spawn(p + d);
			});
		}
		else if (r.type == LevelTerrain::RM_TERMINAL)
		{
			r.area.map([&](vec2i p) {
				auto& c = lt_cref(p);
				if (c.structure && rnd.range_n() < 0.6 && get_rot(p)) {
					add_decor( p, mk_cont, true );
				}
				else if (c.structure == LevelTerrain::STR_RIB_IN)  add_disp (p, true);
				else if (c.structure == LevelTerrain::STR_RIB_OUT) add_mdock(p, true);
			});
		}
		else if (r.type == LevelTerrain::RM_TRANSIT)
		{
			Transform at;
			at.pos = room_ctr(r, true);
			at.rot = M_PI/2 * rnd.range_index(4);
			new EDecorGhost(at, MODEL_TELEPAD);
			lt_cref( to_cell_coord(at.pos) ).decor_used = true;
			
			r.area.map_inner([&](vec2i p)
			{
				float t = rnd.range_n();
				if (t < 0.7) return;
				if (t < 0.8) add_disp(p);
				else         add_mdock(p);
			});
		}
		else if (r.type == LevelTerrain::RM_ABANDON)
		{
			for (int i=0; i<4; ++i)
			{
				vec2i at;
				at.x = rnd.range( r.area.lower().x, r.area.upper().x - 1 );
				at.y = rnd.range( r.area.lower().y, r.area.upper().y - 1 );
				add_rnd_res(at);
			}
		}
	}
	
	// add spawns
	
	TimeSpan time_spawns = TimeSpan::since_start();
	
	auto add_ctr_spwn = [&](auto type, auto& room) {
		spps.push_back({ type, room_ctr(room) });
	};
	
	add_ctr_spwn(SP_PLAYER, lt.rooms[0]);
	
	// spawn keys & terminals
	
	auto spawn_key = [&](LevelTerrain::Room& r)
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
		f.ui_name = "Security token";
		
		auto next_pos = [&](bool check_dec) -> std::optional<vec2i>
		{
			vec2i at;
			at.x = rnd.range( r.area.lower().x, r.area.upper().x - 1 );
			at.y = rnd.range( r.area.lower().y, r.area.upper().y - 1 );
			
			auto& c = lt_cref(at);
			if (!c.is_wall && (!check_dec || !c.decor_used)) return at;
			return {};
		};
		
		std::optional<vec2i> at;
		for (int i=0; i<40 && !at; ++i) at = next_pos(true);
		while (!at) at = next_pos(false);	
		new EPickable( to_center_coord(*at), std::move(f) );
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
	
	TimeSpan time_enemies = TimeSpan::since_start();
	
	auto pars_workr = std::make_shared<AI_DroneParams>();
	pars_workr->speed = {2, 3, 4};
	pars_workr->dist_minimal = 3;
	pars_workr->dist_optimal = 10;
	pars_workr->dist_visible = 14;
	pars_workr->dist_suspect = 16;
	
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
			dl_count = 1;
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
			dl_count = 0.8;
			dl_heavy = 0.1;
			break;
			
		default:
			dl_count = 0;
			break;
		}
		
		int room_area = r.area.size().area();
		int room_cdel = 12;
		
		if		(room_area < 100) room_cdel = 12;
		else if (room_area < 300) room_cdel = 16;
		else                      room_cdel = 20;
		
		size_t num = dl_count * (room_area / room_cdel);
		if (!num) continue;
		
		std::vector<vec2i> ps;
		ps.reserve( r.area.size().area() );
		r.area.map([&](vec2i p)
		{
			auto& c = lt_cref(p);
			if (!c.is_wall /*&& !c.decor_used*/)
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
			
			at.x += GameCore::get().get_random().range_n2() * 0.5;
			at.y += GameCore::get().get_random().range_n2() * 0.5;
			
			float rnd_k = GameCore::get().get_random().range_n();
			if		(rnd_k < dl_work)
			{
				new EEnemyDrone(at, {grp, pars_workr, MODEL_WORKER, 0.4});
			}
			else if (rnd_k < dl_drone)
			{
				new EEnemyDrone(at, {grp, pars_drone, MODEL_DRONE, 0.7});
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
	
	TimeSpan time_doors = TimeSpan::since_start();
	
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
			
			auto rm_prio = [&](LevelTerrain::RoomType type)
			{
				switch (type)
				{
				case LevelTerrain::RM_DEFAULT:  return -2;
				case LevelTerrain::RM_CONNECT:  return -1;
				case LevelTerrain::RM_TERMINAL: return 10;
				case LevelTerrain::RM_KEY:      return 5;
				case LevelTerrain::RM_TRANSIT:  return 20;
				default: break;
				}
				return 0;
			};
			auto rm_which = [&](vec2i pa, vec2i pb)
			{
				auto& ca = lt_cref(pa);
				auto& cb = lt_cref(pb);
				if (ca.room && cb.room)
				{
					int pd = rm_prio(ca.room->type) - rm_prio(cb.room->type);
					if (pd) return pd > 0 ? 1 : -1;
					return rnd.flag() ? 1 : -1;
				}
				return ca.room ? 1 : -1;
			};
			
			vec2i room = {};
			if (ext.x) room.y = rm_which({x, y+1}, {x, y-1});
			else       room.x = rm_which({x+1, y}, {x-1, y});
			
			bool plr_only = false;
			for (auto& d : sg_dirs)
			{
				int type;
				if (auto r = lt_cref(vec2i(x,y) + d).room) type = r->type;
				else continue;
				if (type == LevelTerrain::RM_KEY || type == LevelTerrain::RM_TRANSIT) {
					plr_only = true;
					break;
				}
			}
			
			lt_cref({x, y}).is_door = false;
			new EDoor({x, y}, ext, room, plr_only);
			
			if (plr_only) {
				if (ext.x) {for (int i=0; i<ext.x; ++i) lt_cref({x + i, y}).is_wall = true;}
				else       {for (int i=0; i<ext.y; ++i) lt_cref({x, y + i}).is_wall = true;}
			}
		}
	}
	
	//
	
	TimeSpan time_end = TimeSpan::since_start();

	VLOGD("LevelControl::() total:   {:.3f} seconds", (time_end     - time_start  ).seconds());
	VLOGD("                 decor:   {:.3f} seconds", (time_spawns  - time_decor  ).seconds());
	VLOGD("                 spawns:  {:.3f} seconds", (time_enemies - time_spawns ).seconds());
	VLOGD("                 enemies: {:.3f} seconds", (time_doors   - time_enemies).seconds());
	VLOGD("                 doors:   {:.3f} seconds", (time_end     - time_doors  ).seconds());
}
void LevelControl::fin_init_debug(LevelTerrain& lt)
{
	EEnemyDrone::Init drone;
	drone.grp = std::make_shared<AI_Group>( Rect({1,1}, lt.grid_size - vec2i::one(1), false) );
	drone.pars = std::make_shared<AI_DroneParams>();
	//
	drone.pars->speed = {4, 7, 9};
	drone.pars->dist_minimal = 8;
	drone.pars->dist_optimal = 14;
	drone.pars->dist_visible = 20;
	drone.pars->dist_suspect = 25;
	
	for (auto& sp : lt.dbg_spawns)
	{
		vec2fp pos = to_center_coord(sp.first);
		switch (sp.second)
		{
		case LevelTerrain::DBG_SPAWN_PLAYER:
			spps.push_back({ SP_PLAYER, pos });
			break;
			
		case LevelTerrain::DBG_SPAWN_DRONE:
			new EEnemyDrone( pos, drone );
			break;
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
