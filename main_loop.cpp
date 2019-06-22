#include <mutex>
#include <thread>
#include "core/tui_layer.hpp"
#include "game/game_core.hpp"
#include "game/physics.hpp"
#include "game/presenter.hpp"
#include "render/camera.hpp"
#include "render/control.hpp"
#include "render/particles.hpp"
#include "render/ren_aal.hpp"
#include "render/ren_imm.hpp"
#include "utils/noise.hpp"
#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_log.hpp"
#include "main_loop.hpp"



class ML_Rentest : public MainLoop
{
public:
	class L : public TUI_Layer
	{
	public:
		struct Head {
			int x, y, clr;
			TimeSpan cou, per;
		};
		std::array<Head, 35> hs;
		TimeSpan prev;
		
		Field y0, y1;
		int fd_w = 8;
		ML_Rentest* mren;
		
		L(ML_Rentest* mren): mren(mren) {
			transparent = false;
			y0 = mk_field({1,1,fd_w,1});
			y1 = mk_field({1,3,fd_w,1});
			
			for (auto& h : hs) init(h);
			
			auto t = TimeSpan::seconds(80);
			size_t n = 100;
			for (size_t i=0; i<n; ++i) proc(t*(1.f/n));
			
			prev = TimeSpan::since_start();
		}
		void on_event(const SDL_Event& ev) {
			if (ev.type == SDL_MOUSEBUTTONDOWN) {
				mren->lr = nullptr;
				delete this;
			}
		}
		void render()
		{
			auto next = TimeSpan::since_start();
			TimeSpan passed = next - prev;
			prev = next;
			proc(passed * 2);
			
			auto ch = TUI_Char::none();
			ch.fore = TUI_SET_TEXT;
			ch.back = TUI_BLACK;
			ch.alpha = 1.f;
			sur.change_rect({0,0,fd_w+1,4}, ch);
			
			float t = float(hs[0].y + 1) / sur.size.y;
			y0.set_bar(t);
			y1.set(FMT_FORMAT("XPos: {}", hs[0].x), 1);
			
			TUI_BoxdrawHelper box;
			box.init(sur);
			box.box({0,0,fd_w+1,2});
			box.box({0,2,fd_w+1,2});
			box.submit();
		}
		void proc(TimeSpan passed)
		{
			sur.upd_any = true;
			for (auto& c : sur.cs) {
				if (c.alpha > 0)
					c.alpha -= 0.17 * passed.seconds();
			}
			
			for (auto& h : hs) {
				h.cou += passed;
				while (h.cou >= h.per) {
					if (++h.y == sur.size.y) {
						init(h);
						continue;
					}
					h.cou -= h.per;
					char32_t sym = 33 + rand() % (126 - 33);
					sur.set({h.x, h.y}, {sym, h.clr});
				}
			}
		}
		void init(Head& h) {
			h.x = rand() % sur.size.x;
			h.y = -1;
			h.clr = rand() % 8;
			h.cou = {};
			h.per = TimeSpan::seconds( 0.1f * (1 + rand() % 8) );
		}
	};
	L* lr;
	
