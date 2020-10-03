#include "core/settings.hpp"
#include "game/game_core.hpp"
#include "game/level_gen.hpp"
#include "render/ren_aal.hpp"
#include "render/ren_imm.hpp"
#include "render/ren_light.hpp"
#include "vaslib/vas_containers.hpp"
#include "vaslib/vas_log.hpp"
#include "ec_render.hpp"
#include "presenter.hpp"

// for screenshot & debug
#include <thread>
#include "core/vig.hpp"
#include "render/camera.hpp"
#include "render/control.hpp"
#include "render/ren_text.hpp"
#include "utils/noise.hpp"
#include "utils/res_image.hpp"



struct PresCmdDbgRect {
	Rectfp dst;
	uint32_t clr;
};
struct PresCmdDbgLine {
	vec2fp a, b;
	uint32_t clr;
	float wid;
};
struct PresCmdDbgText {
	vec2fp at;
	std::string str;
	uint32_t clr;
};
struct PresCmdEffectFunc {
	std::unique_ptr<GameRenderEffect> eff;
};



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
		
		FloatTextRender(FloatText&& ft) noexcept
		{
			at = ft.at;
			tri.str_a = ft.str.data();
			tri.length = ft.str.length();
			tri.build();
			size = ft.size;
			
			clr = ft.color & (~0xff);
			base_a = ft.color & 0xff;
			
			t = 1 + ft.show_len / ft.fade_len;
			tps = 1.f / std::max(0.1, ft.fade_len.seconds());
			
			dps.set(std::min(0.5, 1 / (ft.show_len + ft.fade_len).seconds()), 0);
			dps.fastrotate( rnd_stat().range_n2() * M_PI );
			dps *= ft.spread_strength;
		}
	};
	
	GameCore* core;
	const LevelTerrain* terrain;
	std::vector<PresCommand> cmds_queue;
	std::vector<PartDelay> part_del;
	
	std::vector<PresCmdDbgRect> dbg_rs, dbg_rs_new;
	std::vector<PresCmdDbgLine> dbg_ls, dbg_ls_new;
	std::vector<PresCmdDbgText> dbg_ts, dbg_ts_new;
	SparseArray<std::unique_ptr<GameRenderEffect>> ef_fs;
	std::vector<FloatTextRender> f_texts;
	
	struct EntReg
	{
		EC_RenderPos* pos = {};
		std::vector<EC_RenderComp*> subs;
	};
	
	std::vector<EntReg> regs;
	TimeSpan last_passed;
	TimeSpan prev_frame;
	
	vec2fp vport_offset = {10, 10}; ///< Occlusion rect size offset
	std::optional<std::pair<int, ImageInfo>> dbg_sshot_img;
	
	int max_interp_frames = 0;
	RAII_Guard menu_g;
	
	
	
	GamePresenter_Impl(const InitParams& pars)
	{
		core = pars.core;
		terrain = pars.lvl;
		reinit_resources(*terrain);
		
		menu_g = vig_reg_menu(VigMenu::DebugRenderer, [this]{
			vig_label_a("Interp frames (max): {}\n", max_interp_frames);
		});
	}
	void sync(TimeSpan now) override
	{
		dbg_rs.clear(); dbg_rs.swap(dbg_rs_new);
		dbg_ls.clear(); dbg_ls.swap(dbg_ls_new);
		dbg_ts.clear(); dbg_ts.swap(dbg_ts_new);
		
		//
		
		TimeSpan frame_time = now + GameCore::step_len;
		int interp_dep = AppSettings::get().interp_depth;
		
		Rectfp vport = vport_rect();
		for (auto& ei : regs)
		{
			if (!ei.pos) continue;
			
			EC_RenderPos& c = *ei.pos;
			Transform tr = c.ent.ref_pc().get_trans();
			
			bool was_vp = c.in_vport;
			c.in_vport = c.disable_culling || vport.contains( tr.pos );
			if (!c.in_vport) continue;
			
			if (interp_dep)
			{
				if (!was_vp || playback_hack) {
					c.pos = tr;
					c.fn = 0;
				}
				else {
					if (!c.fn) c.fs[c.fn++] = { prev_frame, c.pos };
					if (c.fs[c.fn-1].first >= frame_time) {
						c.fs[c.fn-1].second = tr;
					}
					else {
						if (c.fn >= interp_dep) c.fn = interp_dep - 1;
						c.fs[c.fn++] = { frame_time, tr };
					}
				}
			}
			else {
				c.pos = tr;
				c.vel = c.ent.ref_pc().get_vel();
			}
			
			if (!was_vp) {
				for (auto& sub : ei.subs)
					sub->on_vport_enter();
			}
		}
		
		//
		
		for (auto& c : cmds_queue)
		{
			std::visit(overloaded{
				[&](PresCmdParticles& c)
				{
					if (!c.gen) return;

					auto& pd = part_del.emplace_back();
					pd.gen = c.gen;
					pd.pars = c.pars;

					if (!c.eid) {}
					else if (auto e = getreg(c.eid))
						pd.pars.tr = e->pos->get_cur().get_combined(pd.pars.tr);
					else if (auto e = core->get_ent(c.eid))
						pd.pars.tr = e->ref_pc().get_trans().get_combined(pd.pars.tr);
				}
			}, c);
		}
		cmds_queue.clear();
		
		//
		
		if (dbg_sshot_img)
		{
			if (dbg_sshot_img->first != 3) dbg_sshot_img->first |= 2;
			else {
				std::thread thr([](ImageInfo img)
				{
					set_this_thread_name("screenshot");
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
	}
	void add_cmd(PresCommand c) override
	{
		if (loadgame_hack) return;
		reserve_more_block(cmds_queue, 256);
		cmds_queue.emplace_back(std::move(c));
	}
	void render(TimeSpan now, TimeSpan passed) override
	{
		for (auto& d : part_del) d.gen->draw(d.pars);
		part_del.clear();
		
		last_passed = passed;
		
		//
		
		int interp_dep = AppSettings::get().interp_depth;
		if (interp_dep > 2)
			now -= GameCore::step_len; // +1 interp step (2 steps lag total)
		
		prev_frame = now;
		max_interp_frames = 0;
		
		for (auto& ei : regs)
		{
			if (!ei.pos) continue;
			
			EC_RenderPos& c = *ei.pos;
			if (!c.is_visible_now()) continue;
			
			if (interp_dep)
			{
				while (c.fn >= 2)
				{
					float t = (now - c.fs[0].first) / (c.fs[1].first - c.fs[0].first);
					if (t < 0 || t > 1) {
						c.pos = c.fs[0].second;
						--c.fn;
						for (int i=0; i<c.fn; ++i) c.fs[i] = c.fs[i+1];
						continue;
					}
					c.pos = lerp(c.fs[0].second, c.fs[1].second, t);
					break;
				}
			}
			max_interp_frames = std::max(max_interp_frames, c.fn);
			
			if (!c.immediate_rotation) {
				for (auto& sub : ei.subs)
					sub->render(c, passed);
			}
			else {
				Transform orig {c.pos};
				Transform imr {c.pos.pos, c.ent.ref_pc().get_angle()};
				for (auto& sub : ei.subs) {
					c.pos = sub->allow_immediate_rotation() ? imr : orig;
					sub->render(c, passed);
				}
				c.pos = orig;
			}
			
			if (!interp_dep)
				c.pos.pos += c.vel * passed.seconds();
		}
		
		//
		
		for (auto i = ef_fs.begin(); i != ef_fs.end(); ++i)
		{
			if ((*i)->is_first) {
				(*i)->is_first = false;
				(*i)->init();
			}
			if (!(*i)->render(passed))
				ef_fs.free_and_reset(i.index());
		}
		
		for (auto it = f_texts.begin(); it != f_texts.end(); )
		{
			uint32_t clr = it->clr;
			clr |= std::min(it->base_a, int_round(it->base_a * it->t));
			RenImm::get().draw_text(it->at, it->tri, clr, true, it->size);
			
			it->at += it->dps * passed.seconds();
			it->t  -= it->tps * passed.seconds();
			
			if (it->t > 0) ++it;
			else it = f_texts.erase(it);
		}
		
		for (auto& d : dbg_rs) RenImm::get().draw_rect(d.dst, d.clr);
		for (auto& d : dbg_ls) RenImm::get().draw_line(d.a, d.b, d.clr, d.wid);
		
		for (auto& d : dbg_ts) RenImm::get().draw_text(d.at, d.str, d.clr);
		
		if (dbg_sshot_img && dbg_sshot_img->first == 2)
		{
			dbg_sshot_img->first |= 1;
			if (RenderControl::get().img_screenshot) VLOGW("GamePresenter::dbg_screenshot() ignored");
			else RenderControl::get().img_screenshot = &dbg_sshot_img->second;
		}
	}
	
	TimeSpan get_passed() override {return last_passed;}
	Rectfp   get_vport()  override {return vport_rect();}
	
	void dbg_screenshot() override
	{
		dbg_sshot_img = {0, {}};
	}
	
	void reinit_resources(const LevelTerrain& lvl)
	{
		RenderControl::get().exec_task([&]{
			RenLight::get().gen_wall_mask(lvl);
			RenAAL::get().inst_begin( GameConst::cell_size );
		});
		
		float kw_grid = 0.07, ka_grid = 1.5;
		float kw_wall = 0.1, ka_wall = 3;
		
		for (auto& l : lvl.ls_grid) RenAAL::get().inst_add({l.first, l.second}, false, kw_grid, ka_grid);
		RenAAL::get().inst_add_end();
		
		for (auto& l : lvl.ls_wall) RenAAL::get().inst_add(l, false, kw_wall, ka_wall);
		RenAAL::get().inst_add_end();
		
		try {ResBase::get().init_ren_wait();}
		catch (std::exception& e) {
			THROW_FMTSTR("GamePresenter::init() ResBase failed: {}", e.what());
		}
		
		RenderControl::get().exec_task([]
		{
			RenAAL::get().inst_end();
		});
		
		// remove all references to particle generators (invalidated)
		part_del.clear();
		for (auto& cmd : cmds_queue)
		{
			if (auto c = std::get_if<PresCmdParticles>(&cmd))
				c->gen = nullptr;
		}
		for (auto& ei : regs)
		{
			if (!ei.pos) continue;
			for (auto& s : ei.subs)
			{
				if (auto c = dynamic_cast<EC_ParticleEmitter*>(s)) {
					c->resource_reinit_hack();
					break;
				}
			}
		}
	}
	
	
	
	Rectfp vport_rect()
	{
		auto& cam = RenderControl::get().get_world_camera();
		return Rectfp::from_center( cam.get_state().pos, (cam.coord_size() /2) + vport_offset );
	}
	
	
	
	void dbg_line(vec2fp a, vec2fp b, uint32_t clr, float wid) override
	{
		reserve_more_block(dbg_ls_new, 128);
		dbg_ls_new.emplace_back(PresCmdDbgLine{ a, b, clr, wid });
	}
	void dbg_rect(Rectfp area, uint32_t clr) override
	{
		reserve_more_block(dbg_rs_new, 128);
		dbg_rs_new.emplace_back(PresCmdDbgRect{ area, clr });
	}
	void dbg_rect(vec2fp ctr, uint32_t clr, float rad) override
	{
		dbg_rect(Rectfp::from_center(ctr, vec2fp::one(rad)), clr);
	}
	void dbg_text(vec2fp at, std::string str, uint32_t clr) override
	{
		reserve_more_block(dbg_ts_new, 128);
		dbg_ts_new.emplace_back(PresCmdDbgText{ at, std::move(str), clr });
	}
	void add_effect(std::unique_ptr<GameRenderEffect> c) override
	{
		if (loadgame_hack) return;
//		reserve_more_block(ef_fs, 128);
		ef_fs.emplace_new(std::move(c));
	}
	void add_float_text(FloatText c) override
	{
		if (loadgame_hack) return;
		reserve_more_block(f_texts, 128);
		f_texts.emplace_back(std::move(c));
	}
	
	
	
	EntReg* getreg(EntityIndex eid, bool is_new = false)
	{
		size_t i = eid.to_int();
		if (is_new) {
			if (regs.size() <= i) regs.resize(i + 1);
		}
		else if (i >= regs.size() || !regs[i].pos) return nullptr;
		return &regs[i];
	}
	void on_add(EC_RenderPos& c) override
	{
		auto p = getreg(c.ent.index, true);
		p->pos = &c;
		p->subs.clear();
	}
	void on_rem(EC_RenderPos& pc) override
	{
		getreg(pc.ent.index)->pos = {};
		Transform pos = pc.newest();
		
		for (auto& cmd : cmds_queue)
		{
			if (auto c = std::get_if<PresCmdParticles>(&cmd);
			    c && c->eid == pc.ent.index)
			{
				auto& pd = part_del.emplace_back();
				pd.gen = c->gen;
				pd.pars = c->pars;
				pd.pars.tr = pos.get_combined(c->pars.tr);
				c->gen = nullptr; // ignore
			}
		}
	}
	void on_add(EC_RenderComp& c) override
	{
		if (auto p = getreg(c.ent.index))
			p->subs.push_back(&c);
	}
	void on_rem(EC_RenderComp& c) override
	{
		if (auto p = getreg(c.ent.index))
			erase_if_find(p->subs, [&](auto& v){ return v == &c; });
	}
};
void GamePresenter::effect(PGG_Pointer pgg, const ParticleBatchPars& pars)
{
	if (pgg) {
		add_cmd(PresCmdParticles{ {}, pgg.p, pars });
		if (net_writer)
			net_writer->on_pgg(pgg, pars);
	}
}



static GamePresenter* rni;
GamePresenter* GamePresenter::init(const InitParams& pars) {return rni = new GamePresenter_Impl (pars);}
GamePresenter* GamePresenter::get() {return rni;}
GamePresenter::~GamePresenter() {rni = nullptr;}
