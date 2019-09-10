#include "utils/image_utils.hpp"
#include "utils/noise.hpp"
#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_log.hpp"
#include "vaslib/vas_time.hpp"
#include "vaslib/vas_types.hpp"
#include "level_gen.hpp"

struct Gen1
{
	// Note: corridor data (cells & neis) may be incorrect and is mostly ignored
	struct RoomClass;
	
	struct CorridorEntry
	{
		vec2i pos; ///< Inside corridor
		std::optional<size_t> room_i;
	};
	struct Corridor
	{
		std::vector<vec2i> cells;
		std::vector<CorridorEntry> ents;
	};
	struct Room
	{
		size_t index;
		Rect area;
		
		std::vector<size_t> cor_i;
		const RoomClass* klass;
	};
	struct Cell
	{
		vec2i pos;
		std::optional<size_t> cor_i;
		std::optional<size_t> room_i;
		std::optional<size_t> room_was; ///< before placing islands etc
		
		bool is_border = false;
		int depth; // tmp
	};
	
	
	
	struct RoomClass
	{
		float kch = 0; // chance
		LevelTerrain::RoomType lc_type = LevelTerrain::RM_DEFAULT;
		
		vec2i sz_min; // room size
		vec2i sz_max;
		
		// island params
		float isl_f = 0.7; // chance to generate any at all
		int isl_per = 30; // 1 per how much area
		int isl_max = 8;
		int isl_chn_mul = 1; // retrys per each
		vec2i isl_sz_min = {1,1};
		vec2i isl_sz_max = {2,5};
	};
	
	struct GenParams
	{
		// room params
		std::vector<RoomClass> rm_cs;
		
		int rm_cor_per = 4; // perimeter cells for one cor
		int rm_cor_max = 12; // max cor count
		int rm_cor_add = 3; // additional trys
		
		// corridor params
		int cor_len_min = 1; // min length
		int cor_len_max = 7; // max length
		float cor_rot_f = 0.2; // rotation chance on step
		float cor_dup_f = 0.5; // duplicate corrdior chance
		float cor_cnt_f = 0.8; // corridor interconnection chance
		
		// flags
		bool gen_islands = true;
	};
	
	const GenParams gp;
	
	
	
	//
	vec2i size;
	std::vector<Cell> cs;
	
	//
	std::vector<Room> g_rooms;
	std::vector<Corridor> g_cors;
	
	//
	RandomGen rnd;
	std::vector<size_t> place_q; // rooms to be yet placed
	
	
	
