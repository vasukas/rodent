#include "game/game_core.hpp"
#include "game/player_mgr.hpp"
#include "utils/noise.hpp"
#include "ai_algo.hpp"
#include "ai_drone.hpp"

// debug output
#include "client/presenter.hpp"



void room_flood(GameCore& core, vec2i pos, int max_depth, bool random_dirs, callable_ref<bool(const LevelControl::Room&, int)> f)
{
	if (!max_depth) return;
	
	auto& lc = core.get_lc();
	auto& rnd = core.get_random();
	std::vector<LevelControl::Room*> open, next, closed;
	std::vector<size_t> i_tmp;
	
	open.reserve(32);
	next.reserve(32);
	closed.reserve(64);
	
	int depth = 0;
	lc.rooms_reset_tmp( std::numeric_limits<int>::max() );
	
	auto room0 = &lc.ref_room(lc.cref(pos).room_nearest);
	room0->tmp = 0;
	open.push_back(room0);
	
	while (!open.empty() && depth < max_depth)
	{
		for (auto& rm : open)
		{
			if (closed.end() == std::find( closed.begin(), closed.end(), rm ))
			{
				if (!f( *rm, depth )) return;
				closed.push_back(rm);
			}
			if (depth == max_depth - 1) continue;
			
			int cost = rm->tmp + rm->ai_radio_cost;
			if (cost >= max_depth) continue;
			
			auto foo = [&](auto& is)
			{
				for (auto& i : is)
				{
					auto nr = &lc.ref_room(i);
					if (nr->tmp > cost)
					{
						nr->tmp = cost;
						next.push_back(nr);
					}
				}
			};
			if (!random_dirs) foo(rm->neis);
			else {
				i_tmp = rm->neis;
				rnd.shuffle(i_tmp);
				foo(i_tmp);
			}
		}
		
		open.swap(next);
		next.clear();
		++depth;
	}
}
void room_flood_p(GameCore& core, vec2fp pos, int max_depth, bool random_dirs, callable_ref<bool(const LevelControl::Room&, int)> f)
{
	auto p = core.get_lc().to_cell_coord(pos);
	room_flood(core, p, max_depth, random_dirs, std::move(f));
}
void room_query(GameCore& core, const LevelControl::Room& rm, callable_ref<bool(AI_Drone&)> f)
{
	auto ai_area = core.get_pmg().get_ai_rects().second;
	
	Rectfp r = rm.area.to_fp(core.get_lc().cell_size);
	if (!r.overlaps(ai_area)) return;
	
	core.get_phy().query_aabb(r, [&](auto& ent, auto& fix)
	{
		if (fix.IsSensor()) return true;
		if (auto d = ent.get_ai_drone(); d && d->is_online) return f(*d);
		return true;
	});
}
void area_query(GameCore& core, vec2fp ctr, float radius, callable_ref<bool(AI_Drone&)> f)
{
	core.get_phy().query_circle_all(conv(ctr), radius, [&](auto& ent, auto&){
		return f(*ent.get_ai_drone());
	},
	[&](auto& ent, auto& fix)
	{
		if (fix.IsSensor()) return false;
		if (auto d = ent.get_ai_drone(); d && d->is_online) return true;
		return false;
	});
}

