#include "game/game_core.hpp"
#include "game/physics.hpp"
#include "game/player_mgr.hpp"
#include "utils/noise.hpp"
#include "vaslib/vas_log.hpp"
#include "ai_algo.hpp"
#include "ai_drone.hpp"
#include "ai_control.hpp"



AI_Group::AI_Group(EntityIndex tar_eid)
	: tar_eid(tar_eid)
{
	aos.reset(AI_AOS::create());
	report_seen();
}
void AI_Group::forall(callable_ref<void(AI_Drone&)> f, AI_Drone* except)
{
	for (auto& d : drones)
		if (d != except)
			f(*d);
}
void AI_Group::report_seen()
{
	last_seen = GameCore::get().get_step_time();
	last_pos = GameCore::get().ent_ref(tar_eid).get_pos();
}
bool AI_Group::init_search(const std::vector<AI_Drone*>& drones, vec2fp target_pos, bool was_in_battle)
{
	size_t num = 0;
	for (auto& d : drones) {if (!d->is_camper()) ++num;}
	if (!num)
		return false;
	
	auto rings = calc_search_rings(target_pos);
	if (rings.empty())
		return false;
	
	float a_diff = 2*M_PI / num;
	float a_cur = GameCore::get().get_random().range_n2() * a_diff;
	
	std::vector<uint8_t> used;
	used.resize(num);
	
	while (!drones.empty())
	{
		AI_Drone* d = drones.front();
		
		if (d->is_camper())
		{
			if (was_in_battle)
				d->set_idle_state();
		}
		else
		{
			vec2fp pos = d->ent->get_pos();
			float rot = (pos - target_pos).fastangle();
			
			size_t opt_slot = int_round((M_PI + rot - a_cur) / a_diff);
			opt_slot %= used.size();
			
			if (used[opt_slot])
			{
				for (size_t i, off = 1 ;; ++off)
				{
					i = (opt_slot + off) % used.size();
					if (!used[i]) {opt_slot = i; break;}
					
					i = (opt_slot - off) % used.size();
					if (!used[i]) {opt_slot = i; break;}
				}
			}
			used[opt_slot] = 1;
			
			rot = opt_slot * a_diff + a_cur;
			
			//
			
			std::vector<vec2fp> ps;
			ps.reserve( rings.size() );
			
			for (auto& r : rings)
			{
				rot += 0.5 * a_diff * GameCore::get().get_random().range_n2();
				
				vec2fp sel = r.front();
				float sd = std::fabs(angle_delta(rot, (sel - pos).fastangle()));
				
				for (auto& p : r)
				{
					float d = std::fabs(angle_delta(rot, (p - pos).fastangle()));
					if (d < sd) {
						sel = p;
						sd = d;
					}
				}
				ps.emplace_back(sel);
			}
			
			if (std::holds_alternative<AI_Drone::Battle>(d->get_state()))
			{
				if (was_in_battle)
					d->replace_state( AI_Drone::Search{ std::move(ps), AI_Const::chase_search_delay } );
			}
			else
				d->add_state( AI_Drone::Search{ std::move(ps), AI_Const::chase_search_delay } );
		}
	}
	return true;
}
void AI_Group::init_search()
{
	if (!init_search(drones, last_pos, true))
	{
		while (!drones.empty())
			drones.back()->set_idle_state();
	}
}
bool AI_Group::is_visible() const
{
	return passed_since_seen() <= GameCore::step_len;
}
TimeSpan AI_Group::passed_since_seen() const
{
	return GameCore::get().get_step_time() - last_seen;
}
void AI_Group::update()
{
	// update radio
	
	if (drones.size() < AI_Const::msg_engage_max_bots)
	{
		auto& lc = LevelControl::get();
		std::vector<std::pair<const LevelControl::Room*, int>> rooms;
		
		for (auto& d : drones)
		{
			auto r = &lc.get().ref_room( lc.cref( lc.to_cell_coord(d->ent->get_pos()) ).room_nearest );
			if (rooms.end() == std::find_if( rooms.begin(), rooms.end(), [&](auto& v){return v.first == r;} ))
			{
				int dist = std::get<AI_Drone::Battle>(d->get_state()) .not_visible.is_positive()
						   ? AI_Const::msg_engage_relay_dist
						   : AI_Const::msg_engage_dist;
				rooms.push_back({ r, dist });
			}
		}
		
		for (auto& rp : rooms)
		{
			room_flood( rp.first->area.center(), rp.second + msg_range_extend, false,
			[&](auto& rm, auto)
			{	
				room_query(rm, 
				[&](AI_Drone& d)
				{
					if (!std::holds_alternative<AI_Drone::Battle>( d.get_state() ))
						d.set_battle_state();
					return true;
				});
				return true;
			});
		}
		
		bool all_campers = [&]{
			for (auto& d : drones) if (!d->is_camper()) return false;
			return true;
		}();
		if (all_campers) ++msg_range_extend;
		else msg_range_extend = 0;
	}
	
	// update aos
	
	if (is_visible())
	{
		auto is_ignored = [&](AI_Drone& d)
		{
			if (d.is_camper() && d.mov) return false;
			float r = d.get_pars().dist_visible;
			return r*r < d.ent->get_pos().dist_squ(last_pos);
		};
		
		float rad = 0;
		for (auto& d : drones)
			rad = std::max(rad, d->get_pars().dist_optimal);
		
		aos->place_begin(last_pos, rad);
		
		for (auto& d : drones)
		{
			if (is_ignored(*d)) continue;
			AI_AOS::PlaceParam p;
			p.at = d->ent->get_pos();
			p.dpar = &d->get_pars();
			p.is_static = !d->mov;
			p.is_visible = !std::get<AI_Drone::Battle>(d->get_state()).not_visible.is_positive();
			aos->place_feed(p);
		}
		
		aos->place_end();
		
		for (auto& d : drones)
		{
			auto& st = std::get<AI_Drone::Battle>(d->get_state());
			if (is_ignored(*d)) st.placement = {};
			else st.placement = aos->place_result().tar;
		}
	}
	else
	{
		for (auto& d : drones)
			std::get<AI_Drone::Battle>(d->get_state()).placement = {};
	}
}



