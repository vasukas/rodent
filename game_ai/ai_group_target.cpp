#include "game/game_core.hpp"
#include "utils/noise.hpp"
#include "ai_drone.hpp"
#include "ai_group.hpp"
#include "ai_group_target.hpp"



void AI_AOS::generate(vec2fp origin, const std::vector<float>& ring_dist)
{
	auto& lc = LevelControl::get();
	rings.clear();
	
	origs_size = std::min(255., 2 * M_PI * ring_dist.back());
	ray_diff = 2 * M_PI / origs_size;
	origin_pos = origin;
	
	rings.resize( ring_dist.size() );
	for (size_t i=0; i < rings.size(); ++i)
	{
		rings[i].dist = ring_dist[i] * ring_dist[i];
		rings[i].perlen = 2 * M_PI * ring_dist[i];
		rings[i].fronts.emplace_back();
	}
	
	//
	
	const vec2fp ray_origin = origin / lc.cell_size;
	const vec2i grid_origin = lc.to_cell_coord(origin);
	const float ray_maxdist = std::ceil( ring_dist.back() / lc.cell_size );
	
	for (size_t i = 0; i < origs_size; ++i)
	{
		// originally based on https://lodev.org/cgtutor/raycasting.html
		
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
		vec2i prev_pos = grid_origin;
		vec2i grid_pos = grid_origin;
		size_t next_ring = 0;
		bool dont_add = false;
		
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
			
			float dist = ray_dist * lc.cell_size;
			
			auto add = [&](bool closer)
			{
				auto& ring = rings[next_ring];
				auto& fs = ring.fronts;
				auto f = &fs.back();
				
				if (!f->rays.empty() && f->rays.back().cell == prev_pos)
				{
					++f->rays.back().i1;
					return;
				}
				
				if (!f->rays.empty() && f->closer != closer)
					f = &fs.emplace_back();
				
				if (f->rays.empty())
					f->closer = closer;
					
				auto& r = f->rays.emplace_back();
				r.cell = prev_pos;
				r.i0 = i;
				r.i1 = i + 1;
				r.dist = dist;
			};
			
			//
			
			if (lc.cref(grid_pos).is_wall)
			{
				if (!dont_add) add(true);
				for (size_t i = next_ring; i < rings.size(); ++i)
				{
					auto& fs = rings[i].fronts;
					if (!fs.empty() && !fs.back().rays.empty() && !fs.back().closer)
						fs.emplace_back();
				}
				break;
			}
			
			prev_pos = grid_pos;
			dont_add = false;
			
			if (dist >= ring_dist[next_ring])
			{
				add(false);
				if (++next_ring == rings.size()) break;
				dont_add = true;
			}
		}
	}
	
	// hack - remove doubles
	
	auto& r0fs = rings[0].fronts;
	for (size_t ri = 1; ri < rings.size(); ++ri)
	{
		for (auto& f : rings[ri].fronts)
		{
			if (!f.closer) continue;
			for (auto& pf : r0fs)
			{
				if (pf.closer) continue;
				for (auto& r : pf.rays)
				{
					for (auto it = f.rays.begin(); it != f.rays.end(); )
					{
						if (r.cell == it->cell) it = f.rays.erase(it);
						else ++it;
					}
				}
			}
		}
	}
	
	// remove empty
	
	for (auto& r : rings)
	{
		for (auto it = r.fronts.begin(); it != r.fronts.end(); )
		{
			if (it->rays.empty()) it = r.fronts.erase(it);
			else ++it;
		}
	}
	
	while (rings.back().fronts.empty())
		rings.pop_back();
	
	// at least one ring with one front and one ray always remains
	
	// merge 360/0 fronts/rays
	
	for (auto& ring : rings)
	{
		auto& fs = ring.fronts;
		if (fs.size() == 1) continue;
		
		auto& rs0 = fs.front().rays;
		auto& rs1 = fs.back().rays;
		
		auto& ray0 = rs0.front();
		auto& ray1 = rs1.back();
		
		if (ray0.cell == ray1.cell)
		{
			ray0.i0 = ray1.i0;
			ray0.dist = (ray0.dist + ray1.dist) /2;
			
			rs0.insert( rs0.begin(), rs1.begin(), rs1.end() - 1 );
			fs.pop_back();
		}
	}
}
vec2fp AI_AOS::get_next(float angle, float dist_squ, bool Left_or_right)
{
	uint8_t opt_index = (angle + M_PI) / ray_diff;
	dist_squ += 0.5; // rounding
	
//	opt_index += Left_or_right ? 1 : -1;
	opt_index %= origs_size;
	
	Ring* ring = nullptr;
	if (rings.front().dist > dist_squ) ring = &rings.front();
	else {
		for (size_t i = rings.size() - 1; i < rings.size(); --i)
		{
			if (rings[i].dist <= dist_squ)
			{
				ring = &rings[i];
				break;
			}
		}
	}
	
	auto& fs = ring->fronts;
	for (size_t fi=0; fi < fs.size(); ++fi)
	{
		auto& f = fs[fi];
		if (is_in(f, opt_index))
		{
			auto& rs = f.rays;
			for (size_t ri=0; ri < rs.size(); ++ri)
			{
				if (is_in(rs[ri], opt_index))
				{
					auto ret = [&](auto& ray) {
						return LevelControl::get().to_center_coord( ray.cell );
					};
					if (Left_or_right)
					{
						if (ri != rs.size() - 1) return ret(rs[ri + 1]);
						else
							return ret(fs[ (fi + 1) % fs.size() ].rays.front());
					}
					else
					{
						if (ri) return ret(rs[ri - 1]);
						else
							return ret(fs[ (fi - 1) % fs.size() ].rays.back());
					}
				}
			}
			break;
		}
	}
	
	// shouldn't be ever reached
	return origin_pos;
}
bool AI_AOS::is_in(Front& f, uint8_t opt_index)
{
	if (f.rays.front().i0 < f.rays.front().i1) // wrap check
	{
		return opt_index >= f.rays.front().i0 &&
		       opt_index <  f.rays.back ().i1;
	}
	else
	{
		return opt_index >= f.rays.front().i0 ||
		       opt_index <  f.rays.back ().i1;
	}
}
bool AI_AOS::is_in(FrontRay& r, uint8_t opt_index)
{
	return r.i0 < r.i1
	        ? r.i0 <= opt_index && opt_index < r.i1
			: r.i0 <= opt_index || opt_index < r.i1;
}