std::vector<std::vector<vec2fp>> calc_search_rings(GameCore& core, vec2fp ctr_pos)
{
	const vec2i dirs[4] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
	auto& ring_dist = AI_Const::search_ring_dist;
	auto& lc = core.get_lc();
	
	Rect grid_area = Rect::from_center_le( lc.to_cell_coord(ctr_pos), vec2i::one(ring_dist.back() + 1) );
	grid_area = calc_intersection(grid_area, Rect{{}, lc.get_size(), true});
	
	std::vector<uint8_t> cs;
	cs.resize( grid_area.size().area() );
	
	vec2i off = grid_area.lower();
	auto getc = [&](vec2i p) -> auto& {
		p -= off;
		return cs[p.y * grid_area.size().x + p.x];
	};
	
	std::vector<vec2i> free_nodes;
	free_nodes.emplace_back( lc.to_cell_coord(ctr_pos) );
	getc(free_nodes.back()) = 1;
	
	std::vector<std::vector<vec2fp>> rings;
	rings.reserve( ring_dist.size() );
	
	for (int step = 1; step <= ring_dist.back() && !free_nodes.empty(); ++step)
	{
		auto nodes = std::move(free_nodes);
		free_nodes.reserve( 2 * M_PI * (step + 1) );
		
		bool add_ring = false;
		for (auto& d : ring_dist) {
			if (step == d) {
				add_ring = true;
				break;
			}
		}
		if (add_ring) {
			rings.emplace_back();
			rings.back().reserve( 2 * M_PI * (step + 1) );
		}
		
		for (auto& n : nodes)
		{
			for (auto& d : dirs)
			{
				vec2i p = n + d;
				if (!grid_area.contains_le(p)) continue;
				
				auto& c = getc(p);
				if (!c) {
					c = 1;
					if (!lc.cref(p).is_wall)
					{
						if (add_ring)
							rings.back().emplace_back( vec2fp(p) * lc.cell_size + vec2fp::one(lc.cell_size / 2) );
							
						free_nodes.emplace_back(p);
					}
				}
			}
		}
	}
	
	if (!rings.empty() && rings.back().empty()) rings.pop_back();
	return rings;
}



class AI_AOS_Impl : public AI_AOS
{
public:
	struct DroneInfo
	{
		const PlaceParam par;
		Placement result;
		vec2i cpos; ///< Current position
		
		DroneInfo(GameCore& core, const PlaceParam& par): par(par) {
			cpos = core.get_lc().to_nonwall_coord(par.at);
		}
	};
	std::vector<DroneInfo> drones_orig;
	std::vector<DroneInfo*> drones;
	size_t result_idx;
	
	
	
	struct Ray {
		uint8_t prio = 0; // priority; 0 if not occupied
		
		void mark(const DroneInfo& d) {
			prio += d.par.dpar->placement_prio;
		}
	};
	
	struct Cell {
		vec2i pos;
		uint8_t i0, i1; ///< Origin indices range, [i0, i1)
		bool occupied = false;
		float dist; ///< Approximate, not squared
		
		LevelControl::Cell& lcell(GameCore& core) {return core.get_lc().mut_cell(pos);}
		int n_rays() {return i1 - i0;}
		int i_mid() {return (int(i0) + int(i1)) /2;}
	};
	
	GameCore& core;
	
	size_t origs_size; ///< Number of original rays
	std::vector<Ray> origs; ///< Rays from center
	std::vector<Cell> cells;
	
	vec2fp origin_pos;
	float ray_diff; ///< Distance between adjacent rays in radians
	float ray_offset; ///< Add this to world angle to obtain AoS angle
	
	
	
	Cell* find_cell(vec2i pos) {
		for (auto& c : cells) {
			if (c.pos == pos)
				return &c;
		}
		return nullptr;
	}
	void for_rays(Cell& c, callable_ref<void(Ray& ray)> f) {
		for (size_t i = c.i0; i < c.i1; ++i) f(origs[i]);
	}
	bool for_rays(Cell& c, callable_ref<bool(Ray& ray)> f) {
		for (size_t i = c.i0; i < c.i1; ++i) if (!f(origs[i])) return false;
		return true;
	}
	