AI_GroupPtr::AI_GroupPtr(AI_Group* grp, AI_Drone* dr)
	: grp(grp), dr(dr)
{
	grp->drones.push_back(dr);
}
AI_GroupPtr::~AI_GroupPtr()
{
	if (!grp) return;
	
	auto& vs = grp->drones;
	auto it = std::find(vs.begin(), vs.end(), dr);
	if (it != vs.end()) vs.erase(it);
	
	if (grp->drones.empty())
		AI_Controller::get().free_group(grp);
}
AI_GroupPtr& AI_GroupPtr::operator= (AI_GroupPtr&& p) noexcept
{
	std::swap(p.grp, grp);
	std::swap(p.dr,  dr);
	return *this;
}



class AI_Controller_Impl : public AI_Controller
{
public:
	std::vector<AI_Drone*> drs;
	TimeSpan check_tmo; // online area check
	
	b2DynamicTree res_tree;
	
	std::optional<AI_Group> the_only_group;
	TimeSpan group_check_tmo;
	
	
	
	void step() override
	{
		if (group_check_tmo.is_positive()) group_check_tmo -= GameCore::step_len;
		else if (the_only_group) {
			group_check_tmo = AI_Const::group_update_timeout;
			the_only_group->update();
		}
		
		if (the_only_group && show_aos_debug)
			the_only_group->aos->debug_draw();
		
		if (check_tmo.is_positive()) check_tmo -= GameCore::step_len;
		else {
			check_tmo = AI_Const::online_check_timeout;
			
			auto [r_on, r_off] = GameCore::get().get_pmg().get_ai_rects();

			GameCore::get().get_phy().query_aabb(r_off, [](Entity& ent, auto&){
				if (auto d = ent.get_ai_drone())
					d->is_online |= 2;
			});
			GameCore::get().get_phy().query_aabb(r_on, [](Entity& ent, auto&){
				if (auto d = ent.get_ai_drone())
					d->is_online |= 4;
			});
			
			for (auto& d : drs)
			{
				if (d->is_online & 6)
				{
					if (d->is_online & 4) {
						d->is_online &= 1;
						d->set_online(true);
					}
					else d->is_online &= 1;
				}
				else {
					d->is_online &= 1;
					d->set_online(false);
				}
			}
		}
	}
	AI_GroupPtr get_group(AI_Drone& drone) override
	{
		if (!the_only_group) the_only_group.emplace( GameCore::get().get_pmg().get_ent()->index );
		return {&*the_only_group, &drone};
	}
	void help_call(AI_Drone& drone, std::optional<vec2fp> target, bool high_prio) override
	{
		int dist = high_prio ? AI_Const::msg_helpcall_highprio_dist : AI_Const::msg_helpcall_dist;
		vec2fp pos = target ? *target : drone.ent->get_pos();
		
		room_flood_p(pos, dist, true,
		[&](auto& room, int)
		{
			bool ret = true;
			std::optional<std::pair<AI_Drone*, float>> best;
			
			room_query(room,
			[&](AI_Drone& d)
			{
				if (&d == &drone || d.is_camper() || d.get_pars().helpcall == AI_DroneParams::HELP_NEVER)
					return true;
				
				if (auto st = std::get_if<AI_Drone::Suspect>( &d.get_state() ))
				{
					if (target) st->pos = *target;
					else if (!st->was_visible) st->pos = pos;
					best.reset();
					return (ret = false);
				}
				
				if (std::holds_alternative<AI_Drone::Idle>( d.get_state() ))
				{
					float dist = d.ent->get_pos().dist_squ( pos );
					
					if (!best) best = {&d, dist};
					else
					{
						int hdiff = d.get_pars().helpcall - best->first->get_pars().helpcall;
						if (hdiff > 0) best = {&d, dist};
						else if (!hdiff && best->second < dist) best = {&d, dist};
					}
				}
				return true;
			});
			
			if (best)
			{
				AI_Drone::Suspect st{ pos };
				st.prio = high_prio ? AI_Drone::Suspect::PRIO_HELPCALL_HIGH : AI_Drone::Suspect::PRIO_HELPCALL;
				
				best->first->add_state(std::move(st));
				return false;
			}
			return ret;
		});
	}
	
	
	