	static GenParams gp_1()
	{
		GenParams gp;
		{
			auto& r = gp.rm_cs.emplace_back();
			r.kch = 0.5;
			r.sz_min = {2,2};
			r.sz_max = {5,4};
			r.isl_f = 0;
			r.lc_type = LevelTerrain::RM_SMALL;
		}{
			auto& r = gp.rm_cs.emplace_back();
			r.kch = 2;
			r.sz_min = {7,6};
			r.sz_max = {12,8};
			r.isl_per = 20;
			r.isl_chn_mul = 2;
		}{	
			auto& r = gp.rm_cs.emplace_back();
			r.kch = 1;
			r.sz_min = {12,8};
			r.sz_max = {24,18};
			r.isl_f = 1;
			r.isl_per = 40;
			r.isl_sz_max = {4,6};
			r.isl_chn_mul = 3;
		}
		for (auto& r : gp.rm_cs) r.isl_per /= 2;
		
		//
		gp.rm_cor_per = 4; // perimeter cells for one cor
		gp.rm_cor_max = 8; // max cor count
		gp.rm_cor_add = 4; // additional trys
		
		// corridor params
		gp.cor_len_min = 2; // min length
		gp.cor_len_max = 6; // max length
		gp.cor_rot_f = -1; // rotation chance on step
		gp.cor_dup_f = 0.1; // duplicate corrdior chance
		gp.cor_cnt_f = 0.5; // corridor interconnection chance
		
		return gp;
	}
	static GenParams gp_2()
	{
		GenParams gp;
		{
			auto& r = gp.rm_cs.emplace_back();
			r.kch = 0.5;
			r.sz_min = {2,2};
			r.sz_max = {5,4};
			r.isl_f = 0;
			r.lc_type = LevelTerrain::RM_SMALL;
		}{
			auto& r = gp.rm_cs.emplace_back();
			r.kch = 2;
			r.sz_min = {3,3};
			r.sz_max = {6,5};
			r.isl_per = 20;
			r.isl_chn_mul = 2;
		}{
			auto& r = gp.rm_cs.emplace_back();
			r.kch = 1;
			r.sz_min = {6,5};
			r.sz_max = {12,10};
			r.isl_f = 1;
			r.isl_per = 40;
			r.isl_sz_max = {4,6};
		}
		
		//
		gp.rm_cor_per = 4; // perimeter cells for one cor
		gp.rm_cor_max = 12; // max cor count
		gp.rm_cor_add = 3; // additional trys
		
		// corridor params
		gp.cor_len_min = 1; // min length
		gp.cor_len_max = 7; // max length
		gp.cor_rot_f = 0.2; // rotation chance on step
		gp.cor_dup_f = 0.5; // duplicate corrdior chance
		gp.cor_cnt_f = 0.8; // corridor interconnection chance
		
		// flags
		gp.gen_islands = false;
		
		return gp;
	}
	static GenParams gp_postp(GenParams gp)
	{
		float s = 0;
		for (auto& r : gp.rm_cs) s += r.kch;
		
		float t = 0;
		for (auto& r : gp.rm_cs) {
			t += r.kch / s;
			r.kch = t;
		}
		
		return gp;
	}
	Gen1(vec2i size_in)
	    :gp(gp_postp(gp_2()))
	{
		// setup
		
		size = size_in + gp.rm_cs.back().sz_max;
		
		cs.resize( size.area() );
		for (int y=0; y<size.y; ++y)
		for (int x=0; x<size.x; ++x)
			cs[y * size.x + x].pos = {x, y};
		
		// generate rooms and corridors
		
		auto nr = &g_rooms.emplace_back();
		nr->index = 0;
		nr->area.sz = {3, 3};
		nr->area.off = size/2 - nr->area.sz/2;
		nr->klass = &gp.rm_cs[0];
		mark_room(*nr);
		place_q.push_back(nr->index);
		
		while (!place_q.empty())
		{
			std::vector<size_t> q;
			q.reserve(gp.rm_cor_max);
			q.swap(place_q);
			
			for (auto& r : q) place_room( &g_rooms[r] );
		}
		
		VLOGI("LevelControl::      {} rooms, {} corridors", g_rooms.size(), g_cors.size());
		
		// remove rooms with single connection
		
		auto rem_cor = [this](size_t i)
		{
			for (auto& e : g_cors[i].ents) {
				auto& v = g_rooms[*e.room_i].cor_i;
				auto it = std::find( v.begin(), v.end(), i );
				if (it != v.end()) v.erase(it);
			}
			g_cors.erase( g_cors.begin() + i );
			for (auto& c : cs) {
				if (!c.cor_i) {}
				else if (*c.cor_i == i) c.cor_i.reset();
				else if (*c.cor_i  > i) --*c.cor_i;
			}
			for (auto& r : g_rooms) {
				for (auto& ci : r.cor_i)
					if (ci > i) --ci;
			}
		};
		auto rem_room = [this, &rem_cor](size_t i)
		{
			g_rooms[i].area.map_outer([&](auto pos) {
				bool ok = false;
				if (auto c = getc(pos + vec2i( 1, 0)); c && c->room_i && *c->room_i != i) ok = true;
				if (auto c = getc(pos + vec2i(-1, 0)); c && c->room_i && *c->room_i != i) ok = true;
				if (auto c = getc(pos + vec2i(0,  1)); c && c->room_i && *c->room_i != i) ok = true;
				if (auto c = getc(pos + vec2i(0, -1)); c && c->room_i && *c->room_i != i) ok = true;
				if (!ok) cref(pos).is_border = false;
			});
			
			for (auto& ci : g_rooms[i].cor_i) rem_cor(ci);
			g_rooms.erase( g_rooms.begin() + i );
			for (auto& c : cs) {
				if (!c.room_i) {}
				else if (*c.room_i == i) c.room_i.reset();
				else if (*c.room_i  > i) --*c.room_i;
			}
			for (auto& c : g_cors) {
				for (auto& e : c.ents)
					if (e.room_i && *e.room_i > i) --*e.room_i;
			}
			for (; i < g_rooms.size(); ++i) --g_rooms[i].index;
		};
		
		while (true)
		{
			bool any = false;
			for (size_t i=0; i<g_rooms.size(); )
			{
				if (g_rooms[i].cor_i.size() > 1) ++i;
				else {
					rem_room(i);
					any = true;
				}
			}
			if (!any) break;
		}
		
		// set room was
		
		for (auto& r : g_rooms)
			r.area.map([&](auto p){ cref(p).room_was = r.index; });
		
		// generate islands
		
		if (gp.gen_islands)
		{
			auto check_entries = [this](Room& r)
			{
				const int d_max = std::numeric_limits<int>::max();
				const Rect zn = {r.area.lower() - vec2i{1,1}, r.area.upper() + vec2i{1,1}, false};
				
				bool cor_any = false;
				zn.map([&](auto p)
				{
					auto& c = cref(p);
					if (c.cor_i) {
						c.depth = cor_any? d_max : 0;
						cor_any = true;
					}
					else if (!c.room_i) c.depth = -1;
					else c.depth = d_max;
				});
				
				for (int d=0 ;; ++d)
				{
					bool ok = false;
					
					zn.map([&](auto pos)
					{
						auto& c = cref(pos);
						if (c.depth != d) return;
						
						const vec2i ps[] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
						for (auto& p : ps)
						{
							auto& nc = cref(pos + p);
							if (nc.depth > d) {
								nc.depth = d + 1;
								ok = true;
							}
						}
					});
					if (!ok) break;
				}
				
				return zn.map_check([&](auto p){ return cref(p).depth != d_max; });
			};
			
			for (auto& r : g_rooms)
			{	
				auto& rc = *r.klass;
				if (rnd.range_n() > rc.isl_f) continue;
				
				int num = std::min( rc.isl_max, r.area.size().area() / rc.isl_per );
				if (!num) num = 1;
				for (int i=0; i<num; ++i)
				{
					for (int j=0; j<rc.isl_chn_mul; ++j)
					{
						vec2i sz;
						sz.x = round(rnd.range(rc.isl_sz_min.x, rc.isl_sz_max.x));
						sz.y = round(rnd.range(rc.isl_sz_min.y, rc.isl_sz_max.y));
						
						if (sz.x >= r.area.size().x) continue;
						if (sz.y >= r.area.size().y) continue;
						
						vec2i off;
						off.x = round(rnd.range(0, r.area.size().x - sz.x));
						off.y = round(rnd.range(0, r.area.size().y - sz.y));
						off += r.area.off;
						
						Rect isl = {off, sz, true};
						isl.map([&](auto p) {
							auto& c = cref(p);
							if (c.room_i == r.index) c.room_i.reset();
						});
						
						if (check_entries(r)) break;
						else {
							isl.map([&](auto p) {
								auto& c = cref(p);
								if (c.room_was == r.index) c.room_i = r.index;
							});
						}
					}
				}
			}
		}
		
		// remove diags
		
		auto rm_diags = [this]
		{
			for (auto& r : g_rooms)
			{
				auto iswall = [&](int x, int y) {
					return !cref(r.area.off + vec2i{x, y}).room_i;
				};
				auto rmrest = [&](int x, int y) {
					auto& c = cref(r.area.off + vec2i{x, y});
					c.room_i = c.room_was;
				};
				
				for (int y = 0; y < r.area.sz.y - 1; ++y)
				for (int x = 0; x < r.area.sz.x - 1; ++x)
				{
					if (iswall(x, y))
					{
						if (iswall(x+1, y+1) && !iswall(x+1, y) && !iswall(x, y+1))
						{
							if (rnd.flag()) rmrest(x, y);
							else rmrest(x+1, y+1);
						}
					}
					else if (!iswall(x+1, y+1) && iswall(x+1, y) && iswall(x, y+1))
					{
						if (rnd.flag()) rmrest(x+1, y);
						else rmrest(x, y+1);
					}
				}
			}
		};
		rm_diags();
		
		// remove 1-wide passageways
		
		for (int y = 2; y < size.y - 3; ++y)
		for (int x = 1; x < size.x - 1; ++x)
		{
			auto iswall = [&](int x, int y) {
				auto& c = cref(vec2i{x, y});
				return !c.room_i && !c.cor_i;
			};
			
			std::optional<int> y1;
			int x1 = x;
			for (; x1 < size.x - 1; ++x1)
			{
				if (iswall(x1, y-1) && !iswall(x1, y) && iswall(x1, y+1))
				{
					if (!iswall(x1, y-2)) {
						if (y1 && *y1 != y-1) break;
						else y1 = y-1;
					}
					if (!iswall(x1, y+2)) {
						if (y1 && *y1 != y+1) break;
						else y1 = y+1;
					}
				}
				else break;
			}
			if (x1 - x < 1) continue;
			
			if (!y1 && !cref({x, y}).cor_i)
			{
				auto avail = [&](int x, int c) {
					if (!cref(vec2i{x, y+c}).room_was) {
						if (y1 && *y1 != y-c) return false;
						y1 = y-c;
					}
					return true;
				};
				
				int t = x;
				for (; t < x1; ++t) {
					if (!avail(t,  1)) break;
					if (!avail(t, -1)) break;
				}
				x1 = t;
			}
			if (!y1) y1 = rnd.flag()? y-1 : y+1;
			
			for (; x < x1; ++x)
			{
				auto& c = cref({x, *y1});
				if (c.room_was) c.room_i = c.room_was;
				else c.cor_i = cref({x, y}).cor_i;
			}
		}
		
		for (int x = 2; x < size.x - 3; ++x)
		for (int y = 1; y < size.y - 1; ++y)
		{
			auto iswall = [&](int x, int y) {
				auto& c = cref(vec2i{x, y});
				return !c.room_i && !c.cor_i;
			};
			
			std::optional<int> x1;
			int y1 = y;
			for (; y1 < size.y - 1; ++y1)
			{
				if (iswall(x-1, y1) && !iswall(x, y1) && iswall(x+1, y1))
				{
					if (!iswall(x-2, y1)) {
						if (x1 && *x1 != x-1) break;
						else x1 = x-1;
					}
					if (!iswall(x+2, y1)) {
						if (x1 && *x1 != x+1) break;
						else x1 = x+1;
					}
				}
				else break;
			}
			if (y1 - y < 1) continue;
			
			if (!x1 && !cref({x, y}).cor_i)
			{
				auto avail = [&](int y, int c) {
					if (!cref(vec2i{x+c, y}).room_was) {
						if (x1 && *x1 != x-c) return false;
						x1 = y-c;
					}
					return true;
				};
				
				int t = y;
				for (; t < y1; ++t) {
					if (!avail(t,  1)) break;
					if (!avail(t, -1)) break;
				}
				x1 = t;
			}
			if (!x1) x1 = rnd.flag()? x-1 : x+1;
			
			for (; y < y1; ++y)
			{
				auto& c = cref({*x1, y});
				if (c.room_was) c.room_i = c.room_was;
				else c.cor_i = cref({x, y}).cor_i;
			}
		}
		
		// remove 1-wide diags
		
		for (int y=1; y < size.y - 2; ++y)
		for (int x=1; x < size.x - 2; ++x)
		{
			auto iswall = [&](int ox, int oy) {
				auto& c = cref({x + ox, y + oy});
				return !c.room_i && !c.cor_i;
			};
			auto corr = [&](int ox, int oy) {
				auto& c = cref({x + ox, y + oy});
				if (c.room_was) c.room_i = c.room_was;
				else {
					if (!c.cor_i) c.cor_i = cref({x+0, y+0}).cor_i;
					if (!c.cor_i) c.cor_i = cref({x+1, y+0}).cor_i;
					if (!c.cor_i) c.cor_i = cref({x+0, y+1}).cor_i;
					if (!c.cor_i) c.cor_i = cref({x+1, y+1}).cor_i;
				}
			};
			
			if		( iswall(0,0) && !iswall(1,0) && !iswall(0,1) && !iswall(1, 1) && (iswall(-1, 0) || iswall(0, -1))) corr(0,0);
			else if (!iswall(0,0) &&  iswall(1,0) && !iswall(0,1) && !iswall(1, 1) && (iswall( 2, 0) || iswall(0, -1))) corr(1,0);
			else if (!iswall(0,0) && !iswall(1,0) &&  iswall(0,1) && !iswall(1, 1) && (iswall(-1, 0) || iswall(0,  2))) corr(0,1);
			else if (!iswall(0,0) && !iswall(1,0) && !iswall(0,1) &&  iswall(1, 1) && (iswall( 2, 0) || iswall(0,  2))) corr(1,1);
		}
		
		// fin
		
		// prepare player spawn
		g_rooms[0].area.map([this](vec2i p)
		{
			auto& c = cref(p);
			c.room_i = c.room_was;
		});
		
		rm_diags();
		
//		g_rooms.shrink_to_fit();
//		g_cors.shrink_to_fit();
	
		VLOGI("Remains: {} rooms, {} corridors", g_rooms.size(), g_cors.size());
	}
	void mark_room(Room& r)
	{
		r.area.map      ([&](auto p){ cref(p).room_i = r.index; });
		r.area.map_outer([&](auto p){ cref(p).is_border = true; });
	}
	void place_room(Room* r)
	{
		size_t r_index = r->index;
		
		std::vector<std::pair<Cell*, vec2i>> bord;
		bord.reserve (r->area.size().perimeter());
		
		mark_room(*r);
		
		auto mark_border = [&](vec2i off, vec2i dir) {
			auto& c = cref(off + r->area.off);
			if (!c.cor_i)
				bord.emplace_back(&c, dir);
		};
		for (int y=0; y<r->area.sz.y; ++y) {
			mark_border({-1,y}, {-1,0});
			mark_border({r->area.sz.x, y}, {1,0});
		}
		for (int x=0; x<r->area.sz.x; ++x) {
			mark_border({x,-1}, {0,-1});
			mark_border({x, r->area.sz.y}, {0,1});
		}
		
		auto gen_corridor = [&]
		{
			size_t i = rnd.range_index(bord.size());
			auto& c0 = bord[i];
			
			{	vec2i p = c0.second; p.rot90cw(); p += c0.first->pos;
				if (cref(p).cor_i || cref(p).room_i) return;
				p = c0.second; p.rot90ccw(); p += c0.first->pos;
				if (cref(p).cor_i || cref(p).room_i) return;
			}
			
			std::vector<Cell*> cs;
			cs.reserve(gp.cor_len_max);
			cs.push_back(c0.first);
			
			vec2i dir = c0.second;
			
			int len = rnd.range_index (gp.cor_len_min, gp.cor_len_max);
			for (int i=1; i<len; ++i)
			{
				if (rnd.range_n() < gp.cor_rot_f)
				{
					if (dir == c0.second) {
						if (rnd.flag()) dir.rot90cw();
						else dir.rot90ccw();
					}
					else dir = c0.second;
				}
				
				vec2i np = cs.back()->pos + dir;
				auto c = getc(np);
				if (!c) return;
				
				cs.push_back(c);
				if (c->cor_i || c->room_i) {
					if (rnd.range_n() > gp.cor_cnt_f) return;
					break;
				}
				if (c->is_border) {
					Cell* n = nullptr;
					const vec2i ds[] = {{0,1}, {0,-1}, {1,0}, {-1,0}};
					for (auto& d : ds) {
						c = getc(np + d);
						if (c->cor_i || c->room_i) {
							if (n) return;
							if (rnd.range_n() > gp.cor_cnt_f) return;
							n = c;
						}
					}
					if (!n) return;
					cs.push_back(n);
					break;
				}
			}
			
			Corridor* cor;
			size_t cor_ix;
			
			if		(cs.back()->cor_i)
			{
				cor_ix = *cs.back()->cor_i;
				cor = &g_cors[cor_ix];
				cs.pop_back();
			}
			else if (cs.back()->room_i)
			{
				for (auto& c : g_cors) {
					bool a = false, b = false;
					for (auto& e : c.ents) {
						if (e.room_i == r->index) a = true;
						if (e.room_i == cs.back()->room_i) b = true;
					}
					if (a && b && c.ents.size() == 2) {
						if (rnd.range_n() > gp.cor_dup_f) return;
					}
				}
				
				reserve_more_block(g_cors, 4096);
				cor_ix = g_cors.size();
				cor = &g_cors.emplace_back();
				
				auto& en = cor->ents.emplace_back();
				en.pos = cs.back()->pos;
				en.room_i = cs.back()->room_i;
				g_rooms[*en.room_i].cor_i.push_back(cor_ix);
				
				cs.pop_back();
			}
			else
			{
				dir = c0.second;
				
				const RoomClass* rc = nullptr;
				float rc_kch = rnd.range_n();
				for (auto& r : gp.rm_cs)
				{
					if (rc_kch < r.kch) {
						rc = &r;
						break;
					}
				}
				if (!rc) rc = &gp.rm_cs.back();
				
				Rect ar;
				ar.sz.x = round(rnd.range(rc->sz_min.x, rc->sz_max.x));
				ar.sz.y = round(rnd.range(rc->sz_min.y, rc->sz_max.y));
				
				ar.off = cs.back()->pos + dir;
				if (dir.x < 0) ar.off.x -= ar.sz.x - 1;
				if (dir.y < 0) ar.off.y -= ar.sz.y - 1;
				
				if (dir.x) ar.off.y -= rnd.range_index(ar.sz.y-1);
				else       ar.off.x -= rnd.range_index(ar.sz.x-1);
				
				int safety = gp.cor_len_max + 3;
				if (ar.off.x - safety < 0 ||
				    ar.off.y - safety < 0 ||
				    ar.off.x + safety + ar.sz.x > size.x ||
				    ar.off.y + safety + ar.sz.y > size.y
				    )
					return;
				
				for (int y=0; y<ar.sz.y; ++y)
				for (int x=0; x<ar.sz.x; ++x)
				{
					auto& c = cref(vec2i{x,y} + ar.off);
					if (c.is_border || c.cor_i || c.room_i) return;
				}
				
				reserve_more_block(g_rooms, 1024);
				auto nr = &g_rooms.emplace_back();
				r = &g_rooms[r_index];
				
				nr->index = g_rooms.size() - 1;
				nr->area = ar;
				nr->klass = rc;
				mark_room(*nr);
				place_q.push_back(nr->index);
				
				cor_ix = g_cors.size();
				cor = &g_cors.emplace_back();
				nr->cor_i.push_back(cor_ix);
				
				auto& en = cor->ents.emplace_back();
				en.pos = cs.back()->pos;
				en.room_i = nr->index;
			}
			
			reserve_more(cor->cells, cs.size());
			for (auto& c : cs) {
				cor->cells.push_back(c->pos);
				c->cor_i = cor_ix;
			}
			
			auto& en = cor->ents.emplace_back();
			en.pos = cs.front()->pos;
			en.room_i = r->index;
			g_rooms[*en.room_i].cor_i.push_back(cor_ix);
		};
		
		int cor_perim = r->area.size().perimeter() / gp.rm_cor_per;
		int cor_num = 1 + rnd.range_index (std::min (gp.rm_cor_max, cor_perim));
		cor_num += gp.rm_cor_add;
		for (int i=0; i<cor_num; ++i) gen_corridor();
	}
	Cell* getc(vec2i pos)
	{
		if (pos.x < 0 || pos.y < 0 || pos.x >= size.x || pos.y >= size.y) return nullptr;
		return &cs[pos.y * size.x + pos.x];
	}
	Cell& cref(vec2i pos)
	{
		if (auto c = getc(pos)) return *c;
		LOG_THROW("LevelControl::cref() failed");
	}
	void remove_border()
	{
		const vec2i orig_size = size;
		
		int x_rem = 0, y_rem = 0;
		for (int x=0; x<size.x; ++x)
		{
			int y=0;
			for (; y<size.y; ++y)
			{
				auto& c = cref({x,y});
				if (c.is_border || c.cor_i || c.room_i)
					break;
			}
			if (y != size.y) break;
			++x_rem;
		}
		for (int y=0; y<size.x; ++y)
		{
			int x=0;
			for (; x<size.x; ++x)
			{
				auto& c = cref({x,y});
				if (c.is_border || c.cor_i || c.room_i)
					break;
			}
			if (x != size.x) break;
			++y_rem;
		}
		
		if (x_rem) --x_rem; // bug hack
		if (y_rem) --y_rem;
		
		for (auto& r : g_rooms) r.area.off -= {x_rem, y_rem};
		for (auto& c : g_cors) {
			for (auto& p : c.cells) p -= {x_rem, y_rem};
			for (auto& e : c.ents) e.pos -= {x_rem, y_rem};
		}
		for (auto& c : cs) c.pos -= {x_rem, y_rem};
		
		cs.erase( cs.begin(), cs.begin() + y_rem * size.x );
		size.y -= y_rem;
		
		for (int y=size.y-1; y>=0; --y) {
			auto it = cs.begin() + y * size.x;
			cs.erase(it, it + x_rem);
		}
		size.x -= x_rem;
		
		//
		
		x_rem = size.x;
		y_rem = size.y;
		
		for (int x = size.x - 1; x > 0; --x)
		{
			int y=0;
			for (; y<size.y; ++y)
			{
				auto& c = cref({x,y});
				if (c.is_border || c.cor_i || c.room_i)
					break;
			}
			if (y != size.y) break;
			--x_rem;
		}
		for (int y = size.y - 1; y > 0; --y)
		{
			int x=0;
			for (; x<size.x; ++x)
			{
				auto& c = cref({x,y});
				if (c.is_border || c.cor_i || c.room_i)
					break;
			}
			if (x != size.x) break;
			--y_rem;
		}
		
		if (x_rem != size.x) ++x_rem; // bug hack
		if (y_rem != size.y) ++y_rem;
		
		if (y_rem != size.y) cs.erase( cs.begin() + y_rem * size.x, cs.end() );
		size.y = y_rem;
		
		for (int y=size.y-1; y>=0; --y) {
			auto it = cs.begin() + y * size.x;
			cs.erase(it + x_rem, it + size.x);
		}
		size.x = x_rem;
		
		VLOGI("Size reduced {}x{} -> {}x{}", orig_size.x, orig_size.y, size.x, size.y);
	}
	void convert(LevelTerrain& t)
	{
		remove_border();
		
		t.grid_size = size;
		t.cs.reserve( cs.size() );
		t.rooms.reserve( g_rooms.size() );
		
		for (auto& r : g_rooms)
		{
			auto& nr = t.rooms.emplace_back();
			nr.area = r.area;
			nr.type = r.klass->lc_type;
		}
		for (auto& c : cs)
		{
			auto& nc = t.cs.emplace_back();
			nc.is_wall = (!c.cor_i && !c.room_i);
		}
		
		auto& r0 = g_rooms[0].area;
		t.spps.emplace_back(LevelControl::SP_PLAYER, r0.off + r0.sz /2);
	}
};



