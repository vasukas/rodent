#include <mutex>
#include "render/ren_aal.hpp"
#include "vaslib/vas_cpp_utils.hpp"
#include "game_core.hpp"
#include "presenter.hpp"

#include "render/ren_imm.hpp"
#include "damage.hpp"



EC_Render::EC_Render(Entity *ent, size_t sprite_id)
{
	this->ent = ent;
	
	PresCommand c;
	c.type = PresCommand::T_CREATE;
	c.index = sprite_id;
	send(c);
}
EC_Render::~EC_Render()
{
	PresCommand c;
	c.type = PresCommand::T_DEL;
	send(c);
}
void EC_Render::parts(size_t id, float power, Transform rel)
{
	PresCommand c;
	c.type = PresCommand::T_OBJPARTS;
	c.index = id;
	c.pos = rel;
	c.power = power;
	send(c);
}
size_t EC_Render::attach(size_t sprite_id, Transform rel)
{
	PresCommand c;
	c.type = PresCommand::T_ATTACH;
	c.index = sprite_id;
	c.pos = rel;
	send(c);
	return att_id++;
}
void EC_Render::detach(size_t id)
{
	PresCommand c;
	c.type = PresCommand::T_DETACH;
	c.index = id;
	send(c);
}
void EC_Render::send(PresCommand& c)
{
	c.obj = ent->index;
	GamePresenter::get().add_cmd(c);
}



class GamePresenter_Impl : public GamePresenter
{
public:
	std::vector<PresCommand> q_evs, q_next;
	std::mutex q_lock;
	
	std::vector<std::shared_ptr<ParticleGroupGenerator>> p_pars;
	std::vector<PresObject> p_objs;
	
	struct Object
	{
		EntityIndex ei;
		Transform tr, vel;
		FColor clr;
		size_t oid;
		
		float hp;
		float obj_rad; // radius
		
		struct Sub
		{
			size_t cmd; // command index
			size_t oid;
			Transform tr; // Relative
		};
		std::vector<Sub> subs;
		size_t sub_id;

		void reset() {
			vel = {};
			hp = -1;
			obj_rad = 0;
			subs.clear();
			sub_id = 0;
		}
	};
	std::vector<Object> objs;
	
	
	
	void render(TimeSpan passed)
	{
		std::vector<PresCommand> evs;
		{
			std::unique_lock g(q_lock);
			evs.swap(q_evs);
		}
		
		for (auto& e : evs)
		{
			switch (e.type)
			{
			case PresCommand::T_CREATE:
				{	if (e.obj >= objs.size()) objs.resize( e.obj + 1 );
					auto& o = objs[e.obj];
					o.ei = e.obj;
					o.tr = e.pos;
					o.oid = e.index;
					auto& p = p_objs[o.oid];
					o.clr = p.clr;
					o.reset();
				}
				break;
				
			case PresCommand::T_DEL:
				{	auto& o = objs[e.obj];
					o.ei = 0;
					auto& p = p_objs[o.oid];
					if (!p.ps.empty() && p.ps[0])
						p.ps[0]->draw(o.tr);
				}
				break;
				
			case PresCommand::T_OBJPARTS:
				{	auto& o = objs[e.obj];
					auto& p = p_objs[o.oid];
					p.ps[e.index]->draw(o.tr.get_combined(e.pos), e.power);
				}
				break;
				
			case PresCommand::T_ATTACH:
				{	auto& o = objs[e.obj];
					auto& s = o.subs.emplace_back();
					s.cmd = o.sub_id++;
					s.oid = e.index;
					s.tr = e.pos;
				}
				break;
				
			case PresCommand::T_DETACH:
				{	auto& o = objs[e.obj];
					auto it = std::find_if(o.subs.begin(), o.subs.end(), [i = e.index](auto& v){return v.cmd == i;});
					if (it != o.subs.end()) o.subs.erase(it);
				}
				break;
				
			case PresCommand::T_FREEPARTS:
				p_pars[e.index]->draw(e.pos, e.power);
				break;
			}
		}

		auto& ren = RenAAL::get();
		float tk = passed.seconds();
		
		auto& imm = RenImm::get();
		imm.set_context(RenImm::DEFCTX_WORLD);
		
		for (auto& o : objs)
		{
			if (!o.ei) continue;
			ren.draw_inst(o.tr, o.clr, p_objs[o.oid].id);
			
			if (o.hp >= 0)
			{
				const vec2fp sz = {2, 0.5};
				
				vec2fp base = o.tr.pos;
				base.y -= o.obj_rad + sz.y/2 + 0.5;
				
				Rectfp r;
				r.from_center(base, sz/2);
				
				imm.draw_rect(r, 0xff0000ff);
				r.b.x = r.a.x + sz.x * o.hp;
				imm.draw_rect(r, 0xc0ff00ff);
				r.b.x = r.a.x + sz.x;
				imm.draw_frame(r, 0xc0c0c0ff, 0.07);
			}
			
			if (!o.subs.empty())
			{
				for (auto& s : o.subs) {
					auto& p = p_objs[s.oid];
					ren.draw_inst(o.tr.get_combined(s.tr), p.clr, p.id);
				}
			}
			
			o.tr.add(o.vel * tk);
		}
	}
	void add_cmd(const PresCommand& cmd)
	{
		reserve_more_block(q_next, 256);
		q_next.emplace_back(cmd);
	}
	void submit()
	{
		std::unique_lock g(q_lock);
		if (q_evs.empty()) q_evs.swap(q_next);
		else {
			q_evs.insert( q_evs.end(), q_next.begin(), q_next.end() );
			q_next.clear();
		}
		auto& core = GameCore::get();
		for (auto& e : q_evs)
		{
			if (e.type == PresCommand::T_CREATE) {
				auto ent = core.get_ent(e.obj);
				if (!ent) continue;
				e.pos = ent->get_pos();
			}
		}
		for (auto& o : objs)
		{
			if (!o.ei) continue;
			if (auto e = core.get_ent(o.ei)) {
				o.tr = e->get_pos();
				o.vel = e->get_vel();
				
				auto& rc = e->getref<EC_Render>();
				if (rc.hp_shown)
				{
					auto hc = e->get<EC_Health>();
					if (hc && !hc->invincible)
						o.hp = hc->hp / hc->hp_max;
					else
						o.hp = -1;
				}
				else o.hp = -1;
				
				o.obj_rad = e->get_radius();
			}
		}
	}
	size_t add_preset(std::shared_ptr<ParticleGroupGenerator> p)
	{
		reserve_more_block(p_pars, 64);
		p_pars.emplace_back(p);
		return p_pars.size() - 1;
	}
	size_t add_preset(const PresObject& p)
	{
		reserve_more_block(p_objs, 128);
		p_objs.emplace_back(p);
		return p_objs.size() - 1;
	}
	void effect(size_t preset_id, Transform at, float power)
	{
		PresCommand c;
		c.type = PresCommand::T_FREEPARTS;
		c.index = preset_id;
		c.pos = at;
		c.power = power;
		add_cmd(c);
	}
};

static GamePresenter* rni;
GamePresenter& GamePresenter::get() {
	if (!rni) rni = new GamePresenter_Impl;
	return *rni;
}
GamePresenter::~GamePresenter() {rni = nullptr;}
