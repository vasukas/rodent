#include "utils/noise.hpp"
#include "utils/res_image.hpp"
#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_log.hpp"
#include "vaslib/vas_time.hpp"
#include "level_gen.hpp"

struct Gen1
{
	struct CorridorEntry
	{
		vec2i pos; ///< Inside corridor
		size_t room_i = size_t_inval;
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
	};
	struct Cell
	{
		vec2i pos;
		size_t cor_i = size_t_inval;
		size_t room_i = size_t_inval;
		
		bool is_border = false;
		int depth; // tmp
	};
	
	
	
	// room params
	const vec2i rm_sz_min = {2,2}; // room size
	const vec2i rm_sz_max = {18,14};
	const int rm_cor_per = 4; // perimeter cells for one cor
	const int rm_cor_max = 8; // max cor count
	const int rm_cor_add = 4; // additional trys
	
	// corridor params
	const int cor_len_min = 1; // min length
	const int cor_len_max = 3; // max length
	const float cor_rot_f = -1; // rotation chance on step
	const float cor_dup_f = 0.1; // duplicate corrdior chance
	const float cor_cnt_f = 0.5; // corridor interconnection chance
	
	// island params
	const float isl_f = 0.7; // chance to generate any at all
	const int isl_per = 30; // 1 per how much area
	const int isl_max = 8;
	const int isl_chn_mul = 1; // retrys per each
	const vec2i isl_sz_min = {1,1};
	const vec2i isl_sz_max = {2,5};
	
	
	
	//
	vec2i size;
	std::vector<Cell> cs;
	
	//
	std::vector<Room> g_rooms;
	std::vector<Corridor> g_cors;
	
	//
	RandomGen rnd;
	std::vector<Room*> place_q; // rooms to be yet placed
	
	
	