LevelTerrain* LevelTerrain::generate(const GenParams& pars)
{
	LevelTerrain* lt_ptr = new LevelTerrain;
	LevelTerrain& lt = *lt_ptr;
	
	TimeSpan t0 = TimeSpan::since_start();
	Gen1( pars.grid_size ).convert( lt );
	lt.cell_size = pars.cell_size;
	
	TimeSpan t1 = TimeSpan::since_start();
	lt.ls_wall = lt.vectorize();
	
	TimeSpan t2 = TimeSpan::since_start();
	lt.ls_grid = lt.gen_grid();
	
	if (log_test_level(LogLevel::Debug))
	{
		TimeSpan t3 = TimeSpan::since_start();
		
		size_t pts = 0, lps = 0;
		for (auto& c : lt.ls_wall) {
			pts += c.size();
			if (c.front().equals(c.back(), 1e-10)) ++lps;
		}
		
		VLOGD("LevelTerrain::() generation: {:.3f} seconds", (t1 - t0).seconds());
		VLOGD("                 vectorize:  {:.3f} seconds, {} chains, {} loops, {} points",
		      (t2 - t1).seconds(), lt.ls_wall.size(), lps, pts);
		VLOGD("                 gen_grid:   {:.3f} seconds, {} lines", (t3 - t2).seconds(), lt.ls_grid.size());
		VLOGD("                 total:      {:.3f} seconds", (t3 - t0).seconds());
	}
	
	return lt_ptr;
}
LevelTerrain* LevelTerrain::load_testlvl(float cell_size, const char *filename)
{
	ImageInfo img;
	if (!img.load(filename, ImageInfo::FMT_RGB))
		throw std::runtime_error("LevelTerrain::load_testlvl() failed");
	
	LevelTerrain* lt = new LevelTerrain;
	lt->cell_size = cell_size;
	lt->grid_size = img.get_size();
	lt->cs.resize( lt->grid_size.area() );
	
	auto& rm = lt->rooms.emplace_back();
	rm.area = {{1,1}, lt->grid_size - vec2i::one(2), true};
	rm.type = RM_DEFAULT;
	
	rm.area.map([lt](auto p) {
		lt->cs[p.y * lt->grid_size.x + p.x].is_wall = false;
	});
	
	for (int y=0; y < lt->grid_size.y; ++y)
	for (int x=0; x < lt->grid_size.x; ++x)
	{
		int v = img.get_pixel_fast({x,y});
		switch (v)
		{
		case 0:
			lt->cs[y * lt->grid_size.x + x].is_wall = true;
			break;
			
		case 0xffffff:
			break;
			
		case 0xff0000:
			lt->spps.emplace_back(LevelControl::SP_PLAYER, vec2i{x,y});
			break;
			
		case 0x00ff00:
			lt->spps.emplace_back(LevelControl::SP_TEST_AI, vec2i{x,y});
			break;
			
		case 0xffff00:
			lt->spps.emplace_back(LevelControl::SP_TEST_BOX, vec2i{x,y});
			break;
			
		default:
			throw std::runtime_error("LevelTerrain::load_testlvl() unknown color");
		}
	}
	
	lt->ls_wall = lt->vectorize();
	lt->ls_grid = lt->gen_grid();
	return lt;
}