	ML_Rentest()
	{
		lr = new L(this);
		lr->bring_to_top();
	}
	void on_event(SDL_Event& ev)
	{
		if (ev.type == SDL_KEYDOWN)
		{
			auto& ks = ev.key.keysym;
			if (ks.scancode == SDL_SCANCODE_1)
			{
				ParticleGroupStd ptg;
				ptg.px_radius = 4.f; // default
				ptg.count = 1000;
				ptg.radius = 60;
				ptg.colors_range[0] = 192; ptg.colors_range[3] = 255;
				ptg.colors_range[1] = 192; ptg.colors_range[4] = 255;
				ptg.colors_range[2] = 192; ptg.colors_range[5] = 255;
				ptg.alpha = 255;
				ptg.speed_min = 60; ptg.speed_max = 200; // 200, 600
				ptg.TTL.ms(2000), ptg.FT.ms(1000);
				ptg.TTL_max = ptg.TTL + TimeSpan::ms(500), ptg.FT_max = ptg.FT + TimeSpan::ms(1000);
				ptg.draw({});
			}
		}
	}
	void render(TimeSpan passed)
	{
		static float r = 0;
		RenAAL::get().draw_line({0, 0}, vec2fp(400, 0).get_rotated(r), 0x40ff40ff, 5.f, 60.f, 2.f);
		r += M_PI * 0.5 * passed.seconds();
		
		RenAAL::get().draw_line({-400,  200}, {-220, -200}, 0x4040ffff, 5.f, 60.f);
		RenAAL::get().draw_line({-400, -200}, {-220,  200}, 0xff0000ff, 5.f, 60.f);
		
		RenImm::get().set_context(RenImm::DEFCTX_WORLD);
		RenImm::get().draw_frame({-200, -200, 400, 400}, 0xff0000ff, 3);
		
		RenAAL::get().draw_line({-200, -200}, {200, 200}, 0x00ff80ff, 5.f, 12.f);
		RenAAL::get().draw_chain({{-200, -200}, {50, 50}, {200, -50}}, true, 0x80ff80ff, 8.f, 3.f);
		RenAAL::get().draw_line({-300, 0}, {-300, 0}, 0xffc080ff, 8.f, 20.f);
		
		RenAAL::get().draw_line({250, -200}, {250, 200}, 0xffc080ff, 8.f, 8.f);
		RenAAL::get().draw_line({325, -200}, {325, 200}, 0xffc080ff, 16.f, 3.f);
		RenAAL::get().draw_line({400, -200}, {400, 200}, 0xffc080ff, 5.f, 30.f);
		
		RenAAL::get().draw_line({-440,  200}, {-260, -200}, 0x6060ffff, 5.f, 60.f, 1.7f);
		RenAAL::get().draw_line({-440, -200}, {-260,  200}, 0xff2020ff, 5.f, 60.f, 1.7f);
		
		if (lr) {
			RenImm::get().set_context(RenImm::DEFCTX_UI);
			RenImm::get().draw_rect({0,0,400,200}, 0xff0000ff);
		}
	}
};



enum ObjEffect
{
	OE_DEATH,
	OE_DUST
};
enum FreeEffect
{
	FE_EXPLOSION,
	FE_HIT
};
enum ObjList
{
	OBJ_PC,
	OBJ_BOX,
	OBJ_HEAVY,
	PROJ_ROCKET,
//	PROJ_RAY,
//	PROJ_BULLET,
	PROJ_PLASMA,
	ARM_SHIELD,
	ARM_ROCKET,
//	ARM_MGUN,
//	ARM_RAYGUN,
	ARM_PLASMA,
	
	OBJ_ALL_WALLS
};

class ML_Game : public MainLoop
{
public:
	enum State
	{
		ST_LOADING,
		ST_RUN,
		ST_FINISH
	};
	
	std::unique_ptr<GamePresenter> pres;
	
	std::thread thr_core;
	State state = ST_LOADING;
	
	std::mutex pc_lock;
	Camera* cam;
	EntityIndex cam_ent = 0;
	
	struct PC_Logic;
	
	std::unique_ptr<GameCore> core;
	PC_Logic* pc_log; // player character
	
	bool ph_debug_draw = false;
	
	
	
