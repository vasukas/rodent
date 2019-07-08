#include "utils/noise.hpp"
#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_log.hpp"
#include "game_core.hpp"
#include "level_ctr.hpp"

#include "utils/res_image.hpp"
#include "vaslib/vas_time.hpp"

class LevelControl_Impl : public LevelControl
{
public:
	// room params
	const vec2i rm_sz_min = {2,2}; // room size
	const vec2i rm_sz_max = {11,7};
	const int rm_cor_per = 6; // perimeter cells for one cor
	const int rm_cor_max = 8; // max cor count
	
	// corridor params
	const int cor_len_min = 1; // min length
	const int cor_len_max = 6; // max length
	const float cor_rot_f = 0.3; // rotation chance on step
	const float cor_dup_f = 0.1; // duplicate corrdior chance
	const float cor_cnt_f = 0.5; // connection corridor chance
	
	// column params
	const int cln_per = 8; // max columns per area
	
	// precomputed
	vec2fp map_off;
	
	// variables
	std::vector<Room> g_rooms;
	std::vector<Corridor> g_cors;
	
	RandomGen& rnd;
	std::vector<Room*> place_q; // rooms to be yet placed
	
	
	
	vec2fp grid_to_world (vec2i p) {return cell_size * p - map_off;}
	Room*     get_room(size_t index) {return index == size_t_inval? nullptr : &g_rooms[index];}
	Corridor* get_corr(size_t index) {return index == size_t_inval? nullptr : &g_cors [index];}
	