	AI_AOS_Impl(GameCore& core): core(core) {}
	void place_begin(vec2fp origin, float max_radius) override
	{
		// prepare
		
		auto& lc = core.get_lc();
		
		origs_size = std::min(255., 2 * M_PI * max_radius);
		ray_diff = 2 * M_PI / origs_size;
		origin_pos = origin;
		
		origs.clear();
		origs.resize(origs_size);
		
		cells.clear();
		cells.reserve( std::pow((max_radius / lc.cell_size + 1)*2, 2) );
		
		drones_orig.clear();
		
		//
		
		const vec2fp ray_origin = origin / lc.cell_size;
		const vec2i grid_origin = lc.to_cell_coord(origin);
		const float ray_maxdist = std::ceil( max_radius / lc.cell_size );
		
		calc_intersection(
			Rect::from_center_le( grid_origin, vec2i::one(max_radius / lc.cell_size + 2) ),
			Rect{{}, lc.get_size(), true} )
		.map([&](vec2i p){
			lc.mut_cell(p).tmp = -1;
		});
		
		// raycast
		
		for (size_t i = 0; i < origs_size; ++i)
		{
			vec2fp ray_dir = {1, 0};
			ray_dir.fastrotate(i * ray_diff);
			
			// cast prepare
			
			float d_delta_x = std::abs(1 / ray_dir.x);
			float d_delta_y = std::abs(1 / ray_dir.y);
			float d_side_x, d_side_y;
			vec2i grid_step;
			
			if (ray_dir.x < 0) {
				grid_step.x = -1;
				d_side_x = (ray_origin.x - grid_origin.x) * d_delta_x;
			}
			else {
				grid_step.x = 1;
				d_side_x = (grid_origin.x + 1 - ray_origin.x) * d_delta_x;
			}
			
			if (ray_dir.y < 0) {
				grid_step.y = -1;
				d_side_y = (ray_origin.y - grid_origin.y) * d_delta_y;
			}
			else {
				grid_step.y = 1;
				d_side_y = (grid_origin.y + 1 - ray_origin.y) * d_delta_y;
			}
			
			// cast loop
			
			float ray_dist = 0;
			vec2i grid_pos = grid_origin;
			
			for (; ray_dist < ray_maxdist; )
			{
				if (d_side_x < d_side_y)
				{
					ray_dist += std::fabs(ray_dir.x);
					d_side_x += d_delta_x;
					grid_pos.x += grid_step.x;
				}
				else
				{
					ray_dist += std::fabs(ray_dir.y);
					d_side_y += d_delta_y;
					grid_pos.y += grid_step.y;
				}
				
				//
				
				auto& c = lc.mut_cell(grid_pos);
				if (c.is_wall)
					break;
				
				if (c.tmp < 0)
				{
					c.tmp = cells.size();
					
					auto& n = cells.emplace_back();
					n.i0 = i;
					n.i1 = i + 1;
					n.pos = grid_pos;
				}
				else
				{
					auto& n = cells[c.tmp];
					n.i1 = i + 1;
				}
			}
		}
		
		// offset rays for 360-0 cell

		if (cells.front().pos == cells.back().pos && cells.size() > 1)
		{
			int overlap = cells.back().n_rays();
			cells.pop_back();
			
			std::rotate(origs.rbegin(), origs.rbegin() + overlap, origs.rend());
			ray_offset = -overlap * ray_diff;
		}
		else ray_offset = 0;
		
		// calculate distance
		
		for (auto& c : cells)
			c.dist = lc.to_center_coord(c.pos).dist(origin_pos);
	}
	void place_end() override
	{
		// prepare
		
		result_idx = 0;
		
		drones.clear();
		drones.reserve( drones_orig.size() );
		for (auto& d : drones_orig) drones.push_back(&d);
		
		auto& lc = core.get_lc();
		
		// mark & remove statics
		
		auto it_part = std::partition( drones.begin(), drones.end(),
		[](auto& v){ return !v->par.is_static; });
		
		for (auto it = it_part; it != drones.end(); ++it)
		{
			auto& d = **it;
			if (d.par.is_visible)
			{
				auto c = find_cell(d.cpos);
				if (!c) continue; // shouldn't happen
				
				for_rays(*c, [&](auto& r){ r.mark(d); });
				c->occupied = true;
			}
		}
		
		drones.erase( it_part, drones.end() );
		
		// sort by priority descending
		
		std::sort( drones.begin(), drones.end(), [&](auto& a, auto& b)
		{
			auto ok = [&](auto& v){
				float max = v->par.dpar->dist_suspect;
				return v->par.at.dist_squ(origin_pos) < max * max ? 1 : 0;
			};
			int n = ok(a) - ok(b);
			if (n) return n > 0;
			
			auto dv = [](auto& v){
				return int(v->par.dpar->placement_prio) * 2 + v->par.is_visible;
			};
			return dv(a) > dv(b);
		});
		
		// for all
		
		for (auto& d : drones)
		{
			const float r_min = d->par.dpar->dist_minimal;
			const float r_max = d->par.dpar->dist_optimal;
			const float opt_dist = lerp(r_min, r_max, 0.8);
			const uint8_t pprio = d->par.dpar->placement_prio;
			
			//
			auto num_crowded = [&](vec2i p)
			{
				int n = 0;
				for (auto& d : AI_Const::placement_crowd_dirs)
				{
					if (auto c = lc.cell(p + d))
						n += c->tmp;
				}
				return n;
			};
			
			// prepare
			
			const float angle = (origin_pos - d->par.at).fastangle() + ray_offset + M_PI; // corrected
			const int optimal_ray = int_round(angle / ray_diff) % origs_size; // ray index
			const float dist_scale = 2.f / (r_max - r_min);
			
			// find best position
			
			Cell* best = nullptr;
			float best_k = 0;
			
			for (auto& c : cells)
			{
				if (c.occupied || c.dist < r_min || c.dist > r_max)
					continue;
				
				// ray info
				
				float rpr_higher = 0;
				float rpr_lower = 0; // occupied, but priority lower
				
				if (!for_rays(c, [&](auto& r){
					if (r.prio >= pprio) ++rpr_higher;
					else if (r.prio) ++rpr_lower;
					return r.prio - pprio < AI_Const::placement_max_prio_diff;
				}))
					continue;
				
				rpr_higher /= c.n_rays();
				rpr_lower /= c.n_rays();
				
				if (rpr_higher > 0.2 || (rpr_lower + rpr_higher) > 0.5)
					continue;
				
				// normalized angle difference
				float angdiff = modulo_dist<float>(optimal_ray, c.i_mid(), origs_size) / (origs_size /2);
				
				// normalized crowd
				float crowd = float(num_crowded(c.pos)) / (std::size(AI_Const::placement_crowd_dirs) /2);
				
				// normalized distance difference
				float ddist = (c.dist - opt_dist) * dist_scale;
				
				// normalized ray occupiance
				float rocc = 2 * rpr_higher + rpr_lower;
				
				//
				float k = 0;
				k += 3.0 * rocc;
				k += 0.7 * angdiff;
				k += 1.0 * crowd;
				k += 2.0 * ddist;
				
				if (best_k < k)
				{
					best = &c;
					best_k = k;
				}
			}
			
			if (best)
			{
				for_rays(*best, [&](auto& r){ r.mark(*d); });
				best->occupied = true;
				
				int freerad = d->par.dpar->placement_freerad;
				if (freerad) {
					for (auto& c : cells) {
						auto d = abs(c.pos - best->pos);
						if (d.x <= freerad || d.y <= freerad)
							c.occupied = true;
					}
				}
				
				d->result.tar = lc.to_center_coord(best->pos);
			}
		}
	}
	void place_feed(const PlaceParam& pars) override {
		reserve_more_block(drones_orig, 128);
		drones_orig.emplace_back(core, pars);
	}
	Placement place_result() override {
		return drones_orig[result_idx++].result;
	}
	
	
	void debug_draw() override
	{
		auto& lc = core.get_lc();
		for (auto& c : cells)
		{
			uint32_t clr;
			if (c.occupied) clr = 0xff000000;
			else            clr = 0x0000ff00;
			
			clr |= 0x30;
			
			vec2fp p = lc.to_center_coord(c.pos);
			GamePresenter::get()->dbg_rect(Rectfp::from_center( p, vec2fp::one(lc.cell_size/2) ), clr);
//			GamePresenter::get()->dbg_text(p, FMT_FORMAT("{:.1f}", c.dist));
		}
		
		for (auto& d : drones_orig)
		{
			if (!d.result.tar)
				GamePresenter::get()->dbg_rect(Rectfp::from_center( lc.to_center_coord(d.cpos), vec2fp::one(lc.cell_size/2) ), 0x00ff0040);
		}
	}
};
AI_AOS* AI_AOS::create(GameCore& core) {
	return new AI_AOS_Impl(core);
}