	ML_Game()
	{
		cam = RenderControl::get().get_world_camera();
		Camera::Frame cf = cam->get_state();
		cf.mag = 18.f;
		cam->set_state(cf);
		
		pres.reset( &GamePresenter::get() );
		init_game();
		thr_core = std::thread([this](){thr_func();});
	}
	~ML_Game()
	{
		state = ST_FINISH;
		thr_core.join();
	}
	void on_event(SDL_Event& ev)
	{
		if (state == ST_FINISH)
		{
			if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
				delete this;
			return;
		}
		if (state != ST_RUN) return;
		
		if (ev.type == SDL_KEYUP) {
			int k = ev.key.keysym.scancode;
			if (k == SDL_SCANCODE_0) ph_debug_draw = !ph_debug_draw;
		}
		
		std::unique_lock g(pc_lock);
		pc_log->on_event(ev);
	}
	void render(TimeSpan passed)
	{
		if		(state == ST_LOADING) RenImm::get().draw_text({}, "Loading...", -1, false, 4.f);
		else if (state == ST_FINISH)  RenImm::get().draw_text({}, "Game finished. Press ESC to exit.", -1, false, 4.f);
		else {
			std::unique_lock g(pc_lock);
//			if (auto e = core->get_ent(cam_ent)) cam_follow(e); else cam_ent = 0;
			GamePresenter::get().render(passed);
			
			if (ph_debug_draw) {
				RenImm::get().set_context(RenImm::DEFCTX_WORLD);
				core->get_phy().world.DrawDebugData();
			}
		}
	}
	
	
	
	struct PC_Logic : EC_Logic
	{
		ML_Game* ml_game;
		
		static const int key_n = 5;
		bool keyf[key_n] = {};
		
