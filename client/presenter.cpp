#include "game/game_core.hpp"
#include "render/ren_aal.hpp"
#include "render/ren_imm.hpp"
#include "vaslib/vas_containers.hpp"
#include "vaslib/vas_log.hpp"
#include "presenter.hpp"



ECompRender::ECompRender(Entity* ent):
    EComp(ent)
{
	PresCommand c;
	c.type = PresCommand::T_CREATE;
	send(c);
}
ECompRender::~ECompRender()
{
	PresCommand c;
	c.type = PresCommand::T_DEL;
	c.ix0 = _comp_id;
	send(c);
}
void ECompRender::parts(ModelType model, ModelEffect effect, const ParticleGroupGenerator::BatchPars& pars)
{
	PresCommand c;
	c.type = PresCommand::T_OBJPARTS;
	c.ix0 = model;
	c.ix1 = effect;
	c.set(pars);
	send(c);
}
void ECompRender::parts(FreeEffect effect, ParticleGroupGenerator::BatchPars pars)
{
	pars.tr = get_pos().get_combined(pars.tr);
	GamePresenter::get()->effect(effect, pars);
}
void ECompRender::attach(AttachType type, Transform at, ModelType model, FColor clr)
{
	PresCommand c;
	c.type = PresCommand::T_ATTACH;
	c.ix0 = type;
	c.ix1 = model;
	c.pos = at;
	c.clr = clr;
	send(c);
}
void ECompRender::send(PresCommand& c)
{
	c.ptr = this;
	GamePresenter::get()->add_cmd(c);
}
void ECompRender::proc(const PresCommand& )
{
	THROW_FMTSTR("ECompRender::proc() not implemented ({})", ent->dbg_id());
}



EC_RenderSimple::EC_RenderSimple(Entity* ent, ModelType model, FColor clr):
    ECompRender(ent), model(model), clr(clr)
{}
EC_RenderSimple::~EC_RenderSimple()
{
	parts(model, ME_DEATH, {get_pos(), 1, clr});
}
void EC_RenderSimple::step()
{
	RenAAL::get().draw_inst(get_pos(), clr, model);	
}



void PresCommand::set(const ParticleGroupGenerator::BatchPars& pars)
{
	pos = pars.tr;
	power = pars.power;
	clr = pars.clr;
}
ParticleGroupGenerator::BatchPars PresCommand::get_pp()
{
	ParticleGroupGenerator::BatchPars pars;
	pars.tr = pos;
	pars.power = power;
	pars.clr = clr;
	return pars;
}



class GamePresenter_Impl : public GamePresenter
{
public:
	std::vector<PresCommand> cmds_queue;
	std::vector<std::vector<PresCommand>> cmds;
	
	SparseArray<ECompRender*> cs;
	TimeSpan last;
	
	
	
	GamePresenter_Impl(InitParams pars)
	{
		RenAAL::get().inst_begin();
		
		for (auto& l : pars.grid_lines) RenAAL::get().inst_add(l, false, 0.07f, 1.5f);
		RenAAL::get().inst_add_end();
		
		for (auto& l : pars.lvl_lines) RenAAL::get().inst_add(l, false);
		RenAAL::get().inst_add_end();
		
		try {ResBase::get().init_ren();}
		catch (std::exception& e) {
			THROW_FMTSTR("GamePresenter::init() ResBase failed: {}", e.what());
		}
		
		RenAAL::get().inst_end();
	}
	void sync()
	{
		bool any = false;
		for (auto& c : cmds_queue)
		{
			switch (c.type)
			{
			case PresCommand::T_ERROR:
				throw std::logic_error("GamePresenter::sync() no type");
				
			case PresCommand::T_CREATE:
				if (c.ptr->ent->is_ok())
					c.ptr->_comp_id = cs.emplace_new(c.ptr);
				break;
				
			case PresCommand::T_DEL:
				cs.free_and_null(c.ix0);
				break;
				
			case PresCommand::T_ATTACH:
			case PresCommand::T_OBJPARTS:
			case PresCommand::T_FREEPARTS:
				any = true;
				break;
			}
		}
		
		if (any) cmds.emplace_back().swap(cmds_queue);
		else cmds_queue.clear();
		
		for (auto& c : cs)
		{
			auto& phy = c->ent->get_phy();
			c->_pos = phy.get_trans();
			c->_vel = phy.get_vel();
			c->sync();
		}
	}
	void add_cmd(const PresCommand& c)
	{
		reserve_more_block(cmds_queue, 1024);
		cmds_queue.emplace_back(c);
	}
	void render(TimeSpan passed)
	{
		for (auto& cline : cmds)
		for (auto& c : cline)
		{
			switch (c.type)
			{
			case PresCommand::T_ERROR:
			case PresCommand::T_CREATE:
			case PresCommand::T_DEL:
				break;
				
			case PresCommand::T_ATTACH:
				c.ptr->proc(c);
				break;
				
			case PresCommand::T_OBJPARTS:
				if (auto gen = ResBase::get().get_eff(static_cast<ModelType>(c.ix0), static_cast<ModelEffect>(c.ix1)))
					gen->draw(c.get_pp());
				break;
				
			case PresCommand::T_FREEPARTS:
				if (auto gen = ResBase::get().get_eff(static_cast<FreeEffect>(c.ix0)))
					gen->draw(c.get_pp());
				break;
			}
		}
		cmds.clear();
		
		last = passed;
		for (auto& c : cs)
		{
			c->step();
			c->_pos.add(c->_vel * passed.seconds());
		}
	}
	TimeSpan get_passed()
	{
		return last;
	}
};
void GamePresenter::effect(FreeEffect effect, const ParticleGroupGenerator::BatchPars &pars)
{
	PresCommand c;
	c.type = PresCommand::T_FREEPARTS;
	c.ix0 = effect;
	c.set(pars);
	add_cmd(c);
}



static GamePresenter* rni;
void GamePresenter::init(InitParams pars) {rni = new GamePresenter_Impl (std::move(pars));}
GamePresenter* GamePresenter::get() {return rni;}
GamePresenter::~GamePresenter() {rni = nullptr;}