	Gen1(vec2i _size): size(_size + rm_sz_max * 3)
	{
		cs.resize( size.area() );
		for (int y=0; y<size.y; ++y)
		for (int x=0; x<size.x; ++x)
			cs[y * size.x + x].pos = {x, y};
		
		int corsadd_total = cor_len_min + (cor_len_max - cor_len_min) / 2;
		vec2i rmavg_total = rm_sz_min + (rm_sz_max - rm_sz_min) / 2;
		size_t rooms_total = size.area() / (rmavg_total + vec2i::one(corsadd_total)).area();
		size_t cors_total = rooms_total * (rmavg_total.perimeter() / rm_cor_per);
		VLOGI("LevelControl: {} rooms, {} corridors", rooms_total, cors_total);
		
		g_rooms.reserve(rooms_total);
		g_cors.reserve(cors_total);
		
		// generate rooms and corridors
		
		auto nr = &g_rooms.emplace_back();
		nr->index = 0;
		nr->area.sz = {3, 3};
		nr->area.off = size/2 - nr->area.sz/2;
		mark_room(*nr);
		place_q.push_back(nr);
		
		while (!place_q.empty())
		{
			std::vector<Room*> q;
			q.reserve(rm_cor_max);
			q.swap(place_q);
			
			for (auto& r : q) place_room(*r);
		}
		
		VLOGI("Actually generated: {} rooms, {} corridors", g_rooms.size(), g_cors.size());
		
		// remove rooms with single connection
		
		auto rem_cor = [this](size_t i)
		{
			for (auto& e : g_cors[i].ents) {
				auto& v = g_rooms[e.room_i].cor_i;
				auto it = std::find( v.begin(), v.end(), i );
				if (it != v.end()) v.erase(it);
			}
			g_cors.erase( g_cors.begin() + i );
			for (auto& c : cs) {
				if (c.cor_i == i) c.cor_i = size_t_inval;
				else if (c.cor_i > i && c.cor_i != size_t_inval) --c.cor_i;
			}
			for (auto& r : g_rooms) {
				for (auto& ci : r.cor_i)
					if (ci > i) --ci;
			}
		};
		auto rem_room = [this, &rem_cor](size_t i)
		{
			auto& r = g_rooms[i].area;
			auto unmark = [&](vec2i off) {
				auto pos = off + r.off;
				bool ok = false;
				if (auto c = getc(pos + vec2i( 1, 0)); c && c->room_i != i && c->room_i != size_t_inval) ok = true;
				if (auto c = getc(pos + vec2i(-1, 0)); c && c->room_i != i && c->room_i != size_t_inval) ok = true;
				if (auto c = getc(pos + vec2i(0,  1)); c && c->room_i != i && c->room_i != size_t_inval) ok = true;
				if (auto c = getc(pos + vec2i(0, -1)); c && c->room_i != i && c->room_i != size_t_inval) ok = true;
				if (!ok) cref(pos).is_border = false;
			};
			for (int y=-1; y<=r.sz.y; ++y) {
				unmark({-1,y});
				unmark({r.sz.x, y});
			}
			for (int x=-1; x<=r.sz.x; ++x) {
				unmark({x,-1});
				unmark({x, r.sz.y});
			}
			
			for (auto& ci : g_rooms[i].cor_i) rem_cor(ci);
			g_rooms.erase( g_rooms.begin() + i );
			for (auto& c : cs) {
				if (c.room_i == i) c.room_i = size_t_inval;
				else if (c.room_i > i && c.room_i != size_t_inval) --c.room_i;
			}
			for (auto& c : g_cors) {
				for (auto& e : c.ents)
					if (e.room_i > i && e.room_i != size_t_inval) --e.room_i;
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
		
		// generate islands
		
		auto check_entries = [this](Room& r)
		{
			const int d_max = std::numeric_limits<int>::max();
			
			bool cor_any = false;
			for (int y=-1; y<=r.area.sz.y; ++y)
			for (int x=-1; x<=r.area.sz.x; ++x)
			{
				auto& c = cref(vec2i{x,y} + r.area.off);
				if (c.cor_i != size_t_inval) {
					c.depth = cor_any? d_max : 0;
					cor_any = true;
				}
				else if (c.room_i == size_t_inval) c.depth = -1;
				else c.depth = d_max;
			}
			
			for (int d=0 ;; ++d)
			{
				bool ok = false;
				
				for (int y=-1; y<=r.area.sz.y; ++y)
				for (int x=-1; x<=r.area.sz.x; ++x)
				{
					auto& c = cref(vec2i{x,y} + r.area.off);
					if (c.depth != d) continue;
					
					const vec2i ps[] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
					for (auto& p : ps)
					{
						auto& nc = cref(c.pos + p);
						if (nc.depth > d) {
							nc.depth = d + 1;
							ok = true;
						}
					}
				}
				
				if (!ok) break;
			}
			
			for (int y=-1; y<=r.area.sz.y; ++y)
			for (int x=-1; x<=r.area.sz.x; ++x)
			{
				auto& c = cref(vec2i{x,y} + r.area.off);
				if (c.depth == d_max) return false;
			}
			return true;
		};
		auto switch_room = [this](size_t i, const std::vector<vec2i>& list)
		{
			for (auto& p : list)
			{
				auto& c = cref(p);
				if (c.room_i == i) c.room_i = size_t_inval;
				else if (c.room_i == size_t_inval) c.room_i = i; 
			}
		};
		
		std::vector<vec2i> list;
		list.reserve( rm_sz_max.area() );
		
		for (auto& r : g_rooms)
		{
			if (rnd.range_n() > isl_f) continue;
			
			int num = std::min( isl_max, r.area.size().area() / isl_per );
			if (!num) num = 1;
			for (int i=0; i<num; ++i)
			{
				for (int j=0; j<isl_chn_mul; ++j)
				{
					vec2i sz;
					sz.x = round(rnd.range(isl_sz_min.x, isl_sz_max.x));
					sz.y = round(rnd.range(isl_sz_min.y, isl_sz_max.y));
					
					if (sz.x >= r.area.size().x) continue;
					if (sz.y >= r.area.size().y) continue;
					
					vec2i off;
					off.x = round(rnd.range(0, r.area.size().x - sz.x));
					off.y = round(rnd.range(0, r.area.size().y - sz.y));
					off += r.area.off;
					
					list.clear();
					for (int y=0; y < sz.y; ++y)
					for (int x=0; x < sz.x; ++x)
						list.push_back(off + vec2i{x,y});
					
					switch_room(r.index, list);
					bool ok = check_entries(r);
					
					if (ok) break;
					else switch_room(r.index, list);
				}
			}
		}
		
		// remove diags
		
		for (auto& r : g_rooms)
		{
			auto iswall = [&](int x, int y) {
				return cref(r.area.off + vec2i{x, y}).room_i == size_t_inval;
			};
			auto rmrest = [&](int x, int y) {
				cref(r.area.off + vec2i{x, y}).room_i = r.index;
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
		
		// fin
		
		g_rooms.shrink_to_fit();
		g_cors.shrink_to_fit();
		
		VLOGI("Remains: {} rooms, {} corridors", g_rooms.size(), g_cors.size());
	}
	void mark_room(Room& r)
	{
		for (int y=0; y<r.area.sz.y; ++y)
		for (int x=0; x<r.area.sz.x; ++x)
			cref(vec2i{x,y} + r.area.off).room_i = r.index;
		
		auto mark_border = [&](vec2i off) {
			cref(off + r.area.off).is_border = true;
		};
		for (int y=-1; y<=r.area.sz.y; ++y) {
			mark_border({-1,y});
			mark_border({r.area.sz.x, y});
		}
		for (int x=-1; x<=r.area.sz.x; ++x) {
			mark_border({x,-1});
			mark_border({x, r.area.sz.y});
		}
	}
	void place_room(Room& r)
	{
		std::vector<std::pair<Cell*, vec2i>> bord;
		bord.reserve (r.area.size().perimeter());
		
		mark_room(r);
		
		auto mark_border = [&](vec2i off, vec2i dir) {
			auto& c = cref(off + r.area.off);
			if (c.cor_i == size_t_inval)
				bord.emplace_back(&c, dir);
		};
		for (int y=0; y<r.area.sz.y; ++y) {
			mark_border({-1,y}, {-1,0});
			mark_border({r.area.sz.x, y}, {1,0});
		}
		for (int x=0; x<r.area.sz.x; ++x) {
			mark_border({x,-1}, {0,-1});
			mark_border({x, r.area.sz.y}, {0,1});
		}
		
		auto room_ptr = [this](size_t i)
		{
			return i == std::string::npos ? nullptr : &g_rooms[i];
		};
		auto cor_ptr = [this](size_t i)
		{
			return i == std::string::npos ? nullptr : &g_cors[i];
		};
		
		auto gen_corridor = [&]()
		{
			if (g_cors.size() == g_cors.capacity()) return;
			
			size_t i = rnd.range_index(bord.size());
			auto& c0 = bord[i];
			
			{	vec2i p = c0.second; p.rot90cw(); p += c0.first->pos;
				if (cor_ptr(cref(p).cor_i) || room_ptr(cref(p).room_i)) return;
				p = c0.second; p.rot90ccw(); p += c0.first->pos;
				if (cor_ptr(cref(p).cor_i) || room_ptr(cref(p).room_i)) return;
			}
			
			std::vector<Cell*> cs;
			cs.reserve(cor_len_max);
			cs.push_back(c0.first);
			
			vec2i dir = c0.second;
			
			int len = rnd.range_index (cor_len_min, cor_len_max);
			for (int i=1; i<len; ++i)
			{
				if (rnd.range_n() < cor_rot_f)
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
				if (cor_ptr(c->cor_i) || room_ptr(c->room_i)) {
					if (rnd.range_n() > cor_cnt_f) return;
					break;
				}
				if (c->is_border) {
					Cell* n = nullptr;
					const vec2i ds[] = {{0,1}, {0,-1}, {1,0}, {-1,0}};
					for (auto& d : ds) {
						c = getc(np + d);
						if (cor_ptr(c->cor_i) || room_ptr(c->room_i)) {
							if (n) return;
							if (rnd.range_n() > cor_cnt_f) return;
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
			
			if		(cor_ptr(cs.back()->cor_i))
			{
				cor_ix = cs.back()->cor_i;
				cor = cor_ptr(cor_ix);
				cs.pop_back();
			}
			else if (room_ptr(cs.back()->room_i))
			{
				for (auto& c : g_cors) {
					bool a = false, b = false;
					for (auto& e : c.ents) {
						if (room_ptr(e.room_i) == &r) a = true;
						if (room_ptr(e.room_i) == room_ptr(cs.back()->room_i)) b = true;
					}
					if (a && b && c.ents.size() == 2) {
						if (rnd.range_n() > cor_dup_f) return;
					}
				}
				
				cor_ix = g_cors.size();
				cor = &g_cors.emplace_back();
				
				auto& en = cor->ents.emplace_back();
				en.pos = cs.back()->pos;
				en.room_i = cs.back()->room_i;
				g_rooms[en.room_i].cor_i.push_back(cor_ix);
				
				cs.pop_back();
			}
			else
			{
				if (g_rooms.size() == g_rooms.capacity()) return;
				dir = c0.second;
				
				Rect ar;
				ar.sz.x = round(rnd.range(rm_sz_min.x, rm_sz_max.x));
				ar.sz.y = round(rnd.range(rm_sz_min.y, rm_sz_max.y));
				
				ar.off = cs.back()->pos + dir;
				if (dir.x < 0) ar.off.x -= ar.sz.x - 1;
				if (dir.y < 0) ar.off.y -= ar.sz.y - 1;
				
				if (dir.x) ar.off.y -= rnd.range_index(ar.sz.y-1);
				else       ar.off.x -= rnd.range_index(ar.sz.x-1);
				
				int safety = cor_len_max + 3;
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
					if (c.is_border || cor_ptr(c.cor_i) || room_ptr(c.room_i)) return;
				}
				
				auto nr = &g_rooms.emplace_back();
				nr->index = g_rooms.size() - 1;
				nr->area = ar;
				mark_room(*nr);
				place_q.push_back(nr);
				
				cor_ix = g_cors.size();
				cor = &g_cors.emplace_back();
				auto& en = cor->ents.emplace_back();
				en.pos = cs.back()->pos;
				en.room_i = nr->index;
				nr->cor_i.push_back(cor_ix);
			}
			
			reserve_more(cor->cells, cs.size());
			for (auto& c : cs) {
				cor->cells.push_back(c->pos);
				c->cor_i = cor_ix;
			}
			
			auto& en = cor->ents.emplace_back();
			en.pos = cs.front()->pos;
			en.room_i = r.index;
			g_rooms[en.room_i].cor_i.push_back(cor_ix);
		};
		
		int cor_perim = r.area.size().perimeter() / rm_cor_per;
		int cor_num = 1 + rnd.range_index (std::min (rm_cor_max, cor_perim));
		cor_num += rm_cor_add;
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
				if (c.is_border || c.cor_i != size_t_inval || c.room_i != size_t_inval)
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
				if (c.is_border || c.cor_i != size_t_inval || c.room_i != size_t_inval)
					break;
			}
			if (x != size.x) break;
			++y_rem;
		}
		
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
				if (c.is_border || c.cor_i != size_t_inval || c.room_i != size_t_inval)
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
				if (c.is_border || c.cor_i != size_t_inval || c.room_i != size_t_inval)
					break;
			}
			if (x != size.x) break;
			--y_rem;
		}
		
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
		t.cors.reserve( g_cors.size() );
		
		for (auto& r : g_rooms)
		{
			auto& nr = t.rooms.emplace_back();
			nr.area = r.area;
		}
		for (auto& c : cs)
		{
			auto& nc = t.cs.emplace_back();
			nc.is_wall = (c.cor_i == size_t_inval && c.room_i == size_t_inval);
		}
		for (auto& c : g_cors)
		{
			auto& nc = t.cors.emplace_back();
			nc.cells = c.cells;
			nc.ents.reserve( c.ents.size() );
			
			for (auto& e : c.ents)
			{
				auto& ne = nc.ents.emplace_back();
				ne.inner = e.pos;
				ne.room_index = e.room_i;
				ne.outer = ne.inner;
				
				const vec2i ds[] = {{0,1}, {0,-1}, {1,0}, {-1,0}};
				for (auto& d : ds) {
					if (getc(e.pos + d)->room_i == e.room_i) {
						ne.outer = e.pos + d;
						break;
					}
				}
				if (ne.inner == ne.outer)
					VLOGX("LevelTerrain::() invalid corridor");
			}
		}
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



void LevelTerrain::test_save(const char *prefix) const
{
	std::string fn_line = std::string(prefix) + "_line.png";
	std::string fn_grid = std::string(prefix) + "_grid.png";
	
	/// Bresenham line - no diagonal
	struct CLD_Line2 {
		CLD_Line2(vec2i p0, vec2i p1) {init(p0, p1);}
		void init(vec2i p0, vec2i p1) {
			x0 = p0.x, x1 = p1.x;
			y0 = p0.y, y1 = p1.y;
			dx = abs(x1 - x0);
			dy = abs(y1 - y0);
			sx = x0 < x1 ? 1 : -1;
			sy = y0 < y1 ? 1 : -1;
			e = dx - dy;
		}
		/// returns false on last step
		bool step(vec2i &p) {
			p = {x0, y0};
			if (x0 == x1 && y0 == y1) return false;
			int e2 = e*2;
			if (e2 + dy > dx - e2) {
				e -= dy;
				x0 += sx;
			}
			else {
				e += dx;
				y0 += sy;
			}
			return true;
		}
	private:
		int x0, y0, x1, y1;
		int dx, dy, sx, sy, e;
	};
	
	{
		ImageInfo img;
		img.reset(grid_size, ImageInfo::FMT_ALPHA);
		
		auto& ls = ls_wall;
		auto& gs = ls_grid;
		
		int cz = int_round(cell_size);
		img.reset(grid_size * cz, ImageInfo::FMT_ALPHA);
		
		for (auto& g : gs)
		{
			CLD_Line2 ln( g.first.int_round(), g.second.int_round() );
			while (true)
			{
				vec2i r;
				bool ok = ln.step(r);
				img.raw()[r.y * grid_size.x * cz + r.x] = 64;
				if (!ok) break;
			}
		}
		for (auto& c : ls)
		{
			vec2i prev = c.front().int_round();
			for (size_t i=1; i<c.size(); ++i)
			{
				auto p = c[i].int_round();
				
				CLD_Line2 ln(prev, p);
				while (true)
				{
					vec2i r;
					bool ok = ln.step(r);
					img.raw()[r.y * grid_size.x * cz + r.x] = 255;
					if (!ok) break;
				}
				
				prev = p;
			}
		}
		
		img.save(fn_line.c_str());
	}
	
	ImageInfo img;
	img.reset(grid_size, ImageInfo::FMT_ALPHA);
	
	for (int y=0; y < grid_size.y; ++y)
	for (int x=0; x < grid_size.x; ++x)
	{
		auto& c = cs[y * grid_size.x + x];
		
		size_t i=0;
		for (; i<rooms.size(); ++i) {
			auto& r = rooms[i].area;
			if (r.contains({x,y}) && x != r.upper().x && y != r.upper().y) break;
		}
		
		int v;
		if (0) {
			if (i != rooms.size()) v = c.is_wall? 64 : 255;
			else                   v = c.is_wall? 0 : 128;
		}
		else v = (!c.is_wall) * 255;
		
		img.raw()[y * grid_size.x + x] = v;
	}
	
	img.save(fn_grid.c_str());
}