std::vector<std::vector<vec2fp>> LevelTerrain::vectorize() const
{
	// generate (straight) segments
	
	std::vector<std::pair<vec2i, vec2i>> ls;
	
	for (int y=0; y < grid_size.y - 1; ++y)
	for (int x=0; x < grid_size.x - 1; ++x)
	{
		size_t i = y * grid_size.x + x;
		if (cs[i].is_wall != cs[i + 1].is_wall)
		{
			size_t j = ls.size() - 1;
			for (; j != size_t_inval; --j) {
				if (ls[j].first .x == x+1 &&
				    ls[j].second.x == x+1 &&
				    ls[j].second.y == y) break;
				if (ls[j].second.y < y) {
					j = size_t_inval;
					break;
				}
			}
			if (j != size_t_inval) ++ls[j].second.y;
			else {
				reserve_more_block(ls, 4096);
				ls.push_back({{x+1, y}, {x+1, y+1}});
			}
		}
		if (cs[i].is_wall != cs[i + grid_size.x].is_wall)
		{
			size_t j = ls.size() - 1;
			for (; j != size_t_inval; --j) {
				if (ls[j].first .y == y+1 &&
				    ls[j].second.y == y+1 &&
				    ls[j].second.x == x) break;
				if (ls[j].first.y <= y) {
					j = size_t_inval;
					break;
				}
			}
			if (j != size_t_inval) ++ls[j].second.x;
			else {
				reserve_more_block(ls, 4096);
				ls.push_back({{x, y+1}, {x+1, y+1}});
			}
		}
	}
	
	// join segments into chains
	
	std::vector<std::vector<vec2i>> cs;
	cs.reserve( 65536 );
	
	for (auto& seg : ls)
	{
		size_t ci = 0;
		for (; ci < cs.size(); ++ci)
		{
			auto& c = cs[ci];
			auto place = [&c](auto it, auto a) {
				c.emplace(it, a.x, a.y);
				reserve_more_block(c, 1024);
			};
			if		(c.front() == seg.first)  place( c.begin(), seg.second );
			else if (c.front() == seg.second) place( c.begin(), seg.first  );
			else if (c.back()  == seg.first)  place( c.end(),   seg.second );
			else if (c.back()  == seg.second) place( c.end(),   seg.first  );
			else continue;
			break;
		}
		if (ci == cs.size()) {
			auto& c = cs.emplace_back();
			reserve_more_block(c, 1024);
			c.emplace_back(seg.first);
			c.emplace_back(seg.second);
		}
	}
	ls.clear();
	
	// convert
	
	std::vector<std::vector<vec2fp>> out;
	out.reserve( cs.size() );
	for (auto& c : cs)
	{
		auto& nc = out.emplace_back();
		nc.reserve( c.size() );
		for (auto& p : c)
			nc.emplace_back( p * cell_size );
	}
	
	return out;
}
std::vector<std::pair<vec2fp, vec2fp>> LevelTerrain::gen_grid() const
{
	std::vector<std::pair<vec2fp, vec2fp>> rs;
	std::vector<int> flag;
	flag.resize( grid_size.area() );
	
	for (int y=0; y < grid_size.y - 1; ++y)
	for (int x=0; x < grid_size.x - 1; ++x)
	{
		size_t i = y * grid_size.x + x;
		if (cs[i].is_wall) continue;
		
		if (!(flag[i] & 1) && !cs[i + 1].is_wall)
		{
			auto& r = rs.emplace_back();
			r.first.x = (x + 1) * cell_size;
			r.first.y = y * cell_size;
			
			int ny = y + 1;
			for (; ny < grid_size.y - 1; ++ny)
			{
				size_t j = ny * grid_size.x + x;
				if (cs[j].is_wall || cs[j + 1].is_wall) break;
				flag[j] |= 1;
			}
			r.second.x = r.first.x;
			r.second.y = ny * cell_size;
		}
		if (!(flag[i] & 2) && !cs[i + grid_size.x].is_wall)
		{
			auto& r = rs.emplace_back();
			r.first.x = x * cell_size;
			r.first.y = (y + 1) * cell_size;
			
			int nx = x + 1;
			for (; nx < grid_size.x - 1; ++nx)
			{
				size_t j = y * grid_size.x + nx;
				if (cs[j].is_wall || cs[j + grid_size.x].is_wall) break;
				flag[j] |= 2;
			}
			r.second.x = nx * cell_size;
			r.second.y = r.first.y;
		}
	}
	
	return rs;
}



