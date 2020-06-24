#include "game/game_core.hpp"
#include "game/game_info_list.hpp"
#include "game/physics.hpp"
#include "game/player_mgr.hpp"
#include "game_objects/objs_creature.hpp"
#include "utils/noise.hpp"
#include "vaslib/vas_log.hpp"
#include "ai_algo.hpp"
#include "ai_drone.hpp"
#include "ai_control.hpp"



AI_Group::AI_Group(GameCore& core, EntityIndex tar_eid)
	: core(core), tar_eid(tar_eid)
{
	aos.reset(AI_AOS::create(core));
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
	last_seen = core.get_step_time();
	last_pos = core.ent_ref(tar_eid).get_pos();
}
bool AI_Group::init_search(GameCore& core, const std::vector<AI_Drone*>& drones, vec2fp target_pos,
                           bool was_in_battle, std::optional<vec2fp> real_target_pos)
{
	size_t num = 0;
	for (auto& d : drones) {if (!d->is_camper()) ++num;}
	if (!num)
		return false;
	
	bool real_inside = false;
	auto rings = calc_search_rings(core, target_pos, real_target_pos.value_or(vec2fp{}), real_inside);
	if (rings.empty())
		return false;
	
	float a_diff = 2*M_PI / num;
	float a_cur = core.get_random().range_n2() * a_diff;
	
	std::vector<uint8_t> used;
	used.resize(num);
	
	AI_Drone* real_ptr = nullptr;
	float real_dist = std::numeric_limits<float>::max();
	
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
			vec2fp pos = d->ent.get_pos();
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
				rot += 0.5 * a_diff * core.get_random().range_n2();
				
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
				else continue;
			}
			else
				d->add_state( AI_Drone::Search{ std::move(ps), AI_Const::chase_search_delay } );
			
			if (real_inside) {
				float dist = pos.dist_squ(*real_target_pos);
				if (dist < real_dist) {
					real_ptr = d;
					real_dist = dist;
				}
			}
		}
	}
	
	if (real_ptr) {
		auto& st = std::get<AI_Drone::Search>(real_ptr->get_state());
		st.pts[0] = *real_target_pos;
	}
	return true;
}
void AI_Group::init_search()
{
	std::optional<vec2fp> real_tar;
	if (auto tar = core.get_ent(tar_eid))
		real_tar = tar->get_pos();
	
	if (!init_search(core, drones, last_pos, true, real_tar))
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
	return core.get_step_time() - last_seen;
}
void AI_Group::update()
{
	bool all_campers = [&]{
		for (auto& d : drones) if (!d->is_camper()) return false;
		return true;
	}();
	
	// check for hanging (usually when no path can be built)
	
	if (passed_since_seen() > AI_Const::battle_reset_timeout)
	{
		init_search();
		return;
	}
	
	// update radio
	
	bool battle_call = drones.size() < AI_Const::msg_engage_max_bots;
	if (battle_call)
	{
		battle_call = false;
		for (auto& d : drones) {
			if (!std::get<AI_Drone::Battle>(d->get_state()).used_radiocall) {
				battle_call = true;
				break;
			}
		}
		if (battle_call) {
			for (auto& d : drones)
				std::get<AI_Drone::Battle>(d->get_state()).used_radiocall = true;
		}
	}
	if (battle_call)
	{
		auto& lc = core.get_lc();
		std::vector<std::pair<const LevelCtrRoom*, bool>> rooms; // flag - is relay
		
		for (auto& d : drones)
		{
			auto r = &lc.ref_room( lc.cref( lc.to_cell_coord(d->ent.get_pos()) ).room_nearest );
			if (rooms.end() == std::find_if( rooms.begin(), rooms.end(), [&](auto& v){return v.first == r;} ))
			{
				int dist = std::get<AI_Drone::Battle>(d->get_state()).not_visible.is_positive();
				rooms.push_back({ r, dist });
			}
		}
		
		for (auto& rp : rooms)
		{
			int dist = rp.second ? AI_Const::msg_engage_relay_dist : AI_Const::msg_engage_dist;
			bool allow_adj = !rp.second;
			
			room_radio_flood( core, rp.first->area.center(), dist + msg_range_extend, false, allow_adj,
			[&](auto& rm, auto)
			{
				room_query(core, rm,
				[&](AI_Drone& d)
				{
					if (d.is_online && !std::holds_alternative<AI_Drone::Battle>( d.get_state() ))
						d.set_battle_state();
					return drones.size() < AI_Const::msg_engage_hard_limit;
				});
				return drones.size() < AI_Const::msg_engage_hard_limit;
			});
		}
		
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
			return r*r < d.ent.get_pos().dist_squ(last_pos);
		};
		
		float rad = 0;
		for (auto& d : drones)
			rad = std::max(rad, d->get_pars().dist_optimal);
		
		aos->place_begin(last_pos, rad);
		
		for (auto& d : drones)
		{
			if (is_ignored(*d)) continue;
			AI_AOS::PlaceParam p;
			p.at = d->ent.get_pos();
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
		
		if (all_campers && passed_since_seen() > TimeSpan::seconds(2)) {
			while (!drones.empty()) {
				auto st = AI_Drone::Suspect{ drones.back()->ent.get_pos(), AI_Const::suspect_max_level };
				drones.back()->set_single_state(st);
			}
		}
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
		grp->core.get_aic().free_group(grp);
}
AI_GroupPtr& AI_GroupPtr::operator= (AI_GroupPtr&& p) noexcept
{
	std::swap(p.grp, grp);
	std::swap(p.dr,  dr);
	return *this;
}



struct HunterScanner
{
	// distance check
	static constexpr TimeSpan dist_wait = TimeSpan::seconds(2);
	TimeSpan dist_wait_until;
	
	// position check
	TimeSpan wait_until;
	std::optional<TimeSpan> failed_until; // for failed searches
	bool wait_forced = false; // can't reduce wait time
	TimeSpan wait_next = AI_Const::hunter_scan_max.second;
	
	//
	vec2fp pos; // last 'scanned'  // both positions are inited in ref()
	vec2fp last_alive;             //   by position of first user
	bool was_dead = false;
	std::vector<AI_Drone*> users;
	
	// delayed update - don't update all drones in one step, as paths can be extremely long
	const int per_step = 4;
	size_t i_delay = 0;
	int check_was;
	
	void delayed_update(bool forced)
	{
		size_t n = forced ? users.size() : std::min(users.size(), i_delay + per_step);
		for (; i_delay < n; ++i_delay) {
			auto& st = *users[i_delay]->as_scanner();
			if (check_was == 1 || st.has_failed) {
				st.pos = pos;
				st.has_failed = false;
			}
		}
	}
	void update(GameCore& core, AI_Group* grp)
	{
		if (users.empty()) {
			wait_until = {};
			wait_forced = false;
			failed_until = {};
			was_dead = false;
			return;
		}
		
		bool alive;
		if (auto tar = core.get_pmg().get_ent())
		{
			if (auto rm = core.get_lc().get_room(tar->get_pos());
				!rm || rm->type != LevelCtrRoom::T_TRANSIT)
			{
				pos = last_alive = tar->get_pos();
			}
			alive = true;
		}
		else {
			force_reset(core, AI_Const::hunter_scan_death_delay);
			alive = false;
		}
		
		TimeSpan now = core.get_step_time();
		int check = 0;
		
		if (failed_until && *failed_until <= now) {
			failed_until.reset();
			check = 2;
		}
		if (was_dead && alive && !wait_forced) {
			was_dead = false;
			wait_until = std::min(wait_until, now + AI_Const::hunter_scan_new_delay);
		}
		if (grp && !wait_forced) {
			wait_until = std::min(wait_until, now + AI_Const::hunter_scan_new_delay);
		}
		
		if (wait_until <= now) {
			wait_until = now + wait_next;
			wait_forced = false;
			check = 1;
		}
		else if (dist_wait_until <= now) {
			dist_wait_until = now + dist_wait;
			
			float dist = std::pow(AI_Const::hunter_scan_max.first + 1, 2);
			for (AI_Drone* d : users) {
				dist = std::min(dist, d->ent.get_pos().dist_squ(pos));
			}
			dist = std::sqrt(dist);
			
			float t = inv_lerp(AI_Const::hunter_scan_min.first, AI_Const::hunter_scan_max.first, dist);
			t = clampf_n(t);
			wait_next = lerp(AI_Const::hunter_scan_min.second, AI_Const::hunter_scan_max.second, t);
			
			if (!wait_forced)
				wait_until = std::min(wait_until, now + wait_next);
		}
		
		if (check)
		{
			if (!alive) {
				if (check == 2) return;
				was_dead = true;
			}
			failed_until.reset();
			
			delayed_update(true);
			i_delay = 0;
			check_was = check;
		}
		delayed_update(false);
	}
	void ref(AI_Drone* d)
	{
		if (d->as_scanner()) {
			if (users.empty()) pos = last_alive = d->ent.get_pos();
			if (!wait_forced) wait_until = std::min(wait_until, d->ent.core.get_step_time() + AI_Const::hunter_scan_new_delay);
			users.push_back(d);
		}
	}
	void unref(AI_Drone* d)
	{
		if (d->as_scanner()) {
			erase_if_find(users, d);
			delayed_update(true);
		}
	}
	void failed(GameCore& core)
	{
		if (!failed_until)
			failed_until = core.get_step_time() + AI_Const::hunter_scan_failed_tmo;
	}
	void force_reset(GameCore& core, TimeSpan fixed_wait)
	{
		wait_until = core.get_step_time() + fixed_wait;
		wait_forced = true;
	}
};



class AI_Controller_Impl : public AI_Controller
{
public:
	GameCore& core;
	
	std::vector<AI_Drone*> drs;
	TimeSpan check_tmo; // online area check
	
	b2DynamicTree res_tree;
	std::vector<std::unique_ptr<AI_SimResource>> res_list;
	
	std::optional<AI_Group> the_only_group;
	bool group_was = false;
	TimeSpan group_check_tmo;
	
	std::vector<EntityIndex> hunters;
	TimeSpan hunter_resp_tmo = TimeSpan::seconds(2*60);
	HunterScanner scanner;
	
	float g_susp = 0;
	
	
	
	AI_Controller_Impl(GameCore& core): core(core) {}
	void step() override
	{
		if (group_check_tmo.is_positive()) group_check_tmo -= GameCore::step_len;
		else if (the_only_group) {
			group_check_tmo = AI_Const::group_update_timeout;
			the_only_group->update();
		}
		
		if (group_was && !the_only_group) g_susp = 1;
		else g_susp = std::max(0., g_susp - core.step_len / AI_Const::global_suspect_decr);
		group_was = !!the_only_group;
		
		debug_batle_number = the_only_group ? the_only_group->drones.size() : 0;
		if (the_only_group && show_aos_debug)
			the_only_group->aos->debug_draw();
		
		if (check_tmo.is_positive()) check_tmo -= GameCore::step_len;
		else {
			check_tmo = AI_Const::online_check_timeout;
			
			auto [r_on, r_off] = core.get_pmg().get_ai_rects();

			core.get_phy().query_aabb(r_off, [](Entity& ent, auto&){
				if (auto d = ent.get_ai_drone())
					d->is_online |= 2;
			});
			core.get_phy().query_aabb(r_on, [](Entity& ent, auto&){
				if (auto d = ent.get_ai_drone())
					d->is_online |= 4;
			});
			
			for (auto& d : drs)
			{
				if (d->always_online)
				{
					if (!(d->is_online & 1)) {
						d->is_online = 0;
						d->set_online(true);
					}
					else d->is_online = 1;
				}
				else if (d->is_online & 6)
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
		
		// hunters
		
		if (hunter_resp_tmo.is_positive()) {
			hunter_resp_tmo -= core.step_len;
		}
		if (hunters.size() < AI_Const::hunter_max_count && !hunter_resp_tmo.is_positive() && core.spawn_hunters)
		{
			auto& list = core.get_info().get_assembler_list();
			if (!list.empty()) {
				auto ent = new EHunter(core, core.get_random().random_el(list).prod_pos);
				hunters.push_back(ent->index);
				hunter_resp_tmo = AI_Const::hunter_respawn_tmo;
			}
		}
		erase_if(hunters, [&](auto& i) {return !core.get_ent(i);});
		scanner.update(core, the_only_group ? &*the_only_group : nullptr);
	}
	AI_GroupPtr get_group(AI_Drone& drone) override
	{
		if (!the_only_group) the_only_group.emplace( core, core.get_pmg().ref_ent().index );
		return {&*the_only_group, &drone};
	}
	void help_call(AI_Drone& drone, std::optional<vec2fp> target, bool high_prio) override
	{
		int dist = high_prio ? AI_Const::msg_helpcall_highprio_dist : AI_Const::msg_helpcall_dist;
		vec2fp pos = target ? *target : drone.ent.get_pos();
		
		room_radio_flood(core, core.get_lc().to_cell_coord(pos), dist, true, true,
		[&](auto& room, int)
		{
			bool ret = true;
			std::optional<std::pair<AI_Drone*, float>> best;
			
			room_query(core, room,
			[&](AI_Drone& d)
			{
				if (!d.is_online || &d == &drone || d.is_camper() || d.get_pars().helpcall == AI_DroneParams::HELP_NEVER)
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
					float dist = d.ent.get_pos().dist_squ( pos );
					
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
	bool is_targeted(Entity& ent) override
	{
		return the_only_group && the_only_group->tar_eid == ent.index;
	}
	void force_reset_scanner(TimeSpan timeout) override
	{
		scanner.force_reset(core, timeout);
	}
	float get_global_suspicion() override
	{
		return std::min(g_susp, AI_Const::global_suspect_max);
	}
	
	
	
	void free_group(AI_Group*) override
	{
		the_only_group.reset();
	}
	void ref_drone(AI_Drone* d) override
	{
		drs.push_back(d);
		scanner.ref(d);
	}
	void unref_drone(AI_Drone* d) override
	{
		auto it = std::find(drs.begin(), drs.end(), d);
		drs.erase(it);
		scanner.unref(d);
	}
	int ref_resource(Rectfp p, AI_SimResource* r) override
	{
		reserve_more_block(res_list, 128);
		res_list.emplace_back(r);
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
	void mark_scan_failed() override
	{
		scanner.failed(core);
	}
};
AI_Controller* AI_Controller::create(GameCore& core) {
	return new AI_Controller_Impl(core);
}
