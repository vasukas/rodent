#include <mutex>
#include "render/ren_aal.hpp"
#include "render/particles.hpp"
#include "vaslib/vas_cpp_utils.hpp"
#include "game_core.hpp"
#include "presenter.hpp"



EC_Render::EC_Render(Entity *ent, size_t sprite_id): ent(ent)
{
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
		Transform tr;
		FColor clr;
		size_t oid;
	};
	std::vector<Object> objs;
	
	
	
	void render(TimeSpan)
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
					o.tr = {};
					o.oid = e.index;
					auto& p = p_objs[o.oid];
					o.clr = p.clr;
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
					p.ps[e.index]->draw(o.tr + e.pos, e.power);
				}
				break;
				
			case PresCommand::T_FREEPARTS:
				p_pars[e.index]->draw(e.pos, e.power);
				break;
			}
		}

		auto& core = GameCore::get();
		auto& ren = RenAAL::get();
		
		for (auto& o : objs)
		{
			if (!o.ei) continue;
			auto ent = core.get_ent(o.ei);
			if (!ent) {
				o.ei = 0;
				continue;
			}
			
			o.tr = core.get_ent(o.ei)->get_pos();
			ren.draw_inst(o.tr, o.clr, p_objs[o.oid].id);
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
