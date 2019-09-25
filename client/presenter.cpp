#include "game/game_core.hpp"
#include "game/level_gen.hpp"
#include "render/ren_aal.hpp"
#include "render/ren_imm.hpp"
#include "vaslib/vas_containers.hpp"
#include "vaslib/vas_log.hpp"
#include "presenter.hpp"



ECompRender::ECompRender(Entity* ent)
    : EComp(ent)
{
	send(PresCmdCreate{this});
}
void ECompRender::parts(ModelType model, ModelEffect effect, const ParticleBatchPars& pars)
{
	send(PresCmdObjEffect{this, model, effect, pars});
}
void ECompRender::parts(FreeEffect effect, const ParticleBatchPars& pars)
{
	send(PresCmdEffect{this, effect, pars});
}
void ECompRender::attach(AttachType type, Transform at, ModelType model, FColor clr)
{
	send(PresCmdAttach{this, type, model, clr, at});
}
void ECompRender::send(PresCommand c)
{
	if (_is_ok) GamePresenter::get()->add_cmd(std::move(c));
}
void ECompRender::proc(PresCommand)
{
	THROW_FMTSTR("ECompRender::proc() not implemented ({})", ent->dbg_id());
}
void ECompRender::on_destroy_ent()
{
	on_destroy();
	send(PresCmdDelete{this, _comp_id});
	_is_ok = false;
}



EC_RenderSimple::EC_RenderSimple(Entity* ent, ModelType model, FColor clr)
    : ECompRender(ent), model(model), clr(clr)
{}
void EC_RenderSimple::on_destroy()
{
	parts(model, ME_DEATH, {{}, 1, clr});
}
void EC_RenderSimple::step()
{
	RenAAL::get().draw_inst(get_pos(), clr, model);
}



EC_RenderBot::EC_RenderBot(Entity* ent, ModelType model, FColor clr)
    : ECompRender(ent), model(model), clr(clr)
{}
void EC_RenderBot::on_destroy()
{
	parts(model, ME_DEATH, {{}, 1, clr});
}
void EC_RenderBot::step()
{
	const Transform fixed{get_pos().pos, rot};
	RenAAL::get().draw_inst(fixed, clr, model);
	
	for (auto& a : atts) {
		if (a.model != MODEL_NONE)
			RenAAL::get().draw_inst(fixed.get_combined(a.at), a.clr, a.model);
	}
}
void EC_RenderBot::proc(PresCommand ac)
{
	if (auto c = std::get_if<PresCmdAttach>(&ac))
	{
		auto& a = atts[c->type];
		a.model = c->model;
		a.at = c->pos;
		a.clr = c->clr;
	}
	else THROW_FMTSTR("EC_RenderBot::proc() not implemented ({})", ent->dbg_id());
}



class GamePresenter_Impl : public GamePresenter
{
public:
	struct PartDelay
	{
		ParticleGroupGenerator* gen;
		ParticleBatchPars pars;
	};
	
	std::vector<PresCommand> cmds_queue;
	std::vector<std::pair<ECompRender*, PresCommand>> cmds;
	
	std::vector<PartDelay> part_del;
	std::vector<PresCmdDbgRect> dbg_rs;
	std::vector<PresCmdDbgLine> dbg_ls;
	
	SparseArray<ECompRender*> cs;
	TimeSpan last;
	
	
	
