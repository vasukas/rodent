#include "game/game_core.hpp"
#include "game/player_mgr.hpp"
#include "utils/noise.hpp"
#include "ai_drone.hpp"
#include "ai_group.hpp"
#include "ai_group_target.hpp"



class AI_Controller_Impl : public AI_Controller
{
public:
	std::vector<AI_Group*> gs;
	std::vector<std::unique_ptr<AI_GroupTarget>> tars;
	
	void step()
	{
		auto [r_on, r_off] = GameCore::get().get_pmg().get_ai_rects();
		
		for (auto& t : tars) t->update_aos();
		for (auto& g : gs)
		{
			if (g->is_enabled)
			{
				if (!g->g_area.intersects(r_off))
				{
					g->is_enabled = false;
					for (auto& d : g->get_drones())
						d->update_enabled(false);
					
					if (!std::holds_alternative<AI_Group::Idle>(g->state))
						g->set_idle();
				}
				else g->step();
			}
			else if (g->g_area.intersects(r_on))
			{
				g->is_enabled = true;
				for (auto& d : g->get_drones())
					d->update_enabled(true);
			}
		}
	}
	void reg(AI_Group* g)
	{
		gs.emplace_back(g);
	}
	void unreg(AI_Group* g)
	{
		auto it = std::find(gs.begin(), gs.end(), g);
		gs.erase(it);
	}
	AI_GroupTarget* ref_target(EntityIndex eid, AI_Group* g)
	{
		for (auto& t : tars)
		{
			if (t->eid == eid) {
				t->ref(g);
				return t.get();
			}
		}
		
		auto& t = tars.emplace_back(new AI_GroupTarget);
		
		t->eid = eid;
		t->report();
		t->update_aos();
		
		t->ref(g);
		return t.get();
	}
	void unref_target(AI_GroupTarget* tar, AI_Group* g)
	{
		tar->unref(g);
		if (tar->groups.empty())
		{
			auto it = std::find_if(tars.begin(), tars.end(), [&](auto& p){return p.get() == tar;});
			tars.erase(it);
		}
	}
};

static AI_Controller* ai_ctr_ptr;
AI_Controller& AI_Controller::get() {return *ai_ctr_ptr;}
AI_Controller* AI_Controller::init() {return ai_ctr_ptr = new AI_Controller_Impl;}
AI_Controller::~AI_Controller() {ai_ctr_ptr = nullptr;}



