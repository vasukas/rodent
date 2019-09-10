#include "game/common_defs.hpp"
#include "render/ren_aal.hpp"
#include "render/particles.hpp"
#include "utils/noise.hpp"
#include "utils/svg_simple.hpp"
#include "presenter.hpp"

// just looks better
#define PLAYER_MODEL_RADIUS_INCREASE 0.1

// 
#define WEAPON_LINE_RADIUS 0.03

// will be changed to param later
#define MODEL_FILENAME "res/models.svg"

//
using namespace GameConst;



class ResBase_Impl : public ResBase
{
public:
	std::array <std::array<std::unique_ptr<ParticleGroupGenerator>, ME_TOTAL_COUNT_INTERNAL>, MODEL_TOTAL_COUNT_INTERNAL> ld_me;
	std::array <std::unique_ptr<ParticleGroupGenerator>, FE_TOTAL_COUNT_INTERNAL> ld_es;
	
	ParticleGroupGenerator* get_eff(ModelType type, ModelEffect eff)
	{
		return ld_me[type][eff].get();
	}
	ParticleGroupGenerator* get_eff(FreeEffect eff)
	{
		return ld_es[eff].get();
	}
	void init_ren();
};
ResBase& ResBase::get()
{
	static ResBase_Impl b;
	return b;
}