	void free_group(AI_Group*) override
	{
		the_only_group.reset();
	}
	void ref_drone(AI_Drone* d) override
	{
		drs.push_back(d);
	}
	void unref_drone(AI_Drone* d) override
	{
		auto it = std::find(drs.begin(), drs.end(), d);
		drs.erase(it);
	}
	int ref_resource(Rectfp p, AI_SimResource* r) override
	{
		return res_tree.CreateProxy({conv(p.lower()), conv(p.upper())}, r);
	}
	void find_resource(Rectfp p, callable_ref<void(AI_SimResource&)> f) override
	{
		struct Cb {
			AI_Controller_Impl* c;
			callable_ref<void(AI_SimResource&)>* f;
			
			bool QueryCallback(int id) {
				(*f)(*static_cast<AI_SimResource*>(c->res_tree.GetUserData(id)));
				return true;
			}
		};
		Cb cb{ this, &f };
		res_tree.Query(&cb, {conv(p.lower()), conv(p.upper())});
	}
};

static AI_Controller* ai_ctr_ptr;
AI_Controller& AI_Controller::get() {return *ai_ctr_ptr;}
AI_Controller* AI_Controller::init() {return ai_ctr_ptr = new AI_Controller_Impl;}
AI_Controller::~AI_Controller() {ai_ctr_ptr = nullptr;}
