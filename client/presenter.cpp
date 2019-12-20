#include "game/game_core.hpp"
#include "game/level_gen.hpp"
#include "render/ren_aal.hpp"
#include "render/ren_imm.hpp"
#include "vaslib/vas_containers.hpp"
#include "vaslib/vas_log.hpp"
#include "effects.hpp"
#include "presenter.hpp"

#include <thread>
#include "render/camera.hpp"
#include "render/control.hpp"
#include "render/ren_text.hpp"
#include "utils/noise.hpp"
#include "utils/res_image.hpp"

void effects_init(); // defined in effects.cpp



ECompRender::ECompRender(Entity* ent)
    : EComp(ent)
{
	if (ent) _pos = ent->get_phy().get_trans();
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
	GamePresenter::get()->effect( FE_EXPLOSION_FRAG, { Transform{get_pos()} } );
	effect_explosion_wave( get_pos().pos );
}
void EC_RenderBot::step()
{
	if (!aequ(rot, rot_tar, 1e-3))
		rot = lerp_angle(rot, rot_tar, std::min(1., 10 * GamePresenter::get()->get_passed().seconds()));
	
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
	struct FloatTextRender
	{
		vec2fp at, dps;
		TextRenderInfo tri;
		float size;
		uint32_t clr;
		int base_a;
		float t, tps;
		
		FloatTextRender(FloatText&& ft)
		{
			at = ft.at;
			tri.str_a = ft.str.data();
			tri.build();
			size = ft.size;
			
			clr = ft.color & (~0xff);
			base_a = ft.color & 0xff;
			
			t = 1 + ft.show_len / ft.fade_len;
			tps = 1.f / std::max(0.1, ft.fade_len.seconds());
			
			dps.set(std::min(0.5, 1 / (ft.show_len + ft.fade_len).seconds()), 0);
			dps.fastrotate( rnd_stat().range_n2() * M_PI );
		}
	};
	
	std::vector<PresCommand> cmds_queue;
	std::vector<std::pair<ECompRender*, PresCommand>> cmds;
	
	std::vector<PartDelay> part_del;
	std::vector<std::function<bool(TimeSpan)>> ef_fs;
	
	std::vector<PresCmdDbgRect> dbg_rs;
	std::vector<PresCmdDbgLine> dbg_ls;
	std::vector<PresCmdDbgText> dbg_ts;
	std::vector<FloatTextRender> f_texts;
	
	SparseArray<ECompRender*> cs;
	TimeSpan last;
	float t_last;
	
	vec2fp vport_offset = {4, 3}; ///< Occlusion rect size offset
	std::optional<std::pair<int, ImageInfo>> dbg_sshot_img;
	
	
	
	GamePresenter_Impl(const InitParams& pars)
	{
		RenAAL::get().inst_begin( pars.lvl->cell_size );
		
		for (auto& l : pars.lvl->ls_grid) RenAAL::get().inst_add({l.first, l.second}, false, 0.07f, 1.5f);
		RenAAL::get().inst_add_end();
		
		for (auto& l : pars.lvl->ls_wall) RenAAL::get().inst_add(l, false);
		RenAAL::get().inst_add_end();
		
		try {ResBase::get().init_ren();}
		catch (std::exception& e) {
			THROW_FMTSTR("GamePresenter::init() ResBase failed: {}", e.what());
		}
		RenAAL::get().inst_end();
		
		effects_init();
	}
	void sync()
	{
		dbg_rs.clear();
		dbg_ls.clear();
		dbg_ts.clear();
		
		for (auto& c : cmds_queue) std::visit(*this, c);
		cmds_queue.clear();
		
		Rectfp vport = vport_rect();
		for (auto& c : cs)
		{
			auto& phy = c->ent->get_phy();
			
			c->_in_vport = c->disable_culling || vport.contains( phy.get_pos() );
			if (!c->_in_vport) continue;
			
			c->_pos = phy.get_trans();
			c->_vel = phy.get_vel();
			c->sync();
		}
		
		if (dbg_sshot_img)
		{
			if (dbg_sshot_img->first != 3) dbg_sshot_img->first |= 2;
			else {
				std::thread thr([](ImageInfo img)
				{
					img.convert(ImageInfo::FMT_RGB);
					img.vflip();
					std::string s = FMT_FORMAT("debug_{}.png", date_time_fn());
					img.save( s.data() );
					VLOGI("Saved sshot: {}", s);
				}
				, std::move(dbg_sshot_img->second));
				dbg_sshot_img = {};
				thr.detach();
			}
		}
		
		t_last = 0;
	}
	void del_sync()
	{
		for (int i=0; i < int(cmds_queue.size()); )
		{
			if (auto cmd = std::get_if<PresCmdDelete>(&cmds_queue[i]))
			{
				auto copy = *cmd;
				
				for (int j=0; j < int(cmds_queue.size()); )
				{
#define CHECK(TYPE)\
	if (auto cmd = std::get_if<TYPE>(&cmds_queue[j])) {\
		if (cmd->ptr == copy.ptr) {\
			(*this)(*cmd);\
			del = true; }}
					
					bool del = false;
					     CHECK(PresCmdObjEffect)
					else CHECK(PresCmdEffect)
#undef CHECK       
					if (!del) ++j;
					else {
						cmds_queue.erase( cmds_queue.begin() + j );
						if (i > j) --i;
					}
				}
				
				(*this)(copy);
				cmds_queue.erase( cmds_queue.begin() + i );
			}
			else ++i;
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
		t_last += passed / GameCore::step_len;
		for (auto& c : cs)
		{
			if (!c->_in_vport) continue;
			c->step();
			c->_pos.add(c->_vel * passed.seconds());
		}
		
		RenImm::get().set_context(RenImm::DEFCTX_WORLD);
		for (auto& d : dbg_rs) RenImm::get().draw_rect(d.dst, d.clr);
		for (auto& d : dbg_ls) RenImm::get().draw_line(d.a, d.b, d.clr, d.wid);
		
		float text_k = 1.f / RenderControl::get().get_world_camera().get_state().mag;
		for (auto& d : dbg_ts) RenImm::get().draw_text(d.at, d.str, d.clr, false, text_k);
		
		for (auto i = ef_fs.begin(); i != ef_fs.end(); )
		{
			if ((*i)(passed)) ++i;
			else i = ef_fs.erase(i);
		}
		
		for (auto it = f_texts.begin(); it != f_texts.end(); )
		{
			uint32_t clr = it->clr;
			clr |= std::min(it->base_a, int_round(it->base_a * it->t));
			RenImm::get().draw_text(it->at, it->tri, clr, true, text_k * it->size);
			
			it->at += it->dps * passed.seconds();
			it->t  -= it->tps * passed.seconds();
			
			if (it->t > 0) ++it;
			else it = f_texts.erase(it);
		}
		
		if (dbg_sshot_img && dbg_sshot_img->first == 2)
		{
			dbg_sshot_img->first |= 1;
			if (RenderControl::get().img_screenshot) VLOGW("GamePresenter::dbg_screenshot() ignored");
			else RenderControl::get().img_screenshot = &dbg_sshot_img->second;
		}
	}
	TimeSpan get_passed()
	{
		return last;
	}
	float get_time_t()
	{
		return t_last;
	}
	void dbg_screenshot()
	{
		dbg_sshot_img = {0, {}};
	}
	
	
	
	Rectfp vport_rect()
	{
		auto& cam = RenderControl::get().get_world_camera();
		return Rectfp::from_center( cam.get_state().pos, (cam.coord_size() /2) + vport_offset );
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
	void operator()(PresCmdDbgText& c)
	{
		reserve_more_block(dbg_ts, 64);
		dbg_ts.emplace_back(std::move(c));
	}
	void operator()(PresCmdEffectFunc& c)
	{
		reserve_more_block(ef_fs, 128);
		ef_fs.emplace_back(std::move(c.eff));
	}
	void operator()(FloatText& c)
	{
		reserve_more_block(f_texts, 128);
		f_texts.emplace_back(std::move(c));
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
void GamePresenter::dbg_text(vec2fp at, std::string str, uint32_t clr)
{
	add_cmd(PresCmdDbgText{at, std::move(str), clr});
}
void GamePresenter::add_effect(std::function<bool(TimeSpan passed)> eff)
{
	if (eff) add_cmd(PresCmdEffectFunc{std::move(eff)});
}
void GamePresenter::add_float_text(FloatText text)
{
	add_cmd(std::move(text));
}



static GamePresenter* rni;
GamePresenter* GamePresenter::init(const InitParams& pars) {return rni = new GamePresenter_Impl (pars);}
GamePresenter* GamePresenter::get() {return rni;}
GamePresenter::~GamePresenter() {rni = nullptr;}
