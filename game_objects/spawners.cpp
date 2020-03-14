#include "game/game_core.hpp"
#include "game/level_ctr.hpp"
#include "game/level_gen.hpp"
#include "game/player_mgr.hpp"
#include "game_ai/ai_sim.hpp"
#include "utils/noise.hpp"
#include "vaslib/vas_log.hpp"
#include "objs_basic.hpp"
#include "objs_creature.hpp"
#include "spawners.hpp"
#include "weapon_all.hpp"



void level_spawn(GameCore& core, LevelTerrain& lt)
{
	TimeSpan time_start = TimeSpan::since_start();
	new EWall(core, lt.ls_wall);
	
	/// Square grid non-diagonal directions (-x, +x, -y, +y)
	const std::array<vec2i, 4> sg_dirs{{{-1,0}, {1,0}, {0,-1}, {0,1}}};
	auto& rnd = core.get_random();
	
	auto& lc = core.get_lc();
	const vec2i size = lc.get_size();
	
	auto lt_cref = [&](vec2i pos) -> auto& {return lt.cs[pos.y * size.x + pos.x];};
	
	auto room_ctr = [&](LevelTerrain::Room& r, bool limited = false)
	{
		vec2fp pos = GameConst::cell_size * r.area.fp_center();
		auto is_ok = [&](vec2i p){ return !lt_cref(p).is_wall && !lt_cref(p).decor_used; };
		
		if (!is_ok(lc.to_cell_coord(pos)))
		{
			size_t n = 0;
			r.area.map([&](vec2i p){
				if (!is_ok(p)) return;
				if (limited) {for (auto& d : sg_dirs) if (!lt_cref(p+d).room) return;}
				++n;
			});
			
			n = core.get_random().range_index(n);
			r.area.map_check([&](vec2i p)
			{
				if (!lt_cref(p).is_wall) {
					if (!n) {
						pos = lc.to_center_coord(p);
						return false;
					}
					--n;
				}
				return true;
			});
		}
		return pos;
	};
	
	// common checks
	
	std::vector<int_fast8_t> room_check_depth;
	std::vector<vec2i> room_check_list;
	std::vector<vec2i> room_check_list2;
	room_check_list.reserve(1000);
	room_check_list2.reserve(1000);
	
	auto isolation_check = [&](LevelTerrain::Room& r, vec2i new_p, bool& check_init)
	{
		auto& c_depth = room_check_depth;
		auto cd = [&](vec2i p) -> auto& {
			return c_depth[p.y * (r.area.size().x + 2) + p.x];
		};
		
		if (!check_init) {
			check_init = true;
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
	auto can_spawn = [&](LevelTerrain::Room& r, vec2i p, bool* isol_check_init = nullptr)
	{
		if (lt_cref(p).is_wall || lt_cref(p).decor_used) return false;
		for (auto& d : sg_dirs) {
			auto& c = lt_cref(p+d);
			if (!c.room && !c.is_wall) return false;
		}
		if (isol_check_init && !isolation_check(r, p, *isol_check_init)) return false;
		return true;
	};
	
	// add resources and decor
	
	TimeSpan time_decor = TimeSpan::since_start();
	
	const float res_fail_chance = 0.45; // chance to fail spawning resource
	
	for (auto& r : lt.rooms)
	{
		bool isolation_check_init = false;
		
		auto is_ok = [&](vec2i p, bool check_isol = true)
		{
			return can_spawn(r, p, check_isol ? &isolation_check_init : nullptr);
		};
		auto add_rnd_res = [&](vec2i at)
		{
			if (rnd.range_n() < res_fail_chance) return; // BALANCE HACK
			
			if (!is_ok(at, false)) return;
			auto ap = EPickable::rnd_ammo(core);
			ap.amount *= rnd.range(0.1, 0.7);
			new EPickable( core, lc.to_center_coord(at), ap );
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
				new EDispenser( core, lc.to_center_coord(at), *rot, increased );
				lt_cref(at).decor_used = true;
			}
		};
		auto add_mdock = [&](vec2i at, std::optional<bool> horiz = {})
		{
			if (auto rot = get_rot(at, horiz)) {
				new EMinidock( core, lc.to_center_coord(at), *rot );
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
		
		auto mk_cont = [&](vec2i at, float r) {
			new EDecor( core, "Box", {at, {1,1}, true}, r, rnd_stat().range_n() < 0.3 ? MODEL_STORAGE_BOX_OPEN : MODEL_STORAGE_BOX );
			return true;
		};
		auto mk_abox = [&](vec2i at, float) {
			new EStorageBox(core, lc.to_center_coord(at));
			return true;
		};
		auto mk_drill = [&](vec2i at, float r) {
			new EMiningDrill(core, lc.to_center_coord(at), r);
			return true;
		};
		auto mk_mk = [&](const char *name, ModelType model, bool is_ghost) {
			return [&core, name, model, is_ghost](vec2i at, float r) {
				if (is_ghost) new EDecorGhost( core, Transform(core.get_lc().to_center_coord(at), r), model );
				else new EDecor( core, name, {at, {1,1}, true}, r, model );
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
			// get & check rnd pos
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
			
			std::vector<vec2i> p_aboxes; // storage boxes position
			
			// spawn storage boxes
			int n_max = std::min(1 + r.area.size().area() / 100, 3);
			for (int i=0, n=0; i<n_max*1.5; ++i)
			{
				if (auto p = next_pos())
				{
					p_aboxes.push_back(*p);
					add_decor(*p, mk_abox, false);
					if (++n == n_max) break;
				}
			}
			if (p_aboxes.empty()) // if no storage boxes were created
			{
				std::vector<vec2i> ps;
				ps.reserve( r.area.size().area() );
				
				r.area.map([&](vec2i p){
					if (is_ok(p, false)) ps.push_back(p);
				});
				rnd.shuffle(ps);
				
				for (auto& p : ps) {
					if (is_ok(p)) {
						p_aboxes.push_back(p);
						add_decor(p, mk_abox, false);
						break;
					}
				}
			}
			
			// spawn rig decor
			n_max = std::min(1 + r.area.size().area() / 100, 3);
			for (int i=0, n=0; i<n_max*1.5; ++i)
			{
				if (auto p = next_pos()) {
					add_decor(*p, mk_drill, false);
					if (++n == n_max) break;
				}
			}
//			model("Drill", MODEL_MINEDRILL, {2,1});
			
			// spawn player resources
			n_max = 3 + r.area.size().area() / 40;
			for (int i=0; i<n_max; ++i)
			{
				vec2i at;
				at.x = rnd.range( r.area.lower().x, r.area.upper().x - 1 );
				at.y = rnd.range( r.area.lower().y, r.area.upper().y - 1 );
				add_rnd_res(at);
			}
			
			// spawn AI resources
			r.area.map([&](vec2i p) // mine walls
			{
				auto& c = lt_cref(p);
				if (c.is_wall || c.decor_used) return;
				
				for (auto& d : sg_dirs)
				{
					auto& c = lt_cref(p + d);
					if (c.is_wall && !c.decor_used)
					{
						if (rnd.range_n() < 0.3) continue; // 70% of all walls
						
						vec2fp at = lc.to_center_coord(p);
						vec2fp vp = lc.to_center_coord(p) + vec2fp(d) * (GameConst::cell_size /2); // at the cell edge
						
						new AI_SimResource(core, {AI_SimResource::T_ROCK, true,
						                    AI_SimResource::max_capacity, AI_SimResource::max_capacity},
						                   at, vp);
					}
				}
			});
			for (auto& p : p_aboxes) // storage boxes
			{
				for (auto& d : sg_dirs)
				{
					if (lt_cref(p + d).is_wall) continue;
					
					vec2fp at = lc.to_center_coord(p) + vec2fp(d) * GameConst::cell_size;
					new AI_SimResource(core, {AI_SimResource::T_ROCK, false, 0, AI_SimResource::max_capacity},
					                   at, lc.to_center_coord(p));
				}
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
						if		(t < 0.4) new EDecor( core, "Conveyor",  {p, {1,1}, true}, rot, MODEL_CONVEYOR );
						else if (t < 0.7) new EDecor( core, "Assembler", {p, {1,1}, true}, rot, MODEL_ASSEMBLER );
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
			vec2fp at = room_ctr(r, true);
			new ETeleport(core, at);
			lt_cref( lc.to_cell_coord(at) ).decor_used = true;
			
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
	
	lc.add_spawn({ LevelControl::SP_PLAYER, GameConst::cell_size * lt.rooms[0].area.fp_center() });
	
	// spawn keys & terminals
	
	auto spawn_key = [&](LevelTerrain::Room& r)
	{
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
		new EPickable( core, lc.to_center_coord(*at), EPickable::SecurityKey{} );
	};
	
	std::vector<LevelTerrain::Room*> key_rooms;
	size_t key_count = 0;
	
	for (auto& r : lt.rooms)
	{
		if		(r.type == LevelTerrain::RM_KEY)     {++key_count; spawn_key(r);}
		else if (r.type == LevelTerrain::RM_TERMINAL) {
			vec2fp p = room_ctr(r);
			new EFinalTerminal(core, p);
			lt_cref(lc.to_cell_coord(p)).decor_used = true;
		}
		else if (r.type == LevelTerrain::RM_LIVING)   key_rooms.push_back(&r);
	}
	
	for (; key_count < GameConst::total_key_count && !key_rooms.empty(); ++key_count)
	{
		size_t i = core.get_random().range_index( key_rooms.size() );
		spawn_key(*key_rooms[i]);
		key_rooms.erase( key_rooms.begin() + i );
	}
	
	// prepare enemy spawn
	
	TimeSpan time_enemies = TimeSpan::since_start();
	
	auto pars_workr = std::make_shared<AI_DroneParams>();
	pars_workr->set_speed(2, 3, 4);
	pars_workr->dist_minimal = 3;
	pars_workr->dist_optimal = 10;
	pars_workr->dist_visible = 14;
	pars_workr->dist_suspect = 16;
	pars_workr->rot_speed = deg_to_rad(90);
	pars_workr->helpcall = AI_DroneParams::HELP_LOW;
	
	auto pars_drone = std::make_shared<AI_DroneParams>();
	pars_drone->set_speed(4, 7, 9);
	pars_drone->dist_minimal = 8;
	pars_drone->dist_optimal = 14;
	pars_drone->dist_visible = 20;
	pars_drone->dist_suspect = 22;
	pars_drone->rot_speed = deg_to_rad(240);
	pars_drone->fov = std::make_pair(deg_to_rad(45), deg_to_rad(90));
	pars_drone->placement_prio = 5;
	
	auto pars_campr = std::make_shared<AI_DroneParams>();
	pars_campr->set_speed(5, 6, 8);
	pars_campr->dist_panic   = 6;
	pars_campr->dist_minimal = 12;
	pars_campr->dist_optimal = 18;
	pars_campr->dist_visible = 24;
	pars_campr->dist_suspect = 28;
	pars_campr->dist_battle = 50;
	pars_campr->rot_speed = deg_to_rad(180);
	pars_campr->is_camper = true;
	pars_campr->fov = {};
	pars_campr->helpcall = AI_DroneParams::HELP_NEVER;
	pars_campr->placement_prio = 30;
	pars_campr->placement_freerad = 1;
	
	// random enemy spawn
	
	size_t total_enemy_count = 0;
	
	for (auto& r : lt.rooms)
	{
		float dl_count = 1;
		float dl_work  = 0;
		float dl_drone = 1;
		float dl_camper = 0;
		float dl_turret = 0;
		
		switch (r.type)
		{
		case LevelTerrain::RM_WORKER:
			dl_count = 0.5;
			dl_work = 2;
			dl_camper = 0.05;
			break;
			
		case LevelTerrain::RM_FACTORY:
			dl_count = 1;
			dl_turret = 0.4;
			dl_camper = 0.1;
			break;
			
		case LevelTerrain::RM_LAB:
			dl_count = 0.7;
			dl_turret = 0.7;
			break;
			
		case LevelTerrain::RM_STORAGE:
			dl_count = 0.6;
			dl_turret = 0.4;
			dl_camper = 0.15;
			break;
			
		case LevelTerrain::RM_KEY:
			dl_count = 0.8;
			dl_turret = 0.1;
			dl_camper = 0.1;
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
		
		std::vector<vec2i> spawn_ps;
		spawn_ps.reserve( r.area.size().area() );
		r.area.map([&](vec2i p)
		{
			auto& c = lt_cref(p);
			if (!c.is_wall /*&& !c.decor_used*/)
				spawn_ps.push_back(p);
		});
		
		rnd.shuffle(spawn_ps);
		num = std::min(num, spawn_ps.size());
		
		float dl_total = dl_work + dl_drone + dl_camper;
		dl_work   = (dl_work   / dl_total);
		dl_drone  = (dl_drone  / dl_total) + dl_work;
		dl_camper = (dl_camper / dl_total) + dl_drone;
		dl_turret = dl_turret / (dl_total + dl_turret);
		
		//
		
		std::vector<std::vector<vec2fp>> base_patrol;
		if (dl_drone > 0)
		{
			const float same_point = GameConst::cell_size * 1.5; // aprrox. diagonal
			std::vector<vec2fp> bps;
			
			r.area.map_inner([&](vec2i p0)
			{
				auto& c0 = lt_cref(p0);
				if (c0.is_wall || c0.decor_used) return;
				
				for (auto& d : sg_dirs)
				{
					auto p = p0 + d;
					auto& c = lt_cref(p);
					
					if (!c.is_wall && c.room != &r) {
						bps.push_back( lc.to_center_coord(p0) );
						return;
					}
				}
			});
			
			base_patrol.reserve( bps.size() );
			for (auto& bp : bps)
			{
				for (auto& ps : base_patrol)
				for (auto& p : ps)
				{
					if (p.dist_squ( bp ) < same_point * same_point)
					{
						ps.push_back(bp);
						goto ok;
					}
				}
				
				base_patrol.emplace_back().push_back(bp);
				ok:;
			}
		}
		
		if (size_t turr_num = num * dl_turret)
		{
			num -= turr_num;
			
			bool isol_init = false;
			int pi = spawn_ps.size() - 1;
			
			for (size_t i=0; i<turr_num && pi >= 0; ++i, --pi)
			{
				vec2i at = spawn_ps[pi];
				if (!can_spawn(r, at, &isol_init)) {
					--i;
					continue;
				}
				
				spawn_ps.erase( spawn_ps.begin() + pi );
				lt_cref(at).is_wall = true;
				
				new ETurret(core, lc.to_center_coord(at), TEAM_BOTS);
			}
		}
		
		for (size_t i=0; i<num; ++i)
		{
			vec2fp at = spawn_ps[i];
			at = at * GameConst::cell_size + vec2fp::one(GameConst::cell_size /2);

			at.x += core.get_random().range_n2() * 0.5;
			at.y += core.get_random().range_n2() * 0.5;
			
			float rnd_k = core.get_random().range_n();
			if		(rnd_k < dl_work)
			{
				static const auto cs = normalize_chances<Weapon*(*)(), 2>({{
					{[]()->Weapon*{return new WpnRocket;}, 1},
					{[]()->Weapon*{return new WpnRifleBot;}, 0.05}
				}});
				new EEnemyDrone(core, at, {pars_workr, MODEL_WORKER, rnd.random_el(cs)(), new AtkPat_Burst, 0.4, true});
			}
			else if (rnd_k < dl_drone)
			{
				std::vector<vec2fp> patrol;
				patrol.reserve( base_patrol.size() );
				for (auto& ps : base_patrol)
				{
					if (ps.size() == 1) patrol.push_back( ps[0] );
					else {
						size_t i = rnd.range_index( ps.size() );
						patrol.push_back( ps[i] );
						ps.erase( ps.begin() + i );
					}
				}
				
				std::rotate( patrol.begin(), patrol.begin() + rnd.range_index( patrol.size() ), patrol.end() );
				if (rnd.flag()) std::reverse( patrol.begin(), patrol.end() );
				
				static const auto cs = normalize_chances<Weapon*(*)(), 2>({{
					{[]()->Weapon*{return new WpnRocket;}, 0.6},
					{[]()->Weapon*{return new WpnRifleBot;}, 0.4}
				}});
				new EEnemyDrone(core, at, {pars_drone, MODEL_DRONE, rnd.random_el(cs)(), {}, 0.7, false, std::move(patrol)});
			}
			else //if (rnd_k < dl_camper)
			{
				new EEnemyDrone(core, at, {pars_campr, MODEL_CAMPER, new WpnElectro(WpnElectro::T_CAMPER), new AtkPat_Sniper, 1});
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
			new EDoor(core, {x, y}, ext, room, plr_only);
			
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
void level_spawn_debug(GameCore& core, LevelTerrain& lt)
{
	auto& lc = core.get_lc();
	new EWall(core, lt.ls_wall);
	
	EEnemyDrone::Init drone;
	drone.pars = std::make_shared<AI_DroneParams>();
	//
	drone.pars->speed = {2, 4, 7, 9};
	drone.pars->dist_minimal = 8;
	drone.pars->dist_optimal = 14;
	drone.pars->dist_visible = 20;
	drone.pars->dist_suspect = 25;
	
	for (auto& sp : lt.dbg_spawns)
	{
		vec2fp pos = lc.to_center_coord(sp.first);
		switch (sp.second)
		{
		case LevelTerrain::DBG_SPAWN_PLAYER:
			lc.add_spawn({ LevelControl::SP_PLAYER, pos });
			break;
			
		case LevelTerrain::DBG_SPAWN_DRONE:
			new EEnemyDrone( core, pos, drone );
			break;
		}
	}
}