ImageInfo LevelTerrain::draw_grid() const
{
	ImageInfo img;
	ImagePointBrush br;
	img.reset(grid_size, ImageInfo::FMT_ALPHA);
	
	auto& ls = ls_wall;
	auto& gs = ls_grid;
	
	int cz = int_round(cell_size);
	img.reset(grid_size * cz, ImageInfo::FMT_ALPHA);
	
	br.clr = 64;
	for (auto& g : gs)
		draw_line(img, g.first.int_round(), g.second.int_round(), br);
	
	br.clr = 255;
	for (auto& c : ls)
	{
		vec2i prev = c.front().int_round();
		for (size_t i=1; i<c.size(); ++i)
		{
			auto p = c[i].int_round();
			draw_line(img, prev, p, br);
			prev = p;
		}
	}
	
	return img;
}
void LevelTerrain::test_save(const char *prefix) const
{
	std::string fn_line = std::string(prefix) + "_line.png";
	std::string fn_grid = std::string(prefix) + "_grid.png";
	
	ImageInfo img;
	img.reset(grid_size, ImageInfo::FMT_ALPHA);
	
	for (int y=0; y < grid_size.y; ++y)
	for (int x=0; x < grid_size.x; ++x)
	{
		auto& c = cs[y * grid_size.x + x];
		
		int v;
		if (1) {
			auto i = std::find_if(rooms.begin(), rooms.end(), [&](auto& v){ return v.area.contains_le({x,y}); });
			if (i != rooms.end()) v = c.is_wall? 64 : 255;
			else                  v = c.is_wall? 0  : 128;
		}
		else v = (!c.is_wall) * 255;
		
		img.raw()[y * grid_size.x + x] = v;
	}
	
	img.save(fn_grid.c_str());
	draw_grid().save(fn_line.c_str());
}
