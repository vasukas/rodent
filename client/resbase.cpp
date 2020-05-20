#include "core/hard_paths.hpp"
#include "core/settings.hpp"
#include "game/common_defs.hpp"
#include "render/control.hpp"
#include "render/ren_aal.hpp"
#include "render/ren_particles.hpp"
#include "render/texture.hpp"
#include "utils/image_utils.hpp"
#include "utils/noise.hpp"
#include "utils/svg_simple.hpp"
#include "vaslib/vas_atlas_packer.hpp"
#include "vaslib/vas_log.hpp"
#include "resbase.hpp"

// just looks better
#define PLAYER_MODEL_RADIUS_INCREASE -0.05

// 
#define WEAPON_LINE_RADIUS 0.03

//
using namespace GameConst;



class ResBase_Impl : public ResBase
{
public:
	std::array <std::array<std::unique_ptr<ParticleGroupGenerator>, ME_TOTAL_COUNT_INTERNAL>, MODEL_TOTAL_COUNT_INTERNAL> ld_me;
	std::array <std::unique_ptr<ParticleGroupGenerator>, FE_TOTAL_COUNT_INTERNAL> ld_es;
	std::array <vec2fp, MODEL_TOTAL_COUNT_INTERNAL> md_cpt = {};
	std::array <Rectfp, MODEL_TOTAL_COUNT_INTERNAL> md_sz = {};
	std::array <TextureReg, MODEL_TOTAL_COUNT_INTERNAL> md_img = {};	
	std::unique_ptr<Texture> tex;
	
	struct AAL_Model {
		std::vector<std::vector<vec2fp>> ls;
		float width;
	};
	struct InitResult {
		std::array<AAL_Model, MODEL_TOTAL_COUNT_INTERNAL> ms;
		std::unique_ptr<Texture> tex;
	};
	std::optional<InitResult> future_init;
	
	
	
	ParticleGroupGenerator* get_eff(ModelType type, ModelEffect eff)
	{
		return ld_me[type][eff].get();
	}
	ParticleGroupGenerator* get_eff(FreeEffect eff)
	{
		return ld_es[eff].get();
	}
	vec2fp get_cpt(ModelType type)
	{
		return md_cpt[type];
	}
	Rectfp get_size(ModelType type)
	{
		return md_sz[type];
	}
	TextureReg get_image(ModelType type)
	{
		return md_img[type];
	}
	
	ResBase_Impl();
	void init_ren_wait();
	void init_ren();
	
	InitResult init_func();
};
ResBase& ResBase::get()
{
	static ResBase_Impl b;
	return b;
}