bool AI_GroupTarget::report()
{
	last_seen = GameCore::get().get_step_time();
	
	vec2fp n_pos = GameCore::get().get_ent(eid)->get_pos();
	if (!is_lost && LevelControl::get().is_same_coord( last_pos, n_pos ))
		return true;
	
	last_pos = n_pos;
	is_lost = false;
	return false;
}
bool AI_GroupTarget::is_visible() const
{
	if (is_lost) return false;
	return (GameCore::get().get_step_time() - last_seen) < AI_Const::grouptarget_lost_time;
}
void AI_GroupTarget::update_aos()
{
	if (!aos_request) return;
	
	auto t_now = GameCore::get().get_step_time();
	if (t_now - aos_time < AI_Const::aos_update_timeout) return;
	
	//
	
	if (aos_drone_change)
	{
		aos_dist.clear();
		
		for (auto& g : groups) {
			auto& d = g->get_aos_ring_dist();
			aos_dist.insert( aos_dist.end(), d.begin(), d.end() );
		}
		
		std::sort(aos_dist.begin(), aos_dist.end());
		aos_dist.erase( std::unique(aos_dist.begin(), aos_dist.end()), aos_dist.end() );
	}
	
	aos.generate(last_pos, aos_dist);
	aos_time = t_now;
	aos_prev = LevelControl::get().to_cell_coord(last_pos);
	aos_request = false;
	aos_drone_change = false;
}
std::vector<std::vector<vec2fp>> AI_GroupTarget::build_search(Rect grid_area) const
{
	static const std::array<vec2i, 4> dirs{{ {1, 0}, {0, 1}, {-1, 0}, {0, -1} }};
	auto& ring_dist = AI_Const::search_ring_dist;
	auto& lc = LevelControl::get();
	
	if (!grid_area.contains_le( lc.to_cell_coord(last_pos) ))
		return {};
	
	std::vector<uint8_t> cs;
	cs.resize( grid_area.size().area() );
	
	vec2i off = grid_area.lower();
	auto getc = [&](vec2i p) -> auto& {
		p -= off;
		return cs[p.y * grid_area.size().x + p.x];
	};
	
	std::vector<vec2i> free_nodes;
	free_nodes.emplace_back( lc.to_cell_coord(last_pos) );
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
AI_AOS* AI_GroupTarget::get_current_aos()
{
	if (!is_visible()) return nullptr;
	if (LevelControl::get().to_cell_coord(last_pos) != aos_prev || aos_drone_change)
	{
		aos_request = true;
		return nullptr;
	}
	return &aos;
}
void AI_GroupTarget::ref(AI_Group* g)
{
	groups.push_back(g);
	
	aos_dist.clear();
	aos_drone_change = true;
}
void AI_GroupTarget::unref(AI_Group* g)
{
	for (auto& d : g->get_drones())
	{
		if (auto tk = std::get_if<AI_Drone::TaskEngage>(&d->task);
		    tk && tk->tar == this)
		{
			d->set_task( AI_Drone::TaskIdle{} );
		}
	}
	
	auto it = std::find(groups.begin(), groups.end(), g);
	groups.erase(it);
	
	aos_dist.clear();
	aos_drone_change = true;
}
