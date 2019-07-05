#include "render/ren_aal.hpp"
#include "utils/noise.hpp"
#include "presenter.hpp"



GameResBase& GameResBase::get()
{
	static GameResBase b;
	return b;
}
void GameResBase::init_res()
{
	auto pres = &GamePresenter::get();
	
	struct Explosion : ParticleGroupGenerator {
		size_t n_base; // base count
		FColor clr0, clr1; // color range
		
		float p_min = 0.1, p_max = 5.f;
		float a_lim = M_PI; // angle limit
		
		float alpha = 1.f;
		float size = 0.1;
		float spd_min = 0.f;
		        
		Transform tr;
		float power;
		FColor clr_t;
		
		size_t begin(const Transform& tr_in, ParticleParams& p, float power_in) {
			tr = tr_in;
			power = clampf(power_in, p_min, p_max);
			clr_t = clr0 + (clr1 - clr0) * clampf(power / p_max - p_min, 0.1, 0.9);
			
			p.px = tr.pos.x;
			p.py = tr.pos.y;
			p.size = size;
			p.lt = 0;
			
			return n_base * power;
		}
		void gen(ParticleParams& p) {
			float vk = rnd_range(0.5, 2) * clampf(power, 0.1, 2) + spd_min;
			vec2fp vel{vk, 0};
			vel.fastrotate( tr.rot + rnd_range(-a_lim, a_lim) );
			p.vx = vel.x;
			p.vy = vel.y;
			
			p.ft = rnd_range(2, 3.5) * alpha;
			float t = rnd_range();
			
#define RND(A) p.clr.A = t < 0.5 ? lint(clr0.A, clr_t.A, t*2) : lint(clr_t.A, clr1.A, t*2-1)
			RND(r); RND(g); RND(b); RND(a);
#undef RND
//				p.clr.a *= alpha;
		}
	};
	struct WpnExplosion : ParticleGroupGenerator {
		FColor clr   = FColor(0.2, 0.2, 1);
		FColor clr_n = FColor(0.1, 0.1, 0.4);
		FColor clr_p = FColor(0.2, 0.6, 0);
		bool implode = false;
		
		vec2fp ctr;
		float rad;
		float t, dt;
		
		size_t begin(const Transform& tr_in, ParticleParams& p, float power_in)
		{
			p.size = 0.1;
			
			size_t num = (2 * M_PI * rad) / (p.size * 0.5);
			if (!num) num = 3;
			
			ctr = tr_in.pos;
			rad = power_in;
			t = 0;
			dt = (M_PI*2) / num;
			return num;
		}
		void gen(ParticleParams& p)
		{
			p.lt = rnd_range(1.5, 2.5);
			p.ft = p.lt * 0.25; p.lt *= 0.75;
			p.clr = clr;
			
			for (int i=0; i<3; ++i) {
				if (rnd_bool()) p.clr[i] += rnd_range(0, clr_p[i]);
				else            p.clr[i] -= rnd_range(0, clr_n[i]);
			}
			
			vec2fp r = {rad, 0};
			r.rotate(t);
			float vt = (p.lt + p.ft) * 0.75;
			
			if (!implode)
			{
				p.px = ctr.x;
				p.py = ctr.y;
				
				p.vx = r.x / vt;
				p.vy = r.y / vt;
			}
			else
			{
				p.px = ctr.x + r.x;
				p.py = ctr.y + r.y;
				
				p.vx = -r.x / vt * 2;
				p.vy = -r.y / vt * 2;
			}
			
			t += dt;
		}
	};
	
	// FE_EXPLOSION
	{
		auto g = std::make_shared<Explosion>();
		g->n_base = 30;
		g->p_max = 3.f;
		g->size = 0.3;
		g->spd_min = 1.;
		g->clr0 = FColor(0.5, 0, 0, 0.5);
		g->clr1 = FColor(1.2, 1, 0.8, 1);
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
	// FE_SHOOT_DUST
	{
		auto g = std::make_shared<Explosion>();
		g->n_base = 40;
		g->a_lim = M_PI * 0.2;
		g->p_min = 0.5, g->p_max = 2.f;
		g->spd_min = 2.f;
		g->clr0 = FColor(1, 0.5, 0, 0.5);
		g->clr1 = FColor(1, 1, 0.2, 1.2);
		pres->add_preset(g);
	}
	// FE_WPN_EXPLOSION
	{
		auto g = std::make_shared<WpnExplosion>();
		pres->add_preset(g);
	}
	// FE_WPN_IMPLOSION
	{
		auto g = std::make_shared<WpnExplosion>();
		g->implode = true;
		pres->add_preset(g);
	}
	
	// OE_DUST
	auto dust_ps = std::make_shared<Explosion>();
	{
		auto& g = dust_ps;
		g->n_base = 8;
		g->p_min = 0.2f, g->p_max = 2.f;
		g->alpha = 0.45;
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
						l.n = l.a.dist(l.b) * 5;
						if (l.n < 2) l.n = 2;
						total += l.n;
					}
				}
			}
			size_t begin(const Transform& tr_in, ParticleParams& p, float)
			{
				p.size = 0.1;
				p.lt = 0;
				p.clr = clr;
				p.lt = 0;
				
				li = lc = 0;
				tr = tr_in;
				return total;
			}
			void gen(ParticleParams& p)
			{
				auto& l = ls[li];
				float t = (lc + 0.5f) / l.n;
				vec2fp pos = lint(l.a, l.b, t);
				if (++lc == l.n) ++li, lc = 0;
				
				vec2fp rp = pos;
				rp.fastrotate(tr.rot);
				p.px = rp.x + tr.pos.x;
				p.py = rp.y + tr.pos.y;
				
				vec2fp rv = vec2fp(0, rnd_range(0.1, 0.5));
				rv.fastrotate( rnd_range(-M_PI, M_PI) );
				p.vx = rv.x;
				p.vy = rv.y;
				
				p.ft = rnd_range(3, 5);
			}
		};
		
		vec2fp maxcd = {};
		for (auto& s : ps) for (auto& p : s.ps) {
			maxcd.x = std::max(maxcd.x, std::fabs(p.x));
			maxcd.y = std::max(maxcd.y, std::fabs(p.y));
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
	
	// OBJ_NONE
	lnn(false); lpt(-1, -1); lpt(1, 1);
	lnn(false); lpt(-1, 1); lpt(1, -1);
	addobj(FColor(1, 0, 0, 1), vec2fp::one(3));

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
	
	// PROJ_RAY
	
	// PROJ_BULLET
	lnn(true);
	lpt(-1, -1); lpt( 1, -1);
	lpt( 1,  1); lpt(-1,  1);
	addobj(FColor(1, 1, 0.2, 1.5), vec2fp(0.4, 0.005));
	
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
	addobj(FColor(1, 0.4, 0, 1), {0.7, 0.2});
	
	// ARM_MGUN
	lnn(true);
	lpt(-1, -1); lpt( 1, -1);
	lpt( 1,  1); lpt(-1,  1);
	addobj(FColor(1, 0.4, 0, 1), {0.6, 0.4});
	
	// ARM_RAYGUN
	
	// ARM_PLASMA
	lnn(true);
	lpt(-1, -1); lpt( 1, -1);
	lpt( 1,  1); lpt(-1,  1);
	lnn(true);
	lpt(-1, -2); lpt( 0, -2);
	lpt( 0, -1); lpt(-1, -1);
	addobj(FColor(1, 0.4, 0, 1), {0.5, 0.25});
}