ResBase_Impl::ResBase_Impl()
{
	future_init = init_func();
}
void ResBase_Impl::init_ren_wait()
{
	if (!future_init) future_init = init_func();
	auto mlns = std::move(future_init->ms);
	RenderControl::get().exec_task([&] {tex = std::move(future_init->tex);});
	future_init.reset();
	
	float kw, ka;
	switch (AppSettings::get().aal_type)
	{
	case AppSettings::AAL_OldFuzzy:
	case AppSettings::AAL_Clear:
		kw = 1; ka = 3;
		break;
		
	case AppSettings::AAL_CrispGlow:
		kw = 0.1; ka = 1.5;
		break;
	}
	
	auto& ren = RenAAL::get();
	for (size_t i = MODEL_LEVEL_STATIC + 1; i < MODEL_TOTAL_COUNT_INTERNAL; ++i)
	{
		for (auto& s : mlns[i].ls) ren.inst_add(s, false, mlns[i].width * kw, ka);
		if (i != ren.inst_add_end())
			throw std::logic_error(std::to_string(i) + " - index mismatch (internal error)");
	}
}
ResBase_Impl::InitResult ResBase_Impl::init_func()
{
	InitResult initres;
	
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
		
		size_t begin(const ParticleBatchPars& pars, ParticleParams& p)
		{
			tr = pars.tr;
			power = clampf(pars.power, p_min, p_max);
			clr_t = clr0 + (clr1 - clr0) * clampf(power / p_max - p_min, 0.1, 0.9);
			
			p.pos = tr.pos;
			p.size = size;
			p.lt = 0;
			
			return n_base * power;
		}
		void gen(ParticleParams& p)
		{
			float vk = rnd_stat().range(0.5, 2) * clampf(power, 0.1, 2) + spd_min;
			p.vel = {vk, 0};
			p.vel.fastrotate( tr.rot + rnd_stat().range(-a_lim, a_lim) );
			
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
		bool is_quick = false;
		
		size_t begin(const ParticleBatchPars& pars, ParticleParams& p)
		{
			p.size = 1;
			
			ctr = pars.tr.pos;
			rad = is_quick ? std::min(2.5f, pars.power) : pars.power;
			t = 0;
			
			size_t num = (2 * M_PI * rad) / (p.size * 0.5);
			if (!num) num = 3;
			dt = (M_PI*2) / num;
			return num;
		}
		void gen(ParticleParams& p)
		{
			float total_len = rnd_stat().range(1, 2) + (is_quick ? -0.5 : 0);
			p.lt = total_len * 0.5;
			p.ft = total_len * 0.5;
			p.clr = clr;
			
			for (int i=0; i<3; ++i) {
				if (rnd_stat().flag()) p.clr[i] += rnd_stat().range(0, clr_p[i]);
				else                   p.clr[i] -= rnd_stat().range(0, clr_n[i]);
			}
			
			vec2fp r = {rad, 0};
			r.rotate(t);
			r *= rnd_stat().range(0.7, 1.1);
			float vt = (p.lt + p.ft) * 0.75;
			
			if (!implode)
			{
				p.pos = ctr;
				p.vel = r / vt;
			}
			else
			{
				p.pos = ctr + r;
				p.vel = r / vt * -2;
			}
			
			if (is_quick)
			{
				p.decel_to_zero();
				p.vel *= 1.5;
			}
			
			if (!implode) p.apply_gravity(3, 0.8);
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
		size_t begin(const ParticleBatchPars& pars, ParticleParams& p)
		{
			p.size = 0.15;
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
			p.pos = lerp(l.a, l.b, t);
			if (++lc == l.n) {++li; lc = 0;}
			
			p.pos.fastrotate(tr.rot);
			p.pos += tr.pos;
			
			p.vel.set(0, rnd_stat().range(0.1, 0.5));
			p.vel.fastrotate( rnd_stat().range(-M_PI, M_PI) );
			
			p.ft = rnd_stat().range(3, 5);
		}
	};
	struct Aura : ParticleGroupGenerator
	{
		struct Ln {
			vec2fp a, b, av, bv;
			size_t n;
		};
		std::vector<Ln> ls;
		size_t total = 0;
		
		Transform tr;
		size_t li, lc;
		float time_k;
		
		Aura(const std::vector<std::vector<vec2fp>>& ps)
		{
			for (auto& s : ps)
			{
				size_t n = s.size();
				for (size_t i=1; i<n; ++i)
				{
					auto& l = ls.emplace_back();
					l.a = s[i-1];
					l.b = s[i];
					l.n = l.a.dist(l.b) * 8;
					if (l.n < 2) l.n = 2;
					total += l.n;
				}
			}
			
			for (size_t i=0; i < ls.size(); ++i)
			{
				auto norm = [&](auto& l)
				{
					auto n = l.b - l.a;
					n.norm();
					n.rot90ccw();
					if (dot(n, lerp(l.a, l.b, 0.5).norm()) < 0) n = -n;
					return n;
				};
				
				auto& l0 = ls[(i - 1 + ls.size()) % ls.size()];
				auto& lc = ls[i];
				auto& l1 = ls[(i + 1) % ls.size()];
				
				auto nc = norm(lc);
				lc.av = slerp(norm(l0), nc, 0.6);
				lc.bv = slerp(norm(l1), nc, 0.6);
			}
		}
		size_t begin(const ParticleBatchPars& pars, ParticleParams& p)
		{
			p.size = 0.25;
			p.clr = pars.clr;
			p.lt = 0;
			
			li = lc = 0;
			tr = pars.tr;
			time_k = pars.power;
			return total;
		}
		void gen(ParticleParams& p)
		{
			auto& l = ls[li];
			float t = (lc + 0.5f) / l.n;
			if (++lc == l.n) {++li; lc = 0;}
			
			p.pos = lerp(l.a, l.b, t);
			p.pos.fastrotate(tr.rot);
			p.pos += tr.pos;
			
			p.vel = slerp(l.av, l.bv, t);
			p.vel.fastrotate( tr.rot + rnd_stat().range(-1, 1) * deg_to_rad(20) );
			p.vel.norm_to( rnd_stat().range(0.5, 0.7) );
			
			p.ft = rnd_stat().range(time_k * 0.5, time_k);
			p.decel_to_zero();
		}
	};
	struct WpnCharge : ParticleGroupGenerator
	{
		ParticleBatchPars bp;
		
		size_t begin(const ParticleBatchPars& pars, ParticleParams& p)
		{
			bp = pars;
			p.pos = bp.tr.pos;
			p.size = 0.12;
			return bp.power * 5;
		}
		void gen(ParticleParams& p)
		{
			p.lt = rnd_stat().range(0.5, 1.5);
			p.ft = rnd_stat().range(0.5, 1);
			
			p.clr = bp.clr;
			for (int i=0; i<3; ++i) p.clr[i] += 0.1 * rnd_stat().range_n2();
			
			p.vel.set(bp.rad * rnd_stat().range(1, 4), 0);
			p.vel.fastrotate( bp.tr.rot + deg_to_rad(70) * rnd_stat().normal_fixed() );
		}
	};
	struct FireSpread : ParticleGroupGenerator
	{
		Transform tr;
		float dist, angle, anglim;
		
		size_t begin(const ParticleBatchPars& pars, ParticleParams& p)
		{
			constexpr float ppm = 2; // parts per radius meter
			
			tr = pars.tr;
			dist = pars.power;
			angle = pars.tr.rot;
			anglim = pars.rad;
			
			p.size = 0.12;
			return (2 * M_PI * dist) * (anglim / M_PI) * ppm;
		}
		void gen(ParticleParams& p)
		{
			p.lt = rnd_stat().range(0.3, 0.7);
			p.ft = rnd_stat().range(0.2, 0.4);
			
			if (rnd_stat().range_n() < 0.2) p.clr = {0,0,0,1};
			else
			p.clr = FColor(
				rnd_stat().range(0.05, 0.16),
				rnd_stat().range(0.2, 1), 1, 0.7
			).hsv_to_rgb();
			
			float rot = angle + rnd_stat().range_n2() * anglim;
			p.pos = tr.pos + vec2fp(dist * 0.2, 0).fastrotate(rot);
			p.vel.set(dist / (p.lt + p.ft), 0);
			p.vel.fastrotate(rot);
			p.decel_to_zero();
		}
	};
	struct CircAura : ParticleGroupGenerator
	{
		ParticleBatchPars bp;
		size_t i, num;
		float rot_k;
		
		size_t begin(const ParticleBatchPars& pars, ParticleParams&)
		{
			bp = pars;
			
			i = 0;
			num = (2 * M_PI * bp.rad) / 0.3;
			rot_k = M_PI / num;
			
			return num;
		}
		void gen(ParticleParams& p)
		{
			float ttl = rnd_stat().range(0.5, 1.5);
			float f_t = rnd_stat().range(0.1, 0.4);
			
			p.lt = ttl * f_t;
			p.ft = ttl * (1 - f_t);
			
			p.clr = bp.clr;
			for (int i=0; i<3; ++i) p.clr[i] += 0.2 * rnd_stat().range_n2();
			
			p.vel.set(bp.rad, 0);
			p.vel.fastrotate((i*2 + rnd_stat().range_n2()) * rot_k);
			++i;
			
			p.pos = bp.tr.pos + p.vel;
			p.vel *= bp.power * (1 + 0.2 * rnd_stat().range_n2()) / bp.rad;
			
			p.size = rnd_stat().range(0.1, 0.3);
		}
	};
	struct ExploFrag : ParticleGroupGenerator
	{
		vec2fp ctr;
		float pwr;
		
		size_t begin(const ParticleBatchPars& pars, ParticleParams& p)
		{
			p.size = 0.5;
			ctr = pars.tr.pos;
			pwr = clampf(pars.power, 0.2, 3);
			
			return int_round( rnd_stat().range(8, 10) ); 
		}
		void gen(ParticleParams& p)
		{
			p.lt = rnd_stat().range(0.2, 0.4);
			p.ft = rnd_stat().range(0.6, 1.5);
			
			p.clr = FColor(
				rnd_stat().flag() ? rnd_stat().range(0, 0.17) : rnd_stat().range(0.55, 1),
				0.8, 1, 0.7
			).hsv_to_rgb();
			
			vec2fp dt = {1, 0};
			dt.fastrotate( rnd_stat().range_n2() * M_PI );
			
			p.pos = ctr + dt * 0.5;
			p.vel = dt * rnd_stat().range(1.5, 7) * pwr;
			p.decel_to_zero();
			
			p.apply_gravity(2);
		}
	};
	struct FrostAura : ParticleGroupGenerator
	{
		vec2fp ctr;
		float pwr, rad, t, td;
		
		size_t begin(const ParticleBatchPars& pars, ParticleParams& p)
		{
			p.size = 0.2;
			
			p.clr = pars.clr;
			ctr = pars.tr.pos;
			pwr = pars.power;
			rad = pars.rad;
			
			int num = 2*M_PI * rad / 0.4;
			td = 2*M_PI / num;
			t = rnd_stat().range(0, td);
			return num;
		}
		void gen(ParticleParams& p)
		{
			vec2fp dir = vec2fp{rad, 0}.fastrotate(t);
			p.pos = ctr + dir;
			p.vel = dir * (pwr * rnd_stat().range(0.8, 1.2));
			t += td;
			
			p.lt = rnd_stat().range(0.5, 1);
			p.ft = rnd_stat().range(1, 1.5);
			p.decel_to_zero();
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
		
		g->clr   = FColor(1,   0.3, 0.1, 0.7);
		g->clr_n = FColor(0,   0.2, 0.1, 0.2);
		g->clr_p = FColor(0.2, 0.8, 0.3, 0.1);
		g->is_quick = true;
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
	}{
		auto g = new WpnCharge;
		ld_es[FE_WPN_CHARGE].reset(g);
	}{
		auto g = new CircAura;
		ld_es[FE_CIRCLE_AURA].reset(g);
	}{
		auto g = new ExploFrag;
		ld_es[FE_EXPLOSION_FRAG].reset(g);
	}{
		auto g = new FireSpread;
		ld_es[FE_FIRE_SPREAD].reset(g);
	}{
		auto g = new FrostAura;
		ld_es[FE_FROST_AURA].reset(g);
	}
	
	
	
	struct ModelInfo
	{
		std::vector<std::vector<vec2fp>> ls;
		bool fix_rotation = true;
		std::optional<size_t> ls_scalebox;
		
		vec2fp p_cpt = {};
		bool has_p_cpt = false; // set if defined
	};
	std::array<ModelInfo, MODEL_TOTAL_COUNT_INTERNAL> mlns;
	
	
	
	std::pair<ModelType, std::string> md_ns[] =
	{
	    {MODEL_ERROR, "error"},
	    {MODEL_WINRAR, "winrar"},
	    
	    {MODEL_PC_RAT, "rat"},
	    {MODEL_PC_SHLD, "pshl"},
	    
	    {MODEL_BOX_SMALL, "box_small"},
	    {MODEL_DRONE, "drone"},
	    {MODEL_WORKER, "worker"},
	    {MODEL_CAMPER, "camper"},
	    {MODEL_HUNTER, "hunter"},
	    {MODEL_HACKER, "hacker"},
	    
	    {MODEL_ARMOR, "armor"},
	    {MODEL_TERMINAL_KEY, "term_key"},
	    
	    {MODEL_DISPENSER, "dispenser"},
	    {MODEL_TERMINAL, "terminal"},
	    {MODEL_MINIDOCK, "minidock"},
	    {MODEL_TERMINAL_FIN, "term_final"},
	    
	    {MODEL_DOCKPAD, "dockpad"},
	    {MODEL_TELEPAD, "telepad"},
	    {MODEL_ASSEMBLER, "assembler"},
	    
	    {MODEL_MINEDRILL, "minedrill"},
	    {MODEL_MINEDRILL_MINI, "drill_mini"},
	    {MODEL_STORAGE, "storage"},
	    {MODEL_CONVEYOR, "conveyor"},
	    {MODEL_STORAGE_BOX, "ministor"},
	    {MODEL_STORAGE_BOX_OPEN, "ministor_alt"},
	    {MODEL_HUMANPOD, "humanpod"},
	    {MODEL_SCIENCE_BOX, "scibox"},
	    
	    {MODEL_BAT, "bat"},
	    {MODEL_HANDGUN, "claw"},
	    {MODEL_BOLTER, "bolter"},
	    {MODEL_GRENADE, "grenade"},
	    {MODEL_MINIGUN, "mgun"},
	    {MODEL_ROCKET, "rocket"},
	    {MODEL_ELECTRO, "electro"},
	    {MODEL_UBERGUN, "ubergun"},
	    
	    {MODEL_HANDGUN_AMMO, "claw_ammo"},
	    {MODEL_BOLTER_AMMO, "bolter_ammo"},
	    {MODEL_GRENADE_AMMO, "grenade_ammo"},
	    {MODEL_MINIGUN_AMMO, "mgun_ammo"},
	    {MODEL_ROCKET_AMMO, "rocket_ammo"},
	    {MODEL_ELECTRO_AMMO, "electro_ammo"},
	    
	    {MODEL_SPHERE, "sphere"}
	};
	
	// load models
	
	{	SVG_File svg = svg_read(HARDPATH_MODELS);
		
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
			std::string sbox = md.second + ":_s";
			bool any = false;
			for (auto& p : svg.paths)
			{
				if (p.id == md.second) {
					mlns[md.first].ls.emplace_back( std::move(p.ps) );
					any = true;
				}
				else if (p.id == sbox) {
					auto& m = mlns[md.first];
					m.ls_scalebox = m.ls.size();
					m.ls.emplace_back( std::move(p.ps) );
				}
			}
			if (!any)
				throw std::runtime_error(md.second + " - model not found");
			
			std::string cpname = md.second + ":_p";
			for (auto& p : svg.points)
			{
				if (p.id == cpname) {
					auto& m = mlns[md.first];
					m.p_cpt = p.pos;
					m.has_p_cpt = true;
					break;
				}
			}
		}
	}
	
	// copy models
	
	mlns[MODEL_ROCKET_PROJ] = mlns[MODEL_ROCKET_AMMO];
	
	// center models
	
	auto model_minmax = [](auto& m)
	{
		auto m0 = vec2fp::one( std::numeric_limits<float>::max() );
		auto m1 = vec2fp::one( std::numeric_limits<float>::lowest() );
		for (auto& s : m.ls) {
			for (auto& p : s) {
				m0 = min(m0, p);
				m1 = max(m1, p);
			}
		}
		return Rectfp{m0, m1, false};
	};
	
	for (auto& m : mlns)
	{
		vec2fp ctr = model_minmax(m).center();
		for (auto& s : m.ls)
			for (auto& p : s) p -= ctr;
		
		if (m.has_p_cpt) m.p_cpt -= ctr;
	}
	
	// rotate models (0 angle: top -> right)
	
	const int no_fix_rot[] =
	{
	    MODEL_ERROR,
	    MODEL_WINRAR,
	    
		MODEL_ARMOR,
	    MODEL_TERMINAL_KEY,
	    
	    MODEL_MINEDRILL_MINI,
	    
	    MODEL_HANDGUN_AMMO,
		MODEL_BOLTER_AMMO,
		MODEL_GRENADE_AMMO,
		MODEL_MINIGUN_AMMO,
		MODEL_ROCKET_AMMO,
		MODEL_ELECTRO_AMMO
	};
	for (auto i : no_fix_rot)
		mlns[i].fix_rotation = false;
	
	for (auto& m : mlns) {
		if (!m.fix_rotation) continue;
		for (auto& s : m.ls)
			for (auto& p : s)
				p.rot90ccw();
		m.p_cpt.rot90ccw();
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
		m.p_cpt *= radius / mr;
	};
	auto scale_d2 = [&](ModelType t, vec2fp radius) {
		auto& m = mlns[t];
		vec2fp mr = {};
		for (auto& s : m.ls)
			for (auto& p : s)
				mr = max(mr, {std::fabs(p.x), std::fabs(p.y)});
		for (auto& s : m.ls)
			for (auto& p : s)
				p *= radius / mr;
		m.p_cpt *= radius / mr;
	};
	
	scale_to(MODEL_PC_RAT, hsz_rat + PLAYER_MODEL_RADIUS_INCREASE);
	scale_d2(MODEL_PC_SHLD, hsz_pshl);
	
	scale_to(MODEL_BOX_SMALL, hsz_box_small);
	scale_to(MODEL_DRONE, hsz_drone);
	scale_to(MODEL_WORKER, hsz_drone_big);
	scale_to(MODEL_CAMPER, hsz_drone_big);
	scale_to(MODEL_HACKER, hsz_drone_big);
	scale_to(MODEL_HUNTER, hsz_drone_hunter);
	
	scale_to(MODEL_ARMOR, hsz_supply);
	scale_to(MODEL_TERMINAL_KEY, hsz_supply_big);
	
	scale_to(MODEL_DISPENSER, hsz_interact);
	scale_to(MODEL_TERMINAL, hsz_interact);
	scale_to(MODEL_MINIDOCK, hsz_interact);
	scale_to(MODEL_TERMINAL_FIN, hsz_termfin);
	
	scale_to(MODEL_DOCKPAD, hsz_cell_tmp);
	scale_to(MODEL_TELEPAD, hsz_cell_tmp);
	scale_to(MODEL_ASSEMBLER, hsz_cell_tmp);
	
	scale_d2(MODEL_MINEDRILL, {hsz_cell_tmp*2, hsz_cell_tmp});
	scale_to(MODEL_MINEDRILL_MINI, hsz_cell_tmp);
	scale_to(MODEL_STORAGE, hsz_cell_tmp);
	scale_to(MODEL_CONVEYOR, hsz_cell_tmp);
	scale_to(MODEL_STORAGE_BOX, hsz_interact);
	scale_to(MODEL_STORAGE_BOX_OPEN, hsz_interact);
	scale_to(MODEL_HUMANPOD, hsz_cell_tmp);
	scale_to(MODEL_SCIENCE_BOX, hsz_interact);
	
	scale_to(MODEL_HANDGUN_AMMO, hsz_supply);
	scale_to(MODEL_BOLTER_AMMO, hsz_supply);
	scale_to(MODEL_GRENADE_AMMO, hsz_supply);
	scale_to(MODEL_MINIGUN_AMMO, hsz_supply);
	scale_to(MODEL_ROCKET_AMMO, hsz_supply);
	scale_to(MODEL_ELECTRO_AMMO, hsz_supply);
	
	
	
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
	
	scale_to(MODEL_ROCKET_PROJ, hsz_proj);
	
	
	
	// offset weapons
	
	const int wpn_ixs[] = {
	    MODEL_BAT,
        MODEL_HANDGUN,
        MODEL_BOLTER,
        MODEL_GRENADE,
	    MODEL_MINIGUN,
	    MODEL_ROCKET,
	    MODEL_ELECTRO,
	    MODEL_UBERGUN
	};
	
	for (auto i : wpn_ixs)
	{
		float xmin = std::numeric_limits<float>::max();
		for (auto& s : mlns[i].ls) for (auto& p : s) xmin = std::min(xmin, p.x);
		for (auto& s : mlns[i].ls) for (auto& p : s) p.x -= xmin;
		mlns[i].p_cpt.x -= xmin;
	}
	
	
	
	// remove scaleboxes
	
	for (auto& m : mlns)
	{
		if (m.ls_scalebox)
			m.ls.erase( m.ls.begin() + *m.ls_scalebox );
	}
	
	
	
	// generate parts
	
	const int parts_aura[] = {
	    MODEL_PC_RAT,
        MODEL_PC_SHLD,
        MODEL_TELEPAD,
	    MODEL_HUNTER,
	    MODEL_ASSEMBLER
	};
	for (auto& i : parts_aura) {
		ld_me[i][ME_AURA].reset( new Aura(mlns[i].ls) );
	}
	
	
	
	// fin
	
	for (size_t i = MODEL_LEVEL_STATIC + 1; i < MODEL_TOTAL_COUNT_INTERNAL; ++i)
	{
		if (mlns[i].ls.empty() && i != MODEL_NONE)
			throw std::logic_error(std::to_string(i) + " - no model data (internal error)");
		
		ld_me[i][ME_DEATH].reset( new Death(mlns[i].ls) );
		md_cpt[i] = mlns[i].p_cpt;
		
		float width = 0.1f;
		if (std::find( std::begin(wpn_ixs), std::end(wpn_ixs), i ) != std::end(wpn_ixs))
			width = WEAPON_LINE_RADIUS;
		
		initres.ms[i].ls = mlns[i].ls; // used later in image generator
		initres.ms[i].width = width;
	}
	
	
	
	// set model sizes
	
	for (size_t i = MODEL_LEVEL_STATIC + 1; i < MODEL_TOTAL_COUNT_INTERNAL; ++i)
	{
		if (i != MODEL_NONE)
			md_sz[i] = model_minmax(mlns[i]);
	}
	
	
	
	// generate images
	
	initres.tex.reset(
	[&]()-> Texture* {
		TimeSpan time0 = TimeSpan::since_start();
	        
		struct Info
		{
			ModelType type;
	        int m_size;
	        FColor clr = FColor(1, 1, 1);
	        ImageInfo img = {};
		};
	    const int wpn_size = 52;
		Info md_is[] =
		{
			{MODEL_WINRAR, 40},
			{MODEL_MINIGUN, wpn_size, FColor(1.0, 0.7, 0.2)},
			{MODEL_ROCKET,  wpn_size, FColor(0.6, 1.0, 0.6)},
			{MODEL_ELECTRO, wpn_size, FColor(0.2, 0.8, 1.0)},
			{MODEL_GRENADE, wpn_size, FColor(1.0, 0.6, 0.4)},
			{MODEL_BOLTER,  wpn_size, FColor(0.6, 1.0, 0.2)},
			{MODEL_UBERGUN, wpn_size, FColor(1.0, 0.2, 1.0)}
		};

		ImageGlowGen glow;
		glow.mode = ImageGlowGen::M_NOISY;
		glow.maxrad = 3;
		glow.glow_k = 0.5;
	        
		AtlasBuilder abd;
		abd.pk.reset( new AtlasPacker );
		abd.pk->bpp = 4;
		abd.pk->min_size = 16;
		abd.pk->max_size = 4096;
		abd.pk->space_size = 2;
		
		for (size_t i=0; i < std::size(md_is); ++i)
		{
	        auto& inf = md_is[i];
	        
	        glow.shs.emplace_back().lines = std::move(mlns[inf.type].ls);
			glow.shs.back().clr = inf.clr;
			
			vec2i sz = vec2i::one(inf.m_size * 2);
			inf.img = glow.gen(sz);
			
			downscale_2x(inf.img);
			
			sz = inf.img.get_size();
			abd.add_static({i, sz.x, sz.y}, inf.img.raw());
		}
	    
		auto as = abd.build();
		ImageInfo res;
		
		if (as.empty()) {
			VLOGE("Resbase:: no images");
			return {};
		}
		
		res.reset({ as[0].info.w, as[0].info.h });
		std::memcpy(res.raw(), as[0].px.data(), as[0].px.size() );
		
		VLOGI("Resbase:: generated images in {:.3f} seconds, {}x{} atlas",
		      (TimeSpan::since_start() - time0).seconds(), res.get_size().x, res.get_size().y);
		
		Texture* tx;
		RenderControl::get().exec_task([&]{
			tx = Texture::create_from(res);
		});
		
		for (auto& a : as[0].info.sprs)
		{
			auto& inf = md_is[a.id];
			auto& t = md_img[inf.type];
			t.tex = tx;
			t.tc.a = vec2fp(a.x, a.y) / vec2fp(res.get_size());
			t.tc.b = vec2fp(a.w, a.h) / vec2fp(res.get_size()) + t.tc.a;
		}
		return tx;
	}());

	return initres;
}
