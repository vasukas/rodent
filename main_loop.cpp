#include <mutex>
#include <thread>
#include "core/tui_layer.hpp"
#include "game/game_core.hpp"
#include "game/logic.hpp"
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
				ptg.TTL.set_ms(2000), ptg.FT.set_ms(1000);
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
	PC_Logic* pc_log = nullptr; // player character
	
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
		
		VLOGI("Box2D version: {}.{}.{}", b2_version.major, b2_version.minor, b2_version.revision);
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
		
		if (pc_log) {
			std::unique_lock g(pc_lock);
			pc_log->on_event_sdl(ev);
		}
	}
	void render(TimeSpan passed)
	{
		if		(state == ST_LOADING) RenImm::get().draw_text({}, "Loading...", -1, false, 4.f);
		else if (state == ST_FINISH)  RenImm::get().draw_text({}, "Game finished. Press ESC to exit.", -1, false, 4.f);
		else {
			RenImm::get().set_context(RenImm::DEFCTX_WORLD);
			
			std::unique_lock g(pc_lock);
//			if (auto e = core->get_ent(cam_ent)) cam_follow(e); else cam_ent = 0;
			GamePresenter::get().render(passed);
			
			if (ph_debug_draw)
				core->get_phy().world.DrawDebugData();
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
		void step() {
			const vec2fp kmv[4] = {{-1, 0}, {0, -1}, {0, 1}, {1, 0}};
			vec2fp mv = {};
			bool mv_any = false;
			for (int i=0; i<4; ++i) if (keyf[i]) mv += kmv[i], mv_any = true;
			if (mv.equals({}, 0.1)) mv_any = false; else mv.norm();
			
			b2Body* body = ent->get_phy()->body;
			const float tmul = ent->core.step_len.seconds();
			
			const b2Vec2 vel = body->GetLinearVelocity();
			const float min_ch = 0.1f;
			const float max_ch = 20.f;
			const float tar_m = 10.f;
			
			const float ps_lim = tar_m * 0.8;
			if (vel.Length() > ps_lim) {
				float p = vel.Length() - ps_lim;
				Transform tr;
				tr.rot = std::atan2(vel.y, vel.x);
				tr.pos = {-GameResBase::get().hsz_rat, 0};
				tr.pos.fastrotate(tr.rot);
				ent->get_ren()->parts(OE_DUST, p, tr);
			}
			
			b2Vec2 tar = tar_m * conv(mv);
			if (keyf[4]) tar *= 1.8;
			
			b2Vec2 vec = tar - vel;
			float xd = fabs(tar.x) - fabs(vel.x);
			float yd = fabs(tar.y) - fabs(vel.y);
			
			if (xd > 0) vec.x *= 10; else vec.x *= !keyf[4]? (xd > 1.0 ? 20 : 10) : 2;
			if (yd > 0) vec.y *= 10; else vec.y *= !keyf[4]? (yd > 1.0 ? 20 : 10) : 2;
			float dl = vec.Length();
			
			if (dl == 0.f) return;
			else if (dl > max_ch) vec *= max_ch / dl;
			else if (dl < min_ch) vec *= 1.f / tmul;
			
			body->ApplyLinearImpulse(body->GetMass() * tmul * vec, body->GetWorldCenter(), mv_any);
		}
		void on_event_sdl(SDL_Event& ev) {
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
	
	void init_game()
	{
		GameCore::InitParams core_init;
		core_init.random_seed = 0;
		core.reset( GameCore::create(core_init) );
		
		RenAAL::get().inst_begin();
		GameResBase::get().init_res();
		init_terrain(hsize);
		RenAAL::get().inst_end();
	}
	void thr_func() try
	{
		struct DL : EC_Logic {
			void step() {
				b2Body* body = ent->get_phy()->body;
				const float tmul = ent->core.step_len.seconds();
//				const float fk = body->GetFixtureList()->GetFriction();
				
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
			void on_event(const ContactEvent& ev) {
				const float lim = 120.f; // 40
				if (ev.type == ContactEvent::T_RESOLVE && ev.imp > lim) {
					ent->destroy();
					GamePresenter::get().effect(FE_EXPLOSION, {ev.point, 0.f}, ev.imp / lim + 0.5f);
				}
			}
		};
		
		auto& rb = GameResBase::get();
		
		for (int i=0; i<25; ++i)
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
			e->get_phy()->add_box(fd, vec2fp::one(big? rb.hsz_heavy : rb.hsz_box), big? 25.f : 8.f);
			
			e->cnew(new DL);
			
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
			e->get_phy()->add_circle(fd, rb.hsz_rat, 15.f);
			
			pc_log = new PC_Logic(this, e);
			e->cnew(pc_log);
			
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