AI_Group::AI_Group(Rect area)
    : g_area(area), area( Rectfp{ g_area.off * LevelControl::get().cell_size, g_area.sz * LevelControl::get().cell_size, true } )
{
	AI_Controller::get().reg(this);
}
AI_Group::~AI_Group()
{
	if (ai_ctr_ptr)
	{
		reset_state();
		AI_Controller::get().unreg(this);
	}
}
void AI_Group::reg_upd(AI_Drone* drone, bool Add_or_rem)
{
	if (Add_or_rem) drones.emplace_back(drone);
	else {
		auto it = std::find(drones.begin(), drones.end(), drone);
		drones.erase(it);
	}
	
	upd_tasks = true;
	aos_ring_dist.clear();
	for_all_targets([](auto t){ t->aos_drone_change = true; });
}
void AI_Group::report_target(Entity* ent)
{
	if (!std::holds_alternative<Battle>(state))
	{
		reset_state();
		state = Battle{};
		upd_tasks = true;
	}
	
	auto& st = std::get<Battle>(state);
	for (auto& t : st.tars)
	{
		if (t->eid == ent->index)
		{
			if (!t->report()) upd_tasks = true;
			return;
		}
	}
	
	st.tars.emplace_back( AI_Controller::get().ref_target(ent->index, this) );
	upd_tasks = true;
}
void AI_Group::no_target(EntityIndex eid)
{
	if (auto st = std::get_if<Battle>(&state))
	{
		for (auto& t : st->tars)
		{
			if (t->eid == eid && (GameCore::get().get_step_time() - t->last_seen) > GameCore::step_len)
				t->is_lost = true;
		}
	}
}
void AI_Group::proxy_inspect(vec2fp suspect)
{
	if (auto st = std::get_if<Battle>(&state);
	    st && (st->tars.size() > 1 || !st->tars.front()->is_lost))
		return;
	
	auto now = GameCore::get().get_step_time();
	if (now - last_inspect_time < AI_Const::proxy_inspect_timeout) return;
	last_inspect_time = now;
	
	AI_Drone* best = nullptr;
	float best_dist = std::numeric_limits<float>::max();
	
	for (auto& d : drones)
	{
		if (!d->get_pars().is_camper && d->mov)
		{
			float dist = d->ent->get_pos().dist_squ(suspect);
			if (dist < best_dist)
			{
				best = d;
				best_dist = dist;
			}
		}
	}
	
	if (best)
		best->set_task(AI_Drone::TaskSuspect{ suspect });
}
const std::vector<float> &AI_Group::get_aos_ring_dist()
{
	auto& dr = aos_ring_dist;
	if (dr.empty())
	{
		for (auto& d : drones)
		{
			if (!d->mov) continue;
			auto& ps = d->get_pars();
			
			float diff = ps.dist_optimal - ps.dist_minimal;
			if (diff < 2) dr.push_back( ps.dist_minimal + diff /2 );
			else
			{
				int n = std::ceil(diff / 2);
				float dt = diff / n;
				for (float t = ps.dist_minimal; t < ps.dist_optimal + dt/2; t += dt)
					dr.push_back(t);
			}
		}
	}
	std::sort(dr.begin(), dr.end());
	dr.erase( std::unique(dr.begin(), dr.end()), dr.end() );
	return dr;
}
std::optional<vec2fp> AI_Group::get_aos_next(AI_Drone* d, bool Left_or_right)
{
	if (std::holds_alternative<Battle>(state))
	{
		auto aos = std::get<AI_Drone::TaskEngage>(d->task).tar->get_current_aos();
		if (aos) {
			vec2fp delta = aos->origin_pos - d->ent->get_pos();
			return aos->get_next( delta.fastangle(), delta.len_squ(), Left_or_right );
		}
	}
	return {};
}
void AI_Group::set_idle()
{
	reset_state();
	upd_tasks = true;
	state = Idle{};
}
std::string AI_Group::get_dbg_state() const
{	
	if (std::holds_alternative<Idle>(state)) return "State: idle";
	if (auto st = std::get_if<Battle>(&state))
	{
		std::string s;
		s = "State: battle";
		for (auto& t : st->tars)
		{
			auto ent = GameCore::get().get_ent(t->eid);
			if (!ent) s += "\n  Invalid target";
			else {
				s += "\n  (";
				s += ent->dbg_id();
				s += "), visible: ";
				s += std::to_string( t->is_visible() );
			}
		}
		return s;
	}
	if (std::holds_alternative<Search>(state)) return "State: search";
	return "State: UNKNOWN";
}
void AI_Group::step()
{
	if (std::holds_alternative<Idle>(state)) {}
	else if (auto st = std::get_if<Battle>(&state))
	{
		for (auto it = st->tars.begin(); it != st->tars.end(); )
		{
			if ((*it)->is_lost)
			{
				if (st->tars.size() != 1)
				{
					it = st->tars.erase(it);
					upd_tasks = true;
				}
				else
				{
					if (!area.contains( (*it)->last_pos, LevelControl::get().cell_size * 0.1 ))
					{
						set_idle();
						break;
					}
					
					size_t num = 0;
					for (auto& d : drones) if (!d->get_pars().is_camper && d->mov) ++num;
					
					if (!num)
					{
						state = Search{*it, true};
						break;
					}
					
					auto rings = (*it)->build_search(g_area);
					if (rings.empty())
					{
						state = Search{*it, true};
						break;
					}
					
					float a_diff = 2*M_PI / num;
					float a_cur = GameCore::get().get_random().range_n2() * a_diff;
					
					std::vector<uint8_t> used;
					used.resize(num);
					
					for (auto& d : drones)
					{
						if (!d->get_pars().is_camper && d->mov)
						{
							vec2fp pos = d->ent->get_pos();
							float rot = (pos - (*it)->last_pos).fastangle();
							
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
							
							d->set_task(AI_Drone::TaskSearch{ std::move(ps) });
						}
					}
					
					state = Search{*it};
					break;
				}
			}
			else ++it;
		}
	}
	else if (auto st = std::get_if<Search>(&state))
	{
		if (!st->all_final)
		{
			st->all_final = true;
			for (auto& d : drones)
			{
				auto ds = std::get_if<AI_Drone::Search>(&d->state);
				if (ds && !ds->end) {
					st->all_final = false;
					break;
				}
			}
		}
		else
		{
			st->left -= GameCore::step_len;
			if (st->left.is_negative()) set_idle();
		}
	}
	
	if (upd_tasks)
	{
		upd_tasks = false;
		
		if (std::holds_alternative<Idle>(state))
		{
			for (auto& d : drones)
				d->set_task(AI_Drone::TaskIdle{});
		}
		else if (auto st = std::get_if<Battle>(&state))
		{
			auto leave = [&](AI_Drone* d)
			{
				auto tk = std::get_if<AI_Drone::TaskEngage>(&d->task);
				return tk && GameCore::get().get_ent(tk->eid);
			};
			
			size_t free = 0;
			
			for (auto& d : drones)
			{
				if (leave(d)) continue;
				++free;
			}
			
			size_t per = free / st->tars.size();
			if (!per) per = 1;
			
			size_t tar = 0, cou = 0;
			for (auto& d : drones)
			{
				if (leave(d)) continue;
				d->set_task(AI_Drone::TaskEngage{ st->tars[tar]->eid, st->tars[tar]->last_pos, st->tars[tar] });
				
				if (++cou == per)
				{
					cou = 0;
					if (++tar == st->tars.size())
						break;
				}
			}
		}
	}
}
void AI_Group::for_all_targets(std::function<void(AI_GroupTarget*)> f)
{
	if (auto st = std::get_if<Search>(&state))
	{
		f(st->tar);
	}
	else if (auto st = std::get_if<Battle>(&state))
	{
		for (auto& t : st->tars)
			f(t);
	}
}