	GamePresenter_Impl(const InitParams& pars)
	{
		RenAAL::get().inst_begin();
		
		for (auto& l : pars.lvl->ls_grid) RenAAL::get().inst_add({l.first, l.second}, false, 0.07f, 1.5f);
		RenAAL::get().inst_add_end();
		
		for (auto& l : pars.lvl->ls_wall) RenAAL::get().inst_add(l, false);
		RenAAL::get().inst_add_end();
		
		try {ResBase::get().init_ren();}
		catch (std::exception& e) {
			THROW_FMTSTR("GamePresenter::init() ResBase failed: {}", e.what());
		}
		
		RenAAL::get().inst_end();
		RenAAL::get().draw_grid = true;
	}
	~GamePresenter_Impl()
	{
		RenAAL::get().draw_grid = false;
	}
	void sync()
	{
		dbg_rs.clear();
		dbg_ls.clear();
		
		for (auto& c : cmds_queue) std::visit(*this, c);
		cmds_queue.clear();
		
		for (auto& c : cs)
		{
			auto& phy = c->ent->get_phy();
			c->_pos = phy.get_trans();
			c->_vel = phy.get_vel();
			c->sync();
		}
	}
	void add_cmd(PresCommand c)
	{
		reserve_more_block(cmds_queue, 1024);
		cmds_queue.emplace_back(std::move(c));
	}
	void render(TimeSpan passed)
	{
		for (auto& c : cmds) c.first->proc( std::move(c.second) );
		cmds.clear();
		
		for (auto& d : part_del) d.gen->draw(d.pars);
		part_del.clear();
		
		last = passed;
		for (auto& c : cs)
		{
			c->step();
			c->_pos.add(c->_vel * passed.seconds());
		}
		
		RenImm::get().set_context(RenImm::DEFCTX_WORLD);
		for (auto& d : dbg_rs) RenImm::get().draw_rect(d.dst, d.clr);
		for (auto& d : dbg_ls) RenImm::get().draw_line(d.a, d.b, d.clr, d.wid);
	}
	TimeSpan get_passed()
	{
		return last;
	}
	
	
	
	template <typename T>
	void add_pd(T& cmd, ECompRender* ptr, ParticleGroupGenerator* gen)
	{
		if (!gen) return;
		auto& pd = part_del.emplace_back();
		pd.gen = gen;
		pd.pars = cmd.pars;
		if (ptr) pd.pars.tr = ptr->get_pos().get_combined(pd.pars.tr);
	}
	
	void operator()(PresCmdCreate& c)
	{
		if (c.ptr->ent->is_ok())
		{
			c.ptr->_comp_id = cs.emplace_new(c.ptr);
			c.ptr->_pos = c.ptr->ent->get_phy().get_trans();
		}
	}
	void operator()(PresCmdDelete& c)
	{
		if (c.index != size_t_inval)
			cs.free_and_reset(c.index);
		
		for (size_t i = 0; i < cmds.size(); )
		{
			if (cmds[i].first == c.ptr) cmds.erase( cmds.begin() + i );
			else ++i;
		}
	}
	void operator()(PresCmdObjEffect& c)
	{
		add_pd(c, c.ptr, ResBase::get().get_eff(c.model, c.eff));
	}
	void operator()(PresCmdEffect& c)
	{
		add_pd(c, c.ptr, ResBase::get().get_eff(c.eff));
	}
	void operator()(PresCmdDbgRect& c)
	{
		reserve_more_block(dbg_rs, 64);
		dbg_rs.emplace_back(c);
	}
	void operator()(PresCmdDbgLine& c)
	{
		reserve_more_block(dbg_ls, 64);
		dbg_ls.emplace_back(c);
	}
	void operator()(PresCmdAttach& c)
	{
		reserve_more_block(cmds, 128);
		cmds.emplace_back(c.ptr, std::move(c));
	}
};
void GamePresenter::effect(FreeEffect effect, const ParticleBatchPars &pars)
{
	add_cmd(PresCmdEffect{nullptr, effect, pars});
}
void GamePresenter::dbg_line(vec2fp a, vec2fp b, uint32_t clr, float wid)
{
	add_cmd(PresCmdDbgLine{a, b, clr, wid});
}
void GamePresenter::dbg_rect(Rectfp area, uint32_t clr)
{
	add_cmd(PresCmdDbgRect{area, clr});
}
void GamePresenter::dbg_rect(vec2fp ctr, uint32_t clr, float rad)
{
	dbg_rect(Rectfp::from_center(ctr, vec2fp::one(rad)), clr);
}



static GamePresenter* rni;
void GamePresenter::init(const InitParams& pars) {rni = new GamePresenter_Impl (pars);}
GamePresenter* GamePresenter::get() {return rni;}
GamePresenter::~GamePresenter() {rni = nullptr;}