	LevelControl_Impl(): rnd(GameCore::get().get_random())
	{
		map_off = cell_size * size() / 2;
		
		size_t rooms_total = size().area() / (rm_sz_min + (rm_sz_max - rm_sz_min)/2).area();
		size_t cors_total = rooms_total * rm_cor_max;
		VLOGI("LevelControl: {} rooms, {} corridors - {} MB", rooms_total, cors_total,
		      (rooms_total * sizeof(Room) + cors_total * sizeof(Corridor)) >> 20);
		
		g_rooms.reserve(rooms_total);
		g_cors.reserve(cors_total);
		
		TimeSpan time0 = TimeSpan::since_start();
		
		auto nr = &g_rooms.emplace_back();
		nr->index = 0;
		nr->area.sz = {3, 3};
		nr->area.off = size()/2 - nr->area.sz/2;
		mark_room(*nr);
		place_q.push_back(nr);
		
		size_t gener_i = 0;
		while (!place_q.empty())
		{
			std::vector<Room*> q;
			q.reserve(rm_cor_max);
			q.swap(place_q);
			
			for (auto& r : q) place_room(*r), r->gener = gener_i;
			++gener_i;
		}
		
		g_rooms.shrink_to_fit();
		g_cors.shrink_to_fit();
		
		VLOGI("Actually generated: {} rooms, {} corridors", g_rooms.size(), g_cors.size());
		VLOGI("Time elapsed: {:6.3} seconds", (TimeSpan::since_start() - time0).seconds());
		
		ImageInfo img;
		img.reset(size(), ImageInfo::FMT_RGB);
		for (auto& c : get_all_cs()) {
			int v = 0;
			if (auto r = get_room(c.room_i)) {
				v = 255;
				if (!r->index) v = 64;
			}
			else if (c.cor_i != size_t_inval) v = 128;
			img.raw()[c.pos.y * size().x + c.pos.x] = v;
		}
		img.save("test.png");
		exit(1);
		
// actually, generate stuff
		//
		//
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
		r.w_ctr = grid_to_world (r.area.lower() + r.area.size() / 2);
		
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
		
		auto gen_corridor = [&]()
		{
			if (g_cors.size() == g_cors.capacity()) return;
			
			size_t i = rnd.range_index(bord.size());
			auto& c0 = bord[i];
			
			{	vec2i p = c0.second; p.rot90cw(); p += c0.first->pos;
				if (cref(p).cor || cref(p).room) return;
				p = c0.second; p.rot90ccw(); p += c0.first->pos;
				if (cref(p).cor || cref(p).room) return;
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
					if (dir != c0.second) {
						if (rnd.flag()) dir.rot90cw();
						else dir.rot90ccw();
					}
					else dir = c0.second;
				}
				
				vec2i np = cs.back()->pos + dir;
				auto c = getc(np);
				if (!c) return;
				
				cs.push_back(c);
				if (c->cor || c->room) {
					if (rnd.range_n() > cor_cnt_f) return;
					break;
				}
				if (c->is_border) {
					Cell* n = nullptr;
					const vec2i ds[] = {{0,1}, {0,-1}, {1,0}, {-1,0}};
					for (auto& d : ds) {
						c = getc(np + d);
						if (c->cor || c->room) {
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
			
			if		(cs.back()->cor)
			{
				cor = cs.back()->cor;
				cs.pop_back();
			}
			else if (cs.back()->room)
			{
				for (auto& c : g_cors) {
					bool a = false, b = false;
					for (auto& e : c.ents) {
						if (e.room == &r)              a = true;
						if (e.room == cs.back()->room) b = true;
					}
					if (a && b && c.ents.size() == 2) {
						if (rnd.range_n() > cor_dup_f) return;
					}
				}
				
				cor = &g_cors.emplace_back();
				
				auto& en = cor->ents.emplace_back();
				en.pos = cs.back()->pos;
				en.room = cs.back()->room;
				
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
				    ar.off.x + safety + ar.sz.x > size().x ||
				    ar.off.y + safety + ar.sz.y > size().y
				    )
					return;
				
				for (int y=0; y<ar.sz.y; ++y)
				for (int x=0; x<ar.sz.x; ++x)
				{
					auto& c = cref(vec2i{x,y} + ar.off);
					if (c.is_border || c.cor || c.room) return;
				}
				
				auto nr = &g_rooms.emplace_back();
				nr->index = g_rooms.size() - 1;
				nr->area = ar;
				mark_room(*nr);
				place_q.push_back(nr);
				
				cor = &g_cors.emplace_back();
				auto& en = cor->ents.emplace_back();
				en.pos = cs.back()->pos;
				en.room = nr;
			}
			
			reserve_more(cor->cells, cs.size());
			for (auto& c : cs) {
				cor->cells.push_back(c->pos);
				c->cor = cor;
			}
			
			auto& en = cor->ents.emplace_back();
			en.pos = cs.front()->pos;
			en.room = &r;
		};
		
		int cor_perim = r.area.size().perimeter() / rm_cor_per;
		int cor_num = 1 + rnd.range_index (std::min (rm_cor_max, cor_perim));
		cor_num += 4;
		for (int i=0; i<cor_num; ++i) gen_corridor();
	}
};



const vec2fp LevelControl::cell_size = {8,8};
LevelControl::LevelControl()
{
	grid_size = (vec2fp(4000, 3000) / cell_size).int_round();
//	grid_size = {1366, 768};
	cs.resize(grid_size.area());
	
	for (int y=0; y<grid_size.y; ++y)
	for (int x=0; x<grid_size.x; ++x)
		cs[y * grid_size.x + x].pos = {x, y};
}
LevelControl::Cell* LevelControl::getc(vec2i pos)
{
	if (pos.x < 0 || pos.y < 0 || pos.x >= grid_size.x || pos.y >= grid_size.y) return nullptr;
	return &cs[pos.y * grid_size.x + pos.x];
}
LevelControl::Cell& LevelControl::cref(vec2i pos)
{
	if (auto c = getc(pos)) return *c;
	GAME_THROW("LevelControl::cref() failed");
}

static LevelControl* ctr;
LevelControl* LevelControl::init() {return ctr = new LevelControl_Impl;}
LevelControl& LevelControl::get() {return *ctr;}
LevelControl::~LevelControl() {ctr = nullptr;}
