#include "utils/image_utils.hpp"
#include "utils/noise.hpp"
#include "utils/path_search.hpp"
#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_log.hpp"
#include "vaslib/vas_time.hpp"
#include "vaslib/vas_types.hpp"
#include "common_defs.hpp"
#include "level_gen.hpp"

constexpr float cell_size = GameConst::cell_size;

struct Gen1
{
	struct RoomClass;
	struct RoomSizeType;
	
	// Note: corridor data (cells & neis) may be incorrect and is mostly ignored
	// search in file - "WARNING:" (though not everything is marked)
	
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
		
		const RoomSizeType* r_szt;
		const RoomClass* r_class = nullptr; // null at first
		
		int depth; // tmp
	};
	struct Cell
	{
		vec2i pos;
		std::optional<size_t> cor_i;
		std::optional<size_t> room_i;
		std::optional<size_t> room_was; ///< before placing islands etc
		bool is_door = false;
		
		bool is_border = false; // room border
		bool protect = false; // chance of NOT changing after initial gen
		LevelTerrain::StructureIndex structure = LevelTerrain::STR_NONE; // same as output
		
		bool isolated = true;
		int depth; // tmp
		int cry_gen;
	};
	
	
	
	enum RoomSizeTypeEnum
	{
		RSZ_SMALL,
		RSZ_MIDDLE,
		RSZ_BIG,
		RSZ_HUGE,
		
		RSZ_TOTAL_COUNT
	};
	
	struct RoomSizeType
	{
		RoomSizeTypeEnum type;
		
		float kch = 0; // chance (modified later)
		vec2i sz_min; // room size
		vec2i sz_max;
		
		// crystal params
		float cry_f = 0.5; // generation chance
		int cry_per = 12; // 1 origin per how much area
		int cry_max = 10; // max length
	};
	
	enum ObstacleType
	{
		OBS_NONE,
		OBS_CRYSTAL,
		OBS_CRYSTAL_CLEARED,
		OBS_COLUMN,
		OBS_RIBS,
		OBS_COLUMN_EDGE
	};
	
	struct RoomClass
	{
		float kch = 0; // chance (original)
		LevelTerrain::RoomType lc_type = LevelTerrain::RM_DEFAULT;
		uint32_t dbg_color = 0;
		
		ObstacleType obs_t = OBS_NONE;
		std::vector<std::pair<RoomSizeTypeEnum, float>> rsz_types; // and (normalized) chances
		float place_prio = 0; // placement priority (higher - earlier), NON-negative
		
		size_t rm_count_min = 0; // total count of rooms of such type
		size_t rm_count_max = size_t_inval;
		size_t rm_count = 0; // current
		
		// flags
		float k_has_doors = 0; // chance of having door
		float k_wall_protect = 0; // wall protect value
	};
	
	struct GenParams
	{
		// room params
		std::array<RoomSizeType, RSZ_TOTAL_COUNT> rm_szs;
		std::vector<RoomClass> rm_cs; // [0] must be RM_DEFAULT; sorted by priority
		
		int rm_cor_per = 4; // perimeter cells for one cor
		int rm_cor_max = 12; // max cor count
		int rm_cor_add = 3; // additional trys
		
		// corridor params
		int cor_len_min = 1; // min length
		int cor_len_max = 7; // max length
		float cor_rot_f = 0.2; // rotation chance on step
		float cor_dup_f = 0.5; // duplicate corrdior chance
		float cor_cnt_f = 0.8; // corridor interconnection chance
	};
	
	GenParams gp;
	
	
	
	//
	vec2i l_size;
	std::vector<Cell> cs;
	
	//
	std::vector<Room> g_rooms;
	std::vector<Corridor> g_cors;
	
	//
	RandomGen& rnd;
	std::vector<size_t> place_q; // rooms yet to be placed
	
	
	
	static GenParams gp_main()
	{
		GenParams gp;
		
		{
			auto& r = gp.rm_szs[RSZ_SMALL];
			r.type = RSZ_SMALL;
			r.kch = 0.5;
			r.sz_min = {3,3};
			r.sz_max = {6,5};
		}{
			auto& r = gp.rm_szs[RSZ_MIDDLE];
			r.type = RSZ_MIDDLE;
			r.kch = 1.5;
			r.sz_min = {5,5};
			r.sz_max = {12,10};
		}{
			auto& r = gp.rm_szs[RSZ_BIG];
			r.type = RSZ_BIG;
			r.kch = 1;
			r.sz_min = {15,13};
			r.sz_max = {20,18};
		}{
			auto& r = gp.rm_szs[RSZ_HUGE];
			r.type = RSZ_HUGE;
			r.kch = 0.5;
			r.sz_min = {23,19};
			r.sz_max = {28,24};
		}
		
		//
		
		{
			auto& r = gp.rm_cs.emplace_back();
			r.place_prio = 1000;
			// default
		}{
			auto& r = gp.rm_cs.emplace_back();
			r.lc_type = LevelTerrain::RM_CONNECT;
			r.dbg_color = -1;
			
			r.kch = 0.2;
			r.rsz_types = {{RSZ_SMALL, 0.8}, {RSZ_MIDDLE, 0.2}};
			
			r.k_wall_protect = 0.2;
		}{
			auto& r = gp.rm_cs.emplace_back();
			r.lc_type = LevelTerrain::RM_LIVING;
			// min assigned later
			r.dbg_color = 0xff8000;
			
			r.kch = 0.1;
			r.obs_t = OBS_CRYSTAL_CLEARED;
			r.rsz_types = {{RSZ_SMALL, 0.5}, {RSZ_MIDDLE, 0.5}};
			
			r.k_has_doors = 0.5;
			r.k_wall_protect = 0.4;
		}{
			auto& r = gp.rm_cs.emplace_back();
			r.lc_type = LevelTerrain::RM_WORKER;
			r.dbg_color = 0x808080;
			
			r.kch = 0.4;
			r.obs_t = OBS_CRYSTAL;
			r.rsz_types = {{RSZ_SMALL, 1}, {RSZ_MIDDLE, 0.7}, {RSZ_BIG, 0.2}, {RSZ_HUGE, 0.1}};
			
			r.k_has_doors = 0.3;
			r.k_wall_protect = 0.3;
		}{
			auto& r = gp.rm_cs.emplace_back();
			r.lc_type = LevelTerrain::RM_REPAIR;
			r.dbg_color = 0x0080ff;
			
			r.kch = 0.1;
			r.rsz_types = {{RSZ_SMALL, 1}};
			
			r.k_has_doors = 0.5;
			r.k_wall_protect = 0.8;
		}{
			auto& r = gp.rm_cs.emplace_back();
			r.lc_type = LevelTerrain::RM_FACTORY;
			r.rm_count_min = r.rm_count_max = 3;
			r.dbg_color = 0xa0a000;
			
			r.kch = 0.5;
			r.obs_t = OBS_COLUMN;
			r.rsz_types = {{RSZ_BIG, 0.2}, {RSZ_HUGE, 0.8}};
			
			r.k_wall_protect = 1;
			r.k_has_doors = 1;
			
			r.place_prio = 0.5;
		}{
			auto& r = gp.rm_cs.emplace_back();
			r.lc_type = LevelTerrain::RM_LAB;
			r.rm_count_min = 1;
			r.rm_count_max = 6;
			r.dbg_color = 0x00a060;
			
			r.kch = 0.2;
			r.obs_t = OBS_RIBS;
			r.rsz_types = {{RSZ_MIDDLE, 0.3}, {RSZ_BIG, 1}, {RSZ_HUGE, 0.4}};
			
			r.k_wall_protect = 1;
			r.k_has_doors = 1;
			
			r.place_prio = 0.5;
		}{
			auto& r = gp.rm_cs.emplace_back();
			r.lc_type = LevelTerrain::RM_STORAGE;
			r.rm_count_min = 1;
			r.rm_count_max = 16;
			r.dbg_color = 0x00a000;
			
			r.kch = 0.4;
			r.obs_t = OBS_COLUMN;
			r.rsz_types = {{RSZ_MIDDLE, 0.7}, {RSZ_BIG, 0.3}, {RSZ_HUGE, 0.1}};
			
			r.k_wall_protect = 0.9;
			r.k_has_doors = 0.7;
		}{
			auto& r = gp.rm_cs.emplace_back();
			r.lc_type = LevelTerrain::RM_KEY;
			// min-max assigned later
			r.dbg_color = 0xc000c0;
			
			r.kch = 0.1;
			r.rsz_types = {{RSZ_SMALL, 1}};
			
			r.k_wall_protect = 0.9;
			r.k_has_doors = 0.7;
			
			r.place_prio = 1;
		}{
			auto& r = gp.rm_cs.emplace_back();
			r.lc_type = LevelTerrain::RM_TERMINAL;
			r.rm_count_min = r.rm_count_max = 1;
			r.dbg_color = 0xc000c0;
			
			r.kch = 0.1;
			r.obs_t = OBS_COLUMN_EDGE;
			r.rsz_types = {{RSZ_HUGE, 1}};
			
			r.k_wall_protect = 1;
			r.k_has_doors = 1;
			
			r.place_prio = 1;
		}{
			auto& r = gp.rm_cs.emplace_back();
			r.lc_type = LevelTerrain::RM_TRANSIT;
			r.rm_count_min = 2; // +1 for room zero
			r.rm_count_max = 5;
			r.dbg_color = 0xc000c0;
			
			r.kch = 0.2;
			r.rsz_types = {{RSZ_SMALL, 1}};
			
			r.k_wall_protect = 1;
			r.k_has_doors = 2;
			
			r.place_prio = 0.8;
		}{
			auto& r = gp.rm_cs.emplace_back();
			r.lc_type = LevelTerrain::RM_ABANDON;
			r.dbg_color = 0xff0000;
			
			r.kch = 0.2;
			r.obs_t = OBS_CRYSTAL;
			r.rsz_types = {{RSZ_SMALL, 0.5}, {RSZ_MIDDLE, 0.5}};
			
			r.k_has_doors = 0.5;
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
		
		return gp;
	}
	static GenParams gp_postp(GenParams gp)
	{
		float s = 0;
		for (auto& r : gp.rm_szs) s += r.kch;
		
		float t = 0;
		for (auto& r : gp.rm_szs) {
			t += r.kch / s;
			r.kch = t;
		}
		
		//
		
		for (auto& r : gp.rm_cs)
		{
			float s = 0;
			for (auto& z : r.rsz_types) s += z.second;
			for (auto& z : r.rsz_types) z.second /= s;
		}
		
		std::sort( gp.rm_cs.begin(), gp.rm_cs.end(), [](auto& a, auto& b){ return a.place_prio > b.place_prio; } );
		
		return gp;
	}
	Gen1(RandomGen* rnd_in, vec2i size_in)
	    : gp(gp_postp(gp_main())), rnd(*rnd_in)
	{
		// set key rooms
		
		auto get_type = [&](auto type) -> RoomClass* {
			for (auto& r : gp.rm_cs)
				if (r.lc_type == type)
					return const_cast<RoomClass*>(&r);
			return nullptr;
		};
		
		size_t key_total = GameConst::total_key_count;
		size_t key_rooms = rnd.int_range( 3, key_total );
		
		get_type(LevelTerrain::RM_KEY)->rm_count_min = key_rooms;
		get_type(LevelTerrain::RM_KEY)->rm_count_max = key_rooms;
		get_type(LevelTerrain::RM_LIVING)->rm_count_min = key_total - key_rooms;
		
		VLOGI("LevelTerrain:: size_in {}x{}", size_in.x, size_in.y);
		
		// try loop
		
		size_t n = 0;
		while (!full_generate(size_in)) {
			++n;
			if (n == 20) throw std::runtime_error("LevelTerrain:: Too many retries (safeguard)");
		}
		VLOGI("LevelTerrain:: retries: {}", n);
	}
	bool full_generate(vec2i size_in)
	{
		// reset
		
		cs.clear();
		g_rooms.clear();
		g_cors.clear();
		place_q.clear();
		
		/// Square grid non-diagonal directions
		const std::array<vec2i, 4> sg_dirs{{{-1,0}, {1,0}, {0,-1}, {0,1}}};
		
		// setup
		
		l_size = size_in + gp.rm_szs.back().sz_max;
		
		cs.resize( l_size.area() );
		for (int y=0; y < l_size.y; ++y)
		for (int x=0; x < l_size.x; ++x)
			cs[y * l_size.x + x].pos = {x, y};
		
		// generate rooms and corridors
		
		auto nr = &g_rooms.emplace_back();
		nr->index = 0;
		nr->area.sz = {3, 3};
		nr->area.off = l_size/2 - nr->area.sz/2;
		nr->r_szt = &gp.rm_szs[0];
		nr->r_class = &gp.rm_cs[0];
		
		for (auto& r : gp.rm_cs) {
			if (r.lc_type == LevelTerrain::RM_TRANSIT) {
				nr->r_class = &r;
				break;
			}
		}
		
		nr->area.map_outer([this](vec2i p){ cref(p).protect = true; });
		mark_room(*nr);
		place_q.push_back(nr->index);
		
		while (!place_q.empty())
		{
			std::vector<size_t> q;
			q.reserve(gp.rm_cor_max);
			q.swap(place_q);
			
			for (auto& r : q) place_room( &g_rooms[r] );
		}
		
		VLOGI("LevelTerrain:: {} rooms, {} corridors", g_rooms.size(), g_cors.size());
		
		// increase connectivity (build additional corridors - more loops!)
		
		{	const int roomconn_maxlen = 7; // max path length between adjacent rooms, in room count
			const int corr_maxlen = 14; // max corridor length
			
			struct PotCorr { // potential corridor
				std::vector<Room*> tars;
				vec2i p0, p1; // ending points, inside room
			};
			
			std::vector<Room*> path_next, path_cur;
			path_next.reserve( g_rooms.size() );
			path_cur .reserve( g_rooms.size() );
			
			for (auto& r : g_rooms)
			{
				std::vector<PotCorr> pcos;
				pcos.reserve( r.area.size().perimeter() );
				
				auto side = [&](vec2i off, vec2i max, vec2i incr, vec2i dir)
				{
					auto ok = [&](vec2i add_off) {
					    auto& c = cref(off + add_off);
						return !c.room_i && !c.cor_i;
					};
					
					for (; off != max; off += incr)
					{
						if (!ok(dir) || !ok(dir + incr) || !ok(dir - incr))
							continue;
						
						PotCorr pc;
						pc.tars.reserve(16);
						
						vec2i p = off + dir;
						for (int len = 1; len < corr_maxlen; ++len, p += dir)
						{
							auto c = getc(p);
							if (!c) break;
							if (c->cor_i) {
								for (auto& e : g_cors[*c->cor_i].ents) {
									if (e.room_i)
										pc.tars.push_back(&g_rooms[*e.room_i]);
								}
								break;
							}
							if (c->room_i) {
								pc.tars.push_back(&g_rooms[*c->room_i]);
								break;
							}
						}
						if (pc.tars.empty()) continue;
						
						for (auto& ci : r.cor_i)
						for (auto& e : g_cors[ci].ents)
						for (auto& tar : pc.tars)
							if (tar->index == e.room_i)
								goto roomconn_side_fail;
									
						pc.p0 = off;
						pc.p1 = p;
						pcos.emplace_back( std::move(pc) );
						
						roomconn_side_fail:;
					}
				};
				
				vec2i ra0 = r.area.lower();
				vec2i ra1 = r.area.upper() - vec2i::one(1);
				side( {ra0.x, ra0.y}, {ra1.x, ra0.y}, {1, 0}, {0, -1} ); // x++
				side( {ra0.x, ra1.y}, {ra1.x, ra1.y}, {1, 0}, {0,  1} );
				side( {ra0.x, ra0.y}, {ra0.x, ra1.y}, {0, 1}, {-1, 0} ); // y++
				side( {ra1.x, ra0.y}, {ra1.x, ra1.y}, {0, 1}, { 1, 0} );
				
				//
				
				std::vector<Room*> all_tars;
				all_tars.reserve( pcos.size() * 4 ); // chosen by fair dice roll
				
				for (auto& pc : pcos) append(all_tars, pc.tars);
				all_tars.erase( std::unique(all_tars.begin(), all_tars.end()), all_tars.end() );
				
				rnd.shuffle(all_tars);
				for (auto& tar : all_tars)
				{
					for (auto& r : g_rooms) r.depth = g_rooms.size() + 1;
					r.depth = 0;
					path_cur = {&r};
					path_next.clear();
					
					int depth = 0;
					for (; depth < roomconn_maxlen; ++depth)
					{
						for (auto& r : path_cur)
						for (auto& ci : r->cor_i)
						for (auto& ne : g_cors[ci].ents)
						{
							if (!ne.room_i) continue;
							auto& nr = g_rooms[*ne.room_i];
							
							if (nr.depth > depth) {
								if (&nr == tar) goto roomconn_path_found;
								nr.depth = depth + 1;
								path_next.push_back(&nr);
							}
						}
						
						path_cur.swap(path_next);
						path_next.clear();
					}
					// path NOT found
					
					{
						auto tar_cs = std::partition( pcos.begin(), pcos.end(), [&](auto& p) {
							for (auto& t : p.tars) if (t == tar) return false;
							return true;
						});
						
						for (auto it = tar_cs; it != pcos.end(); ++it)
						{
							vec2i dir = (it->p0.x == it->p1.x)
							            ? vec2i(0, it->p0.y < it->p1.y ? 1 : -1)
							            : vec2i(it->p0.x < it->p1.x ? 1 : -1, 0);
							
							size_t ci = g_cors.size();
							auto& cs = g_cors.emplace_back();
							cs.ents.push_back({ it->p0 + dir, r.index });
							cs.ents.push_back({ it->p1 - dir, tar->index });
							
							for (vec2i p = it->p0; p != it->p1; p += dir)
								cref(p).cor_i = ci;
						}
						
						pcos.erase( tar_cs, pcos.end() );
					}
					
					roomconn_path_found:;
				}
			}
		}
		
		// get shuffled rooms (except first)
		
		std::vector<Room*> rnd_rooms;
		rnd_rooms.reserve( g_rooms.size() - 1 );
		
		for (size_t i = 0; i < g_rooms.size() - 1; ++i)
			rnd_rooms.push_back(&g_rooms[i + 1]);
		
		rnd.shuffle(rnd_rooms);
		
		// assign room type - helpers
		
		auto set_room_type = [&](Room& r, RoomClass& rc)
		{
			rc.rm_count ++;
			r.r_class = &rc;
			
			if (rc.k_wall_protect > 0.01)
			{
				r.area.map_outer([&](vec2i p) {
					auto& flag = cref(p).protect;
					if (!flag) flag = rnd.range_n() < rc.k_wall_protect;
				});
			}
		};
		
		auto room_typesize_kch = [&](Room& r, const RoomClass& rc) -> std::optional<float>
		{
			for (auto& z : rc.rsz_types) {
				if (z.first == r.r_szt->type)
					return z.second;
			}
			return {};
		};
		
		// assign room type - neccessary
		
		for (auto& rc : gp.rm_cs)
		{
			while (rc.rm_count < rc.rm_count_min)
			{
				bool any = false;
				for (auto r : rnd_rooms)
				{
					if (r->r_class) continue;
					
					float zch;
					if (auto t = room_typesize_kch(*r, rc)) zch = *t;
					else continue;
					
					any = true;
					if (rnd.range_n() >= zch) continue;
					
					set_room_type(*r, rc);
					if (rc.rm_count >= rc.rm_count_min) break;
				}
				if (!any) return false;
			}
		}
		
		// assign room type - others
		
		std::vector<std::pair<float, RoomClass*>> r_rc_kchs; // just array
		r_rc_kchs.resize( gp.rm_cs.size() );
		
		for (auto r : rnd_rooms)
		{
			if (r->r_class) continue;
			
			// get types
			
			auto& r_ks = r_rc_kchs;
			size_t r_kn = 0;
			bool any = false;
			
			for (auto& rc : gp.rm_cs)
			{
				if (rc.rm_count >= rc.rm_count_max || rc.kch < 1e-5) continue;
				
				float zch;
				if (auto t = room_typesize_kch(*r, rc)) zch = *t;
				else continue;
				
				any = true;
				if (rnd.range_n() >= zch) continue;
				
				r_ks[r_kn] = {rc.kch, &rc};
				++r_kn;
			}
			
			if (!r_kn && any) // if random failed
			{
				RoomClass* b_ptr = nullptr;
				float b_prio = -1;
				
				for (auto& rc : gp.rm_cs)
				{
					if (rc.rm_count >= rc.rm_count_max || rc.kch < 1e-5) continue;
					if (!room_typesize_kch(*r, rc)) continue;
					
					if (b_prio < rc.place_prio || (b_prio - 0.01 < rc.place_prio && rnd.flag()))
					{
						b_ptr = &rc;
						b_prio = rc.place_prio;
					}
				}
				
				if (b_ptr) {
					r_ks[r_kn] = {b_ptr->kch, b_ptr};
					r_kn = 1;
				}
			}
			if (!r_kn) // can't place any room
			{
				set_room_type(*r, gp.rm_cs[0]);
				continue;
			}
			
			// get rnd
			
			float sum = 0;
			for (size_t i=0; i<r_kn; ++i) sum += r_ks[i].first;
			
			float rc_kch = rnd.range_n() * sum;
			float t = 0;
			
			size_t i = 0;
			for (; i < r_kn - 1; ++i)
			{
				t += r_ks[i].first;
				if (rc_kch < t) break;
			}
			
			set_room_type(*r, *r_ks[i].second);
		}
		
		// set room_was
		
		for (auto& r : g_rooms)
			r.area.map([&](auto p){ cref(p).room_was = r.index; });
		
		// protect room entries
		
		for (auto& r : g_rooms)
		{
			r.area.map_outer([&](vec2i p)
			{
				for (auto& d : sg_dirs)
				{
					auto& c = cref(p + d);
					if (c.room_i) c.protect = true;
				}
			});
		}
		
		// init path search
		
		std::unique_ptr<PathSearch> aps;
		aps.reset( PathSearch::create() );
		{
			std::vector<uint8_t> g( l_size.area() );
			for (size_t i=0; i < cs.size(); ++i)
			{
				bool is_wall = !cs[i].cor_i && !cs[i].room_i;
				g[i] = is_wall ? 0 : 1;
			}
			aps->update( l_size, std::move(g) );
		}
		
		// generate obstacles
		
		std::vector<vec2i> cry_add_next;
		std::vector<vec2i> cry_add_cur;
		
		for (auto& r : g_rooms)
		{	
			bool do_gen;
			float prot_kch;
			
			switch (r.r_class->obs_t)
			{
			case OBS_CRYSTAL_CLEARED:
				prot_kch = 0;
				do_gen = true;
				break;
				
			case OBS_CRYSTAL:
				prot_kch = 1;
				do_gen = true;
				break;
				
			default:
				do_gen = false;
				break;
			}
			if (!do_gen) continue;
			
			//
			
			auto& rc = *r.r_szt;
			int orig_num = r.area.size().area() / rc.cry_per;
			if (!orig_num) continue;
			
			//
			
			int area_prot = 0;
			
			r.area.map([&](auto p)
			{
				auto& c = cref(p);
				if (c.protect) ++area_prot;
				else cref(p).cry_gen = 0;
			});
			
			for (size_t i=0;   i < r.cor_i.size(); ++i)
			for (size_t j=i+1; j < r.cor_i.size(); ++j)
			{
				auto set = [&](vec2i p)
				{
					auto& c = cref(p);
					if (c.room_i == r.index && !c.cry_gen)
					{
						c.cry_gen = 1;
						++area_prot;
					}
				};
				
				auto gc = [&](size_t ix) {return g_cors[ r.cor_i[ix] ].cells.front();};
				auto res = aps->find_path({ gc(i), gc(j) });
				
				for (auto& p : res.ps)
					set(p);
				
				for (size_t i=1; i < res.ps.size(); ++i)
				{
					auto p0 = res.ps[i-1];
					auto p1 = res.ps[i];
					
					if (p0.x != p1.x && p0.y != p1.y)
						set({p0.x, p1.y});
				}
			}
			
			orig_num = std::min(orig_num, r.area.sz.area() - area_prot);
			if (orig_num <= 0) continue;
			
			//
			
			cry_add_next.reserve( r.area.sz.area() - area_prot );
			r.area.map([&](vec2i p)
			{
				auto& c = cref(p);
				if (!c.protect && !c.cry_gen)
					cry_add_next.push_back(p);
			});
			rnd.shuffle(cry_add_next);
			
			cry_add_next.resize( orig_num );
			
			//
			
			auto nsum = [&](vec2i p) {
				int s = 0;
				for (auto& d : sg_dirs) s += cref(p + d).cry_gen;
				return s;
			};
			
			int gen = 1;
			while (!cry_add_next.empty())
			{
				cry_add_cur.clear();
				cry_add_cur.swap( cry_add_next );
				
				for (auto& p0 : cry_add_cur)
				{
					auto& c = cref(p0);
					if (c.cry_gen) continue;
					
					c.cry_gen = gen;
					c.protect = rnd.range_n() < prot_kch;
					c.room_i.reset();
				}
	
				for (auto& p0 : cry_add_cur)
				for (auto& d : sg_dirs)
				{
					vec2i p = p0 + d;
					auto& c = cref(p);
					if (c.room_i != r.index || c.protect || c.cry_gen) continue;
	
					float t = nsum(p);
					t = 1.f - t / rc.cry_max;
	
					if (rnd.range_n() < t * rc.cry_f)
						cry_add_next.push_back(p);
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
					if (!c.protect) c.room_i = c.room_was;
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
		
		for (int y = 2; y < l_size.y - 3; ++y)
		for (int x = 1; x < l_size.x - 1; ++x)
		{
			auto iswall = [&](int x, int y) {
				auto& c = cref(vec2i{x, y});
				return !c.room_i && !c.cor_i;
			};
			
			std::optional<int> y1;
			int x1 = x;
			for (; x1 < l_size.x - 1; ++x1)
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
				if (c.protect) continue;
				if (c.room_was) c.room_i = c.room_was;
				else c.cor_i = cref({x, y}).cor_i;
			}
		}
		
		for (int x = 2; x < l_size.x - 3; ++x)
		for (int y = 1; y < l_size.y - 1; ++y)
		{
			auto iswall = [&](int x, int y) {
				auto& c = cref(vec2i{x, y});
				return !c.room_i && !c.cor_i;
			};
			
			std::optional<int> x1;
			int y1 = y;
			for (; y1 < l_size.y - 1; ++y1)
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
				if (c.protect) continue;
				if (c.room_was) c.room_i = c.room_was;
				else c.cor_i = cref({x, y}).cor_i;
			}
		}
		
		// remove 1-wide diags
		
		for (int y=1; y < l_size.y - 2; ++y)
		for (int x=1; x < l_size.x - 2; ++x)
		{
			auto iswall = [&](int ox, int oy) {
				auto& c = cref({x + ox, y + oy});
				return !c.room_i && !c.cor_i;
			};
			auto corr = [&](int ox, int oy) {
				auto& c = cref({x + ox, y + oy});
				if (c.room_was) c.room_i = c.room_was;
				else {
					if (c.protect) return;
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
		
		// generate doors
		
		for (auto& r : g_rooms)
		{
			if (r.r_class->k_has_doors < 1e-5) continue;
			
			auto f = [&](vec2i p, vec2i into, vec2i next)
			{
				bool any = false;
				int i = 0;
				for (; i < 3; ++i)
				{
					auto& cc = cref(p + i*next);
					auto& rc = cref(p + i*next + into);
					if (cc.is_door || !cc.cor_i || !rc.room_i) break;
					
					auto& oc = cref(p + i*next - into);
					any |= (oc.room_i || oc.cor_i);
				}
				
				if (!i || !any) return;
				bool fix_wide = r.r_class->k_has_doors > 0.9;
				
				if (cref(p -   next).cor_i) {
					if (fix_wide) cref(p -   next).cor_i.reset();
					else return;
				}
				if (cref(p + i*next).cor_i) {
					if (fix_wide) cref(p + i*next).cor_i.reset();
					else return;
				}
				if (rnd.range_n() >= r.r_class->k_has_doors) return;
				
				for (; i >= 0; --i)
				{
					auto& cc = cref(p + i*next);
					auto& rc = cref(p + i*next + into);
					
					cc.is_door = true;
					cc.protect = true;
					rc.protect = true;
				}
			};
			
			for (int y = r.area.lower().y; y < r.area.upper().y; ++y)
			{
				f({ r.area.lower().x - 1, y }, { 1, 0}, {0, 1});
				f({ r.area.upper().x,     y }, {-1, 0}, {0, 1});
			}
			for (int x = r.area.lower().x; x < r.area.upper().x; ++x)
			{
				f({ x, r.area.lower().y - 1 }, {0,  1}, {1, 0});
				f({ x, r.area.upper().y     }, {0, -1}, {1, 0});
			}
		}
		
		// gen structures
		
		auto obs_gen_ribs = [&](Room& r)
		{
			int w_off; // offset from walls
			int c_len; // claw length (x)
			int c_wid; // claw width (y)
			
			int c_len_max = 6;
			int c_wid_max = 6;
			
			switch (r.r_szt->type)
			{
			case RSZ_SMALL:
			case RSZ_MIDDLE:
				w_off = 2;
				c_len = 2;
				c_wid = 3;
				break;
				
			case RSZ_BIG:
				w_off = 2;
				c_len = 4;
				c_wid = 4;
				break;
				
			case RSZ_HUGE:
			case RSZ_TOTAL_COUNT: // suppress warning
				w_off = 3;
				c_len = 5;
				c_wid = 3;
				break;
			}
			
			int x_left = r.area.sz.x - (c_len*2 + w_off*3);
			int y_left = r.area.sz.y - (c_wid*2 + w_off*3);
			bool failed = (x_left < 0 || y_left < 0);
			
			auto set = [&](vec2i p)
			{
				auto& c = cref(r.area.off + p);
				if (!c.protect) {
					c.room_i.reset();
					c.protect = true;
				}
			};
			auto mirror = [&](vec2i off, auto f)
			{
				int x0 = off.x; int x1 = r.area.sz.x - 1 - off.x;
				int y0 = off.y; int y1 = r.area.sz.y - 1 - off.y;
				
				f({ x0, y0 });
				f({ x1, y0 });
				f({ x0, y1 });
				f({ x1, y1 });
			};
			auto mirror_set = [&](vec2i off)
			{
				mirror(off, set);
			};
			auto claw = [&](vec2i off, vec2i size)
			{
				size.x = std::min(size.x, c_len_max);
				size.y = std::min(size.y, c_wid_max);
				
				if (failed) {
					for (int x=0; x < size.x; ++x) {
						mirror_set(off + vec2i(x, size.y / 2));
					}
					return;
				}
				
				for (int x=0; x < size.x; ++x)
				{
					mirror_set(off + vec2i(x, 0));
					mirror_set(off + vec2i(x, size.y - 1));
				}
				for (int y=1; y < size.y - 1; ++y)
				{
					mirror(off + vec2i(0, y), [&](vec2i p){ cref( r.area.off + p ).structure = LevelTerrain::STR_RIB_OUT; });
					mirror_set(off + vec2i(1, y));
					mirror(off + vec2i(2, y), [&](vec2i p){ cref( r.area.off + p ).structure = LevelTerrain::STR_RIB_IN; });
				}
			};
			
			if (x_left >= (w_off + 1)*2)
				x_left -= (w_off + 1)*2;
			
			claw({ w_off, w_off }, vec2i(c_len + x_left /2, c_wid + y_left /2));
			return true;
		};
		
		auto obs_gen_cols = [&](Room& r, bool is_edge_type)
		{
			// 1 + space between columns
			int x_spc, y_spc;
			
			switch (r.r_szt->type)
			{
			case RSZ_SMALL:
				x_spc = y_spc = 10000;
				break;
				
			case RSZ_MIDDLE:
				x_spc = 1 + 3;
				y_spc = 1 + 2;
				break;
				
			case RSZ_BIG:
			case RSZ_HUGE:
			case RSZ_TOTAL_COUNT: // suppress warning
				x_spc = 1 + 4;
				y_spc = 1 + 3;
				if (is_edge_type) {
					x_spc -= 2;
					y_spc -= 1;
				}
				break;
			}
			
			int xn = r.area.sz.x / x_spc;
			int yn = r.area.sz.y / y_spc;
			if (!xn || !yn) return false;
			
			if (is_edge_type) {
				xn = std::min(xn, 4);
				yn = std::min(yn, 3);
			}
			
			if (xn > 1 && r.area.sz.x < x_spc*2 + 1) xn = 1;
			if (yn > 1 && r.area.sz.y < y_spc*2 + 1) yn = 1;
			
			int x_ctr = r.area.sz.x / 2;
			if ((xn & 1) && !(r.area.sz.x & 1))
			{
				x_ctr -= x_spc/2;
				--xn;
			}
			else if (!(xn & 1) && !(r.area.sz.x & 1))
			{
				x_ctr -= x_spc/2;
			}
			
			int y_ctr = r.area.sz.y / 2;
			if ((yn & 1) && !(r.area.sz.y & 1))
			{
				y_ctr -= y_spc/2;
				--yn;
			}
			else if (!(yn & 1) && !(r.area.sz.y & 1))
			{
				y_ctr -= y_spc/2;
			}
			
			if (!(xn & 1)) x_ctr -= x_spc/2;
			if (!(yn & 1)) y_ctr -= y_spc/2;
			
			//
			
			auto set = [&](vec2i p)
			{
				auto& c = cref(r.area.off + p);
				if (!c.protect) {
					c.room_i.reset();
					c.protect = true;
					c.structure = LevelTerrain::STR_COLUMN;
				}
			};
			
			for (int yc = 0; yc <= yn /2; ++yc)
			for (int xc = 0; xc <= xn /2; ++xc)
			{
				int x0, y0;
				if (!is_edge_type) {
					x0 = x_ctr - x_spc*xc;
					y0 = y_ctr - y_spc*yc;
				}
				else {
					x0 = x_spc * (xc + 1);
					y0 = y_spc * (yc + 1);
				}
				int x1 = r.area.sz.x - 1 - x0;
				int y1 = r.area.sz.y - 1 - y0;
				
				set({ x0, y0 });
				set({ x1, y0 });
				set({ x0, y1 });
				set({ x1, y1 });
			}
			
			return true;
		};
		
		for (auto& r : g_rooms)
		{
			if		(r.r_class->obs_t == OBS_RIBS) obs_gen_ribs(r);
			else if (r.r_class->obs_t == OBS_COLUMN) obs_gen_cols(r, false);
			else if (r.r_class->obs_t == OBS_COLUMN_EDGE) obs_gen_cols(r, true);
		}
		
		// set isolated
		
		std::vector<vec2i> isol_open;
		std::vector<vec2i> isol_next;
		isol_open.reserve( l_size.area() );
		isol_next.reserve( l_size.area() );
		
		g_rooms[0].area.map([&](vec2i p){ isol_open.push_back(p); });
		for (auto& c : cs) c.depth = (c.cor_i || c.room_i) ? l_size.area() : -1;
		
		int depth = 0;
		while (!isol_open.empty())
		{
			isol_next.clear();
			
			for (auto& p : isol_open)
			{
				auto& c = cref(p);
				if (c.depth > depth)
				{
					c.depth = depth;
					c.isolated = false;
					
					for (auto& d : sg_dirs)
						isol_next.push_back(p + d);
				}
			}
			
			isol_open.swap(isol_next);
		}
		
		// delete isolated rooms
		// WARNING: Only room indices in CELLS are rearranged
		
		for (auto it = g_rooms.begin(); it != g_rooms.end(); )
		{
			if (!it->area.map_check([&](vec2i p){ return cref(p).isolated; }))
			{
				++it;
				continue;
			}
			// TODO: rearrange indices
			else
			{
				size_t i = it - g_rooms.begin();
				it = g_rooms.erase(it);

				for (auto& c : cs) {
					if (!c.room_i || *c.room_i < i) continue;
					if (*c.room_i == i) c.room_i.reset();
					else --*c.room_i;
				}
			}
		}
		
		// remove doors in the walls
		
		for (auto& c : cs) {
			if ((!c.cor_i && !c.room_i) || c.isolated)
				c.is_door = false;
		}
		
		// fin
		
		rm_diags();
		
//		g_rooms.shrink_to_fit();
//		g_cors.shrink_to_fit();
	
		VLOGI("LevelTerrain:: remains {} rooms, {} corridors", g_rooms.size(), g_cors.size());
		return true;
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
		for (int y=1; y<r->area.sz.y -1; ++y) { // 1 offset to remove corner corridors
			mark_border({-1,y}, {-1,0});
			mark_border({r->area.sz.x, y}, {1,0});
		}
		for (int x=1; x<r->area.sz.x -1; ++x) {
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
			
			int len = rnd.range_index (gp.cor_len_max, gp.cor_len_min);
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
				
				const RoomSizeType* rc = nullptr;
				float rc_kch = rnd.range_n();
				for (auto& r : gp.rm_szs)
				{
					if (rc_kch < r.kch) {
						rc = &r;
						break;
					}
				}
				if (!rc) rc = &gp.rm_szs.back();
				
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
				    ar.off.x + safety + ar.sz.x > l_size.x ||
				    ar.off.y + safety + ar.sz.y > l_size.y
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
				nr->r_szt = rc;
				mark_room(*nr);
				place_q.push_back(nr->index);
				
				cor_ix = g_cors.size();
				cor = &g_cors.emplace_back();
				nr->cor_i.push_back(cor_ix);
				
				auto& en = cor->ents.emplace_back();
				en.pos = cs.back()->pos;
				en.room_i = nr->index;
			}
			if (cs.empty()) return;
			
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
		if (pos.x < 0 || pos.y < 0 || pos.x >= l_size.x || pos.y >= l_size.y) return nullptr;
		return &cs[pos.y * l_size.x + pos.x];
	}
	Cell& cref(vec2i pos)
	{
		if (auto c = getc(pos)) return *c;
		LOG_THROW("LevelTerrain::cref() failed");
	}
	void remove_border()
	{
		vec2i& size = l_size;
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
		
		VLOGI("LevelTerrain:: size reduced {}x{} -> {}x{}", orig_size.x, orig_size.y, size.x, size.y);
	}
	void convert(LevelTerrain& t)
	{
		remove_border();
		
		t.grid_size = l_size;
		t.cs.reserve( cs.size() );
		t.rooms.reserve( g_rooms.size() );
		
		for (auto& r : g_rooms)
		{
			auto& nr = t.rooms.emplace_back();
			nr.area = r.area;
			nr.type = r.r_class->lc_type;
			nr.dbg_color = r.r_class->dbg_color;
		}
		
		for (auto& c : cs)
		{
			auto& nc = t.cs.emplace_back();
			nc.is_wall = (!c.cor_i && !c.room_i);
			nc.is_door = !nc.is_wall && c.is_door;
			nc.structure = c.structure;
			
			nc.isolated = !nc.is_wall && c.isolated;
			if (nc.isolated) nc.is_wall = true;
			
			if (c.room_i)
				nc.room = &t.rooms[*c.room_i];
		}
	}
};



LevelTerrain* LevelTerrain::generate(const GenParams& pars)
{
	auto lt_ptr = std::make_unique<LevelTerrain>();
	LevelTerrain& lt = *lt_ptr;
	
	TimeSpan t0 = TimeSpan::since_start();
	Gen1( pars.rnd, pars.grid_size ).convert( lt );
	lt.find_corridors();
	
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
		
		VLOGD("LevelTerrain:: total:      {:.3f} seconds", (t3 - t0).seconds());
		VLOGD("               generation: {:.3f} seconds", (t1 - t0).seconds());
		VLOGD("               vectorize:  {:.3f} seconds, {} chains, {} loops, {} points",
		      (t2 - t1).seconds(), lt.ls_wall.size(), lps, pts);
		VLOGD("               gen_grid:   {:.3f} seconds, {} lines", (t3 - t2).seconds(), lt.ls_grid.size());
	}
	
	return lt_ptr.release();
}



void LevelTerrain::find_corridors()
{
	const std::array<vec2i, 4> sg_dirs{{{-1,0}, {1,0}, {0,-1}, {0,1}}};
	std::vector<vec2i> open, next;
	
	auto ce = [&](vec2i at) -> Cell*
	{
		if (Rect{{}, grid_size, true}.contains_le(at))
			return &cs[at.x + at.y * grid_size.x];
		return nullptr;
	};
	
	for (auto& c : cs)
		c.tmp = -1;
	
	for (size_t i=0; i < cs.size(); ++i)
	{
		{	auto& c = cs[i];
			if (c.is_wall || c.room)
				continue;
		}
		auto& corr = corrs.emplace_back();
		
		{	int x = i % grid_size.x;
			int y = i / grid_size.x;
			open.push_back({ x, y });
		}
		cs[i].tmp = i;
		
		while (!open.empty())
		{
			for (auto& p : open)
			{
				corr.cs.push_back(p);
				
				for (auto& d : sg_dirs)
				{
					auto c = ce(p + d);
					if (!c) continue;
					
					if (c->tmp == int(i)) continue;
					c->tmp = i;
					
					if (!c->is_wall) {
						if (c->room) corr.rs.push_back(std::distance( rooms.data(), c->room ));
						else next.push_back(p + d);
					}
				}
			}
			
			open.swap(next);
			next.clear();
		}
		
		auto& rs = corr.rs;
		auto it = std::unique( rs.begin(), rs.end() );
		rs.erase( it, rs.end() );
	}
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



ImageInfo LevelTerrain::draw_grid(bool is_debug) const
{
	ImageInfo img;
	ImagePointBrush br;
	
	auto& ls = ls_wall;
	auto& gs = ls_grid;
	
	int cz = int_round(cell_size);
	img.reset(grid_size * cz, is_debug? ImageInfo::FMT_RGB : ImageInfo::FMT_ALPHA);
	
	if (is_debug)
	{
		for (auto& r : rooms)
		{
			br.clr = r.dbg_color;
			fill_rect(img, {r.area.lower() * cz, r.area.size() * cz, true}, br);
			
			br.clr = 0;
			r.area.map([&](auto p)
			{
				auto& c = cs[p.y * grid_size.x + p.x];
				if (c.is_wall || c.isolated)
				{
					for (int i=0; i<cz; ++i)
					for (int j=0; j<cz; ++j)
						br.apply(img, p*cz + vec2i(j, i));
				}
			});
		}
		
		br.clr = 0xffff00;
		for (int y=0; y < grid_size.y; ++y)
		for (int x=0; x < grid_size.x; ++x)
		{
			auto& c = cs[y * grid_size.x + x];
			if (!c.is_door) continue;
			
			vec2i p = {x,y};
			for (int i=0; i<cz; ++i)
			for (int j=0; j<cz; ++j)
				br.apply(img, p*cz + vec2i(j, i));
		}
	}
	
	br.clr = is_debug? 0x000080 : 64;
	for (auto& g : gs)
		draw_line(img, g.first.int_round(), g.second.int_round(), br);
	
	br.clr = is_debug? 0xc0c0c0 : 255;
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
void LevelTerrain::debug_save(const char *prefix, bool img_line, bool img_grid) const
{
	std::string fn_line = std::string(prefix) + "_line.png";
	std::string fn_grid = std::string(prefix) + "_grid.png";
	
	if (img_line)
		draw_grid(true).save(fn_line.c_str());
	
	if (!img_grid) return;
	
	ImageInfo img;
	img.reset(grid_size, ImageInfo::FMT_RGB);
	
	for (int y=0; y < grid_size.y; ++y)
	for (int x=0; x < grid_size.x; ++x)
	{
		auto& c = cs[y * grid_size.x + x];
		uint32_t v;
		
		if (c.isolated) v = 0x802020;
		else {
			auto r = std::find_if(rooms.begin(), rooms.end(), [&](auto& v){ return v.area.contains_le({x,y}); });
			if (r == rooms.end()) {
				if (c.is_door) v = 0x00ffff;
				else v = c.is_wall ? 0 : 0x909090;
			}
			else if (c.is_wall) v = 0x404040;
			else v = 0xffffff;
		}
		
		img.set_pixel_fast({x, y}, v);
	}
	
	img.save(fn_grid.c_str());
}