		PC_Logic(ML_Game* ml_game, Entity* ent_): ml_game(ml_game) {
			ent = ent_;
		}
		~PC_Logic() {
			ml_game->state = ST_FINISH;
			ml_game->pc_log = nullptr;
		}
		void tick() {
			const vec2fp kmv[key_n] = {{-1, 0}, {0, -1}, {0, 1}, {1, 0}};
			vec2fp mv = {};
			bool mv_any = false;
			for (int i=0; i<4; ++i) if (keyf[i]) mv += kmv[i], mv_any = true;
			if (mv_any) mv.norm();
			
			b2Body* body = ent->get_phy()->body;
			const float tmul = ent->core.step_len.seconds();
			
			const b2Vec2 vel = body->GetLinearVelocity();
			b2Vec2 damp = -1 * vel;
			if (keyf[4]) damp *= 2;
			
			const float min_damp = keyf[4]? 0.5f : 0.2f;
			const float max_damp = keyf[4]? 9.f : 6.f;
			float dl = damp.Length();
			if (dl > max_damp) damp *= max_damp / dl;
			else if (dl != 0.f && dl < min_damp) damp *= min_damp / dl;
			if (!mv_any && dl == 0.f) return;
			
			b2Vec2 mv_vec = 8.f * conv(mv);
			if (fabs(mv_vec.x) > 0) damp.x = 0;
			if (fabs(mv_vec.y) > 0) damp.y = 0;
			
			b2Vec2 imp = tmul * (mv_vec + damp);
			body->ApplyLinearImpulse(body->GetMass() * imp, body->GetWorldCenter(), mv_any);
		}
		void on_event(SDL_Event& ev) {
			if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
				const SDL_Scancode cs[key_n] = {
				    SDL_SCANCODE_A, SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_SPACE
				};
				for (int i=0; i<key_n; ++i)
					if (cs[i] == ev.key.keysym.scancode)
						keyf[i] = (ev.type == SDL_KEYDOWN);
			}
		}
	};
	
	const float hsize = 15.f; // level
	const float hsz_box = 0.4;
	const float hsz_heavy = 1.5;
	const float hsz_rat = 0.5;
	const float hsz_proj = 0.2;
	const vec2fp hsz_shld = {2, 0.7};
	
	void init_game()
	{
		GameCore::InitParams core_init;
		core_init.random_seed = 0;
		core.reset( GameCore::create(core_init) );
		
		RenAAL::get().inst_begin();
		init_resources();
		init_terrain(hsize);
		RenAAL::get().inst_end();
	}
	void thr_func() try
	{
		for (int i=0; i<8; ++i)
		{
			bool big = (i%4 == 0);
			float rn = hsize - 1;
			vec2fp pos(rnd_range(-rn, rn), rnd_range(-rn, rn));
			
			auto e = core->create_ent();
			e->cnew(new EC_Render(e, big? OBJ_HEAVY : OBJ_BOX));
			
			b2BodyDef bd;
			bd.type = b2_dynamicBody;
			bd.position = conv(pos);
			e->cnew(new EC_Physics(bd));
			
			b2FixtureDef fd;
			fd.friction = 0.1;
			fd.restitution = big? 0.1 : 0.5;
			e->get_phy()->add_box(fd, vec2fp::one(big? hsz_heavy : hsz_box), big? 25.f : 8.f);
			
			struct DL : EC_Logic {
				void tick() {
					b2Body* body = ent->get_phy()->body;
					const float tmul = ent->core.step_len.seconds();
//					const float fk = body->GetFixtureList()->GetFriction();
					
					const b2Vec2 vel = body->GetLinearVelocity();
					b2Vec2 damp = -0.99f * vel;
					
					const float max_damp = 2.f;
					if (float l = damp.Length(); l > max_damp)
						damp *= max_damp / l;
					
					b2Vec2 imp = tmul * damp;
					body->ApplyLinearImpulse(body->GetMass() * imp, body->GetWorldCenter(), false);
					
					float ai = -2.f * body->GetAngularVelocity();
					ai *= tmul;
					body->ApplyAngularImpulse(body->GetMass() * ai, false);
				}
			};
			e->logic.reset(new DL);
			e->logic->ent = e;
			
			e->dbg_name = big? "Heavy" : "Box";
		}
		{
			auto e = core->create_ent();
			e->cnew(new EC_Render(e, OBJ_PC));
			
			b2BodyDef bd;
			bd.type = b2_dynamicBody;
			bd.position = {0, 0};
bd.fixedRotation = true;
			e->cnew(new EC_Physics(bd));
			
			b2FixtureDef fd;
			fd.friction = 0.3;
			fd.restitution = 0.5;
			e->get_phy()->add_circle(fd, hsz_rat, 12.f);
			
			pc_log = new PC_Logic(this, e);
			e->logic.reset(pc_log);
			
			e->dbg_name = "PC";
			cam_ent = e->index;
		}
		
		state = ST_RUN;
		while (state != ST_FINISH)
		{
			{	std::unique_lock g(pc_lock);
				core->step();
			}
			sleep(core->step_len);
		}
	}
	catch (std::exception& e) {
		state = ST_FINISH;
		VLOGE("Game failed: {}", e.what());
	}
	
	
	
	void init_resources()
	{
		struct Explosion : ParticleGroupGenerator {
			size_t n_base; // base count
			FColor clr0, clr1; // color range
			
			float p_min = 0.1, p_max = 5.f;
			float a_lim = M_PI; // angle limit
			        
			Transform tr;
			float power;
			FColor clr_t;
			
			size_t begin(const Transform& tr_in, ParticleParams& p, float power_in) {
				p.px = tr.pos.x;
				p.py = tr.pos.y;
				p.size = 0.1;
				p.ax = p.ay = 0;
				p.lt = 0;
				
				tr = tr_in;
				power = clampf(power_in, p_min, p_max);
				clr_t = clr0 + (clr1 - clr0) * clampf(power / p_max - p_min, 0.1, 0.9);
				return n_base * power;
			}
			void gen(ParticleParams& p) {
				vec2fp vel{clampf(power, 0.1, 2), 0};
				vel.fastrotate( tr.rot + rnd_range(-a_lim, a_lim) );
				p.vx = vel.x;
				p.vy = vel.y;
				
				p.ft = rnd_range(2, 5);
				
#define RND(A) p.clr.A = rnd_bool() ? rnd_range(clr0.A, clr_t.A) : rnd_range(clr_t.A, clr1.A)
				RND(r); RND(g); RND(b); RND(a);
#undef RND
			}
		};
		
		// FE_EXPLOSION
		{
			auto g = std::make_shared<Explosion>();
			g->n_base = 80;
			g->clr0 = FColor(0.5, 0, 0, 0.5);
			g->clr1 = FColor(1.2, 1, 0.5, 1);
			pres->add_preset(g);
		}
		// FE_HIT
		{
			auto g = std::make_shared<Explosion>();
			g->n_base = 20;
			g->clr0 = FColor(1, 0, 0, 0.1);
			g->clr1 = FColor(1.2, 0.2, 0.2, 1);
			pres->add_preset(g);
		}
		
		// OE_DUST
		auto dust_ps = std::make_shared<Explosion>();
		{
			auto& g = dust_ps;
			g->n_base = 8;
			g->p_min = 1.f, g->p_max = 2.f;
			g->clr0 = FColor(0.4, 1, 1, 0.8);
			g->clr1 = FColor(0.9, 1, 1, 0.8);
		}
		
		struct RenShape {
			std::vector<vec2fp> ps;
			bool loop = true;
		};
		std::vector<RenShape> ps;
		
		auto addobj = [&](FColor clr, vec2fp size)
		{
			struct Death : ParticleGroupGenerator {
				struct Ln {
					vec2fp a, b;
					size_t n;
				};
				std::vector<Ln> ls;
				size_t total = 0;
				Transform tr;
				FColor clr;
				
				size_t li, lc;
				
				Death(const std::vector<RenShape>& ps, FColor clr): clr(clr)
				{
					for (auto& s : ps)
					{
						size_t n = s.ps.size();
						if (s.loop) ++n;
						for (size_t i=1; i<n; ++i)
						{
							auto& l = ls.emplace_back();
							l.a = s.ps[i-1];
							l.b = s.ps[i % s.ps.size()];
							l.n = l.a.dist(l.b) * 10;
							if (l.n < 2) l.n = 2;
							total += l.n;
						}
					}
				}
				size_t begin(const Transform& tr_in, ParticleParams& p, float) {
					p.size = 0.1;
					p.lt = 0;
					p.clr = clr;
					
					li = lc = 0;
					tr = tr_in;
					return total;
				}
				void gen(ParticleParams& p) {
					auto& l = ls[li];
					float t = (lc + 0.5f) / l.n;
					vec2fp pos = lint(l.a, l.b, t);
					if (++lc == l.n) ++li;
					
					p.px = pos.x;
					p.py = pos.y;
					
					vec2fp rv = vec2fp(0, rnd_range(0.1, 0.5));
					rv.fastrotate( rnd_range(-M_PI, M_PI) );
					p.vx = rv.x;
					p.vy = rv.y;
					
					p.ft = rnd_range(1, 3);
				}
			};
			
			vec2fp maxcd = {};
			for (auto& s : ps) for (auto& p : s.ps) {
				maxcd.x = std::max(maxcd.x, fabs(p.x));
				maxcd.y = std::max(maxcd.y, fabs(p.y));
			}
			maxcd = size / maxcd;
			for (auto& s : ps) for (auto& p : s.ps) p *= maxcd;
			
			auto& ren = RenAAL::get();
			for (auto& p : ps) ren.inst_add(p.ps, p.loop);
			
			PresObject p;
			p.id = ren.inst_add_end();
			p.clr = clr;
			p.ps.emplace_back(new Death(ps, clr));
			p.ps.emplace_back(dust_ps);
			pres->add_preset(p);
			
			ps.clear();
		};
		auto lnn = [&](bool loop){ ps.emplace_back().loop = loop; };
		auto lpt = [&](float x, float y){ ps.back().ps.push_back({x, y}); };
		
		// OBJ_PC
		lnn(true);
		lpt(0, -1);
		lpt(1, 1);
		lpt(0, 0.7);
		lpt(-1, 1);
		addobj(FColor(0.4, 0.9, 1, 1), vec2fp::one(hsz_rat));
		
		// OBJ_BOX
		lnn(true);
		lpt(-1, -1); lpt( 1, -1);
		lpt( 1,  1); lpt(-1,  1);
		addobj(FColor(1, 0.4, 0, 1), vec2fp::one(hsz_box));
		
		// OBJ_HEAVY
		lnn(true);
		lpt(-1, -1); lpt( 1, -1);
		lpt( 1,  1); lpt(-1,  1);
		addobj(FColor(1, 0.4, 0, 1), vec2fp::one(hsz_heavy));
		
		// PROJ_ROCKET
		lnn(true);
		lpt(0, -1);
		lpt(-1, 1);
		lpt( 1, 1);
		addobj(FColor(0.2, 1, 0.6, 1.5), vec2fp::one(hsz_proj));
		
		// PROJ_PLASMA
		lnn(true);
		lpt(-1, -1); lpt( 1, -1);
		lpt( 1,  1); lpt(-1,  1);
		addobj(FColor(0.4, 1, 0.4, 1.5), vec2fp::one(hsz_proj));
		
		// ARM_SHIELD
		lnn(true);
		lpt(-1, -1  ); lpt( 0, -1.2);
		lpt( 0,  0.8); lpt(-1,  1  );
		lnn(true);
		lpt( 0, -1.2); lpt( 1, -1  );
		lpt( 1,  1  ); lpt( 0,  0.8);
		addobj(FColor(1, 0.4, 0, 1), hsz_shld);
		
		// ARM_ROCKET
		lnn(true);
		lpt(-1, -1); lpt( 1, -1);
		lpt( 1,  1); lpt(-1,  1);
		addobj(FColor(1, 0.4, 0, 1), {0.2, 0.7});
		
		// ARM_PLASMA
		lnn(true);
		lpt(-1, -1); lpt( 1, -1);
		lpt( 1,  1); lpt(-1,  1);
		lnn(true);
		lpt(-1, -2); lpt( 0, -2);
		lpt( 0, -1); lpt(-1, -1);
		addobj(FColor(1, 0.4, 0, 1), {0.25, 0.5});
	}
	void init_terrain(float hsize)
	{
		std::vector<vec2fp> ps = {{-hsize,-hsize}, {hsize,-hsize}, {hsize,hsize}, {-hsize,hsize}};
		RenAAL::get().inst_add(ps, true);
		
		PresObject p;
		p.id = RenAAL::get().inst_add_end();
		p.clr = FColor(0.75, 0.75, 0.75, 1);
		pres->add_preset(p);
		
		auto e = core->create_ent();
		e->cnew(new EC_Render(e, OBJ_ALL_WALLS));
		
		b2BodyDef def;
		e->cnew(new EC_Physics(def));
		
		b2ChainShape shp;
		b2Vec2 vs[4];
		for (int i=0; i<4; ++i) vs[i] = conv(ps[i]);
		shp.CreateLoop(vs, 4);
		
		b2FixtureDef fd;
		fd.friction = 0.15;
		fd.restitution = 0.1;
		fd.shape = &shp;
		e->get_phy()->body->CreateFixture(&fd);
		
		e->dbg_name = "terrain";
	}
	void cam_follow(Entity* ent)
	{
		b2Body* body = ent->get_phy()->body;
		
		b2Vec2 vel = body->GetLinearVelocity();
		float spd = vel.Length();
		b2Vec2 fwd = body->GetWorldVector({0, -1});
		if (b2Dot(vel, fwd) < 0) fwd = -fwd;
		vel = fwd;
		vel *= spd;
		
		Camera::Frame cf = cam->get_state();
		
		vec2fp old = cf.pos;
		cf.pos = conv( vel + body->GetPosition() );
		
		float d = cf.pos.dist(old);
		if (d < 3.f) return; // min dist - 3 m
		
		cf.len = TimeSpan::seconds(0.2) * d; // 5 m / sec
		
		cam->reset_frames();
		cam->add_frame(cf);
	}
};



MainLoop* MainLoop::current;
void MainLoop::init( InitWhich which )
{
	if (which == INIT_DEFAULT)
		which = INIT_GAME;
	
	try {
		if		(which == INIT_RENTEST) current = new ML_Rentest;
		else if (which == INIT_GAME)    current = new ML_Game;
	}
	catch (std::exception& e) {
		VLOGE("MainLoop::init() failed: {}", e.what());
	}
}
MainLoop::~MainLoop() {
	if (current == this) current = nullptr;
}