void ResBase_Impl::init_ren()
{
	struct Explosion : ParticleGroupGenerator
	{
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
		
		size_t begin(const BatchPars& pars, ParticleParams& p)
		{
			tr = pars.tr;
			power = clampf(pars.power, p_min, p_max);
			clr_t = clr0 + (clr1 - clr0) * clampf(power / p_max - p_min, 0.1, 0.9);
			
			p.px = tr.pos.x;
			p.py = tr.pos.y;
			p.size = size;
			p.lt = 0;
			
			return n_base * power;
		}
		void gen(ParticleParams& p)
		{
			float vk = rnd_stat().range(0.5, 2) * clampf(power, 0.1, 2) + spd_min;
			vec2fp vel{vk, 0};
			vel.fastrotate( tr.rot + rnd_stat().range(-a_lim, a_lim) );
			p.vx = vel.x;
			p.vy = vel.y;
			
			p.ft = rnd_stat().range(2, 3.5) * alpha;
			float t = rnd_stat().range_n();
			
#define RND(A) p.clr.A = t < 0.5 ? lerp(clr0.A, clr_t.A, t*2) : lerp(clr_t.A, clr1.A, t*2-1)
			RND(r); RND(g); RND(b); RND(a);
#undef RND
//				p.clr.a *= alpha;
		}
	};
	struct WpnExplosion : ParticleGroupGenerator
	{
		FColor clr   = FColor(0.2, 0.2, 1);
		FColor clr_n = FColor(0.1, 0.1, 0.4);
		FColor clr_p = FColor(0.2, 0.6, 0);
		bool implode = false;
		
		vec2fp ctr;
		float rad;
		float t, dt;
		
		size_t begin(const BatchPars& pars, ParticleParams& p)
		{
			p.size = 1; //0.1;
			
			ctr = pars.tr.pos;
			rad = pars.power;
			t = 0;
			
			size_t num = (2 * M_PI * rad) / (p.size * 0.5);
			if (!num) num = 3;
			dt = (M_PI*2) / num;
			return num;
		}
		void gen(ParticleParams& p)
		{
			p.lt = rnd_stat().range(1.5, 2.5);
			p.ft = p.lt * 0.5; p.lt *= 0.5;
			p.clr = clr;
			
			for (int i=0; i<3; ++i) {
				if (rnd_stat().flag()) p.clr[i] += rnd_stat().range(0, clr_p[i]);
				else                   p.clr[i] -= rnd_stat().range(0, clr_n[i]);
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
	struct Death : ParticleGroupGenerator
	{
		struct Ln {
			vec2fp a, b;
			size_t n;
		};
		std::vector<Ln> ls;
		size_t total = 0;
		std::optional<FColor> clr;
		
		Transform tr;
		size_t li, lc;
		
		Death(const std::vector<std::vector<vec2fp>>& ps)
		{
			for (auto& s : ps)
			{
				size_t n = s.size();
				for (size_t i=1; i<n; ++i)
				{
					auto& l = ls.emplace_back();
					l.a = s[i-1];
					l.b = s[i];
					l.n = l.a.dist(l.b) * 5;
					if (l.n < 2) l.n = 2;
					total += l.n;
				}
			}
		}
		size_t begin(const BatchPars& pars, ParticleParams& p)
		{
			p.size = 0.15;
			p.lt = 0;
			p.clr = clr? *clr : pars.clr;
			p.lt = 0;
			
			li = lc = 0;
			tr = pars.tr;
			return total;
		}
		void gen(ParticleParams& p)
		{
			auto& l = ls[li];
			float t = (lc + 0.5f) / l.n;
			vec2fp pos = lerp(l.a, l.b, t);
			if (++lc == l.n) {++li; lc = 0;}
			
			vec2fp rp = pos;
			rp.fastrotate(tr.rot);
			p.px = rp.x + tr.pos.x;
			p.py = rp.y + tr.pos.y;
			
			vec2fp rv = vec2fp(0, rnd_stat().range(0.1, 0.5));
			rv.fastrotate( rnd_stat().range(-M_PI, M_PI) );
			p.vx = rv.x;
			p.vy = rv.y;
			
			p.ft = rnd_stat().range(3, 5);
		}
	};



	{
		auto g = new Explosion;
		ld_es[FE_EXPLOSION].reset(g);
		
		g->n_base = 30;
		g->p_max = 3.f;
		g->size = 0.3;
		g->spd_min = 1.;
		g->clr0 = FColor(0.5, 0, 0, 0.5);
		g->clr1 = FColor(1.2, 1, 0.8, 1);
	}{
		auto g = new Explosion;
		ld_es[FE_HIT].reset(g);
		
		g->n_base = 20;
		g->clr0 = FColor(1, 0, 0, 0.1);
		g->clr1 = FColor(1.2, 0.2, 0.2, 0.5);
	}{
		auto g = new Explosion;
		ld_es[FE_HIT_SHIELD].reset(g);
		
		g->n_base = 20;
		g->clr0 = FColor(0, 0.5, 1, 0.1);
		g->clr1 = FColor(0.2, 1, 1.2, 1);
	}{
		auto g = new WpnExplosion;
		ld_es[FE_WPN_EXPLOSION].reset(g);
		
		g->clr   = FColor(1, 0.3, 0.1);
		g->clr_n = FColor(0, 0.2, 0.1);
		g->clr_p = FColor(0.2, 0.8, 0.3);
	}{
		auto g = new Explosion;
		ld_es[FE_SHOT_DUST].reset(g);
		
		g->n_base = 40;
		g->a_lim = M_PI * 0.2;
		g->p_min = 0.5, g->p_max = 2.f;
		g->spd_min = 2.f;
		g->clr0 = FColor(1, 0.5, 0, 0.5);
		g->clr1 = FColor(1, 1, 0.2, 1.2);
	}{
		auto g = new Explosion;
		ld_es[FE_SPEED_DUST].reset(g);
		
		g->n_base = 8;
		g->p_min = 0.2f, g->p_max = 2.f;
		g->alpha = 0.45;
		g->size = 0.15;
		g->clr0 = FColor(0.4, 1, 1, 0.8);
		g->clr1 = FColor(0.9, 1, 1, 0.8);
	}{
		auto g = new WpnExplosion;
		ld_es[FE_SPAWN].reset(g);
		
		g->implode = true;
	}
	
	
	
	struct ModelInfo
	{
		std::vector<std::vector<vec2fp>> ls;
	};
	ModelInfo mlns[MODEL_TOTAL_COUNT_INTERNAL];
	
	
	
	std::pair<ModelType, std::string> md_ns[] =
	{
	    {MODEL_ERROR, "error"},
	    
	    {MODEL_PC_RAT, "rat"},
	    {MODEL_BOX_SMALL, "box_small"},
	    
	    {MODEL_MEDKIT, "medkit"},
	    {MODEL_ARMOR, "armor"},
	    
	    {MODEL_BAT, "bat"},
	    {MODEL_HANDGUN, "claw"},
	    {MODEL_BOLTER, "bolter"},
	    {MODEL_GRENADE, "grenade"},
	    {MODEL_MINIGUN, "mgun"},
	    {MODEL_ROCKET, "rocket"},
	    {MODEL_ELECTRO, "electro"},
	    
	    {MODEL_HANDGUN_AMMO, "claw_ammo"},
	    {MODEL_BOLTER_AMMO, "bolter_ammo"},
	    {MODEL_GRENADE_AMMO, "grenade_ammo"},
	    {MODEL_MINIGUN_AMMO, "mgun_ammo"},
	    {MODEL_ROCKET_AMMO, "rocket_ammo"},
	    {MODEL_ELECTRO_AMMO, "electro_ammo"},
	    
	    {MODEL_SPHERE, "sphere"}
	};
	
	// load models
	
	{	SVG_File svg = svg_read(MODEL_FILENAME);
		
		for (auto& p : svg.paths)
		{
			size_t ni = p.id.length() - 1;
			for (; ni; --ni) {
				char c = p.id[ni];
				if (c > '9' || c < '0') break;
			}
			p.id.erase(ni + 1);
		}
		
		for (auto& md : md_ns)
		{
			bool any = false;
			for (auto& p : svg.paths)
			{
				if (p.id == md.second) {
					mlns[md.first].ls.emplace_back( std::move(p.ps) );
					any = true;
				}
			}
			if (!any)
				throw std::runtime_error(md.second + " - model not found");
		}
	}
	
	// center models
	
	for (auto& m : mlns)
	{
		vec2fp ctr = {};
		size_t ctr_n = 0;
		for (auto& s : m.ls) {
			size_t n = s.size();
			if (n && s.front().equals( s.back(), 1e-10 )) --n;
			for (size_t i=0; i<n; ++i) ctr += s[i];
			ctr_n += n;
		}
		ctr /= ctr_n;
		for (auto& s : m.ls)
			for (auto& p : s) p -= ctr;
	}
	
	// rotate models (0 angle: top -> right)
	
	for (auto& m : mlns) {
		for (auto& s : m.ls)
			for (auto& p : s)
				p.rot90ccw();
	}
	
	
	
	// scale
	
	auto scale_to = [&](ModelType t, float radius) {
		auto& m = mlns[t];
		float mr = 0.f;
		for (auto& s : m.ls)
			for (auto& p : s)
				mr = std::max(mr, std::max(std::fabs(p.x), std::fabs(p.y)));
		for (auto& s : m.ls)
			for (auto& p : s)
				p *= radius / mr;
	};
	
	scale_to(MODEL_PC_RAT, hsz_rat + PLAYER_MODEL_RADIUS_INCREASE);
	scale_to(MODEL_BOX_SMALL, hsz_box_small);
	
	scale_to(MODEL_MEDKIT, hsz_supply);
	scale_to(MODEL_ARMOR, hsz_supply);
	
	
	
	// generate projs
	
	auto ln_proj = [&](ModelType t, float n) {
		auto& m = mlns[t];
		if (m.ls.empty()) {
			auto& v = m.ls.emplace_back();
			v.push_back({0, 0});
			v.push_back({n, 0});
		}
	};
	auto sp_proj = [&](ModelType t, float r) {
		auto& m = mlns[t];
		if (m.ls.empty()) {
			m.ls = mlns[MODEL_SPHERE].ls;
			scale_to(t, r);
		}
	};
	
	ln_proj(MODEL_HANDGUN_PROJ, 0.3);
	ln_proj(MODEL_BOLTER_PROJ,  1.5);
	ln_proj(MODEL_MINIGUN_PROJ, 0.7);
	sp_proj(MODEL_GRENADE_PROJ, hsz_proj);
	
	sp_proj(MODEL_BOLTER_PROJ_ALT, hsz_proj);
	sp_proj(MODEL_GRENADE_PROJ_ALT, hsz_proj_big);
	sp_proj(MODEL_MINIGUN_PROJ_ALT, hsz_proj);
	sp_proj(MODEL_ELECTRO_PROJ_ALT, hsz_proj_big);
	
	mlns[MODEL_ROCKET_PROJ] = mlns[MODEL_ROCKET_AMMO];
	scale_to(MODEL_ROCKET_PROJ, hsz_proj);
	
	
	
	// offset weapons
	
	const int wpn_ixs[] = {
	    MODEL_BAT,
        MODEL_HANDGUN,
        MODEL_BOLTER,
        MODEL_GRENADE,
	    MODEL_MINIGUN,
	    MODEL_ROCKET,
	    MODEL_ELECTRO
	};
	
	for (auto i : wpn_ixs)
	{
		float xmin = std::numeric_limits<float>::max();
		for (auto& s : mlns[i].ls) for (auto& p : s) xmin = std::min(xmin, p.x);
		for (auto& s : mlns[i].ls) for (auto& p : s) p.x -= xmin;
	}
	
	
	
	// fin
	
	auto& ren = RenAAL::get();
	
	for (size_t i = MODEL_LEVEL_STATIC + 1; i < MODEL_TOTAL_COUNT_INTERNAL; ++i)
	{
		if (mlns[i].ls.empty() && i != MODEL_NONE)
			throw std::logic_error(std::to_string(i) + " - no model data (internal error)");
		
		ld_me[i][ME_DEATH].reset( new Death(mlns[i].ls) );
		
		float width = 0.1f;
		if (std::find( std::begin(wpn_ixs), std::end(wpn_ixs), i ) != std::end(wpn_ixs))
			width = WEAPON_LINE_RADIUS;
		
		for (auto& s : mlns[i].ls) ren.inst_add(s, false, width);
		if (i != ren.inst_add_end())
			throw std::logic_error(std::to_string(i) + " - index mismatch (internal error)");
	}
	
	// generate parts
	
	{	auto g = new Death(mlns[MODEL_PC_RAT].ls);
		g->clr = FColor(0.3, 0.7, 1, 2);
		ld_me[MODEL_PC_RAT][ME_POWERED].reset(g);
	}
}
