#include "core/hard_paths.hpp"
#include "render/postproc.hpp"
#include "render/ren_aal.hpp"
#include "render/ren_imm.hpp"
#include "utils/noise.hpp"
#include "effects.hpp"
#include "presenter.hpp"

static bool effects_init_called = false;
static std::unique_ptr<Texture> tex_explowave;

void effects_init()
{
	if (effects_init_called) return;
	effects_init_called = true;
	
	tex_explowave.reset( Texture::load(HARDPATH_EXPLOSION_IMG) );
}



struct GRE_Lighting : GameRenderEffect
{
	vec2fp a, b; // only for init
	EffectLightning type;
	
	float t = 1, tps;
	FColor clr;
	
	struct Line {
		vec2fp a, b;
		float r;
	};
	std::vector<Line> ls;
	
	void init() override
	{
		if (type == EffectLightning::Straight)
		{
			auto& ln = ls.emplace_back();
			ln.a = a;
			ln.b = b;
			ln.r = 1;
		}
		else
		{
			constexpr float min_len = 0.7;
			constexpr float min_squ = min_len * min_len;
			
			if (rnd_stat().range_n() < 0.3) {
				float t = rnd_stat().range_n();
				tps *= lerp(0.3, 0.6, t);
				clr += 0.15 * t;
				clr.a = lerp(3, 1.5, t);
			}
			
			float total_squ = a.dist_squ(b);
			ls.reserve( 6 + 1.f / ((min_len /2) * fast_invsqrt(total_squ)) );
			
			if (type == EffectLightning::First)
			{
				auto rvec = [] {
					vec2fp p(rnd_stat().range(0.2, 1), 0);
					p.fastrotate( rnd_stat().range_n2() * M_PI );
					return p;
				};
				ls.push_back({a, b + rvec(), 0.5});
				ls.push_back({a, b + rvec(), 0.1});
			}
			ls.push_back({a, b, 1});
			
			for (size_t i=0; i<ls.size(); ++i)
			{
				auto& ln = ls[i];
				float sl = ln.a.dist_squ(ln.b);
				if (sl > min_squ)
				{
					float d = rnd_stat().range(0.2, 0.8);
					float rot_k = std::max(min_len * fast_invsqrt(sl), 0.4f);
					
					vec2fp c = (ln.b - ln.a) * d;
					c.fastrotate( rnd_stat().range_n2() * (M_PI/2 * rot_k) );
					c += ln.a;
					
					vec2fp nb = ln.b;
					ln.b = c;
					ls.push_back({c, nb, ln.r});
				}
			}
		}
	}
	bool render(TimeSpan passed) override
	{
		for (auto& l : ls)
			RenAAL::get().draw_line(l.a, l.b, clr.to_px(), 0.1f, l.r * 5.f, l.r * t * 2.5);
		return (t -= tps * passed.seconds()) > 0;
	}
};
void effect_lightning(vec2fp a, vec2fp b, EffectLightning type, TimeSpan length, FColor clr)
{
	float total_squ = a.dist_squ(b);
	if (total_squ < 0.1) return;
	
	auto p = new GRE_Lighting;
	p->a = a;
	p->b = b;
	p->type = type;
	p->tps = 1 / length.seconds();
	p->clr = clr;
	GamePresenter::get()->add_effect(std::unique_ptr<GameRenderEffect>(p));
}



struct GRE_Explowave : GameRenderEffect
{
	static constexpr float t_dur = 1; // duration, seconds
	static constexpr float r_max = 3.5; // max radius, meters
	static constexpr float a_thr = 0.4; // fade below this 't'
	
	vec2fp pos;
	float t = 1, tps, szk;
	
	GRE_Explowave(vec2fp ctr, float power)
	{
		pos = ctr;
		tps = 1 / (power * t_dur);
		szk = r_max * power;
	}
	bool render(TimeSpan passed)
	{
		int a;
		if (t > a_thr) a = 255 * lerp(1 - a_thr, 1, (t - a_thr) / (1 - a_thr));
		else a = 255 * (1 - a_thr) * (t / a_thr);
		RenImm::get().draw_image( Rectfp::from_center(pos, vec2fp::one( (1 - t*t*t) * szk )), tex_explowave.get(), 0xffffff00 | a );
		return (t -= tps * passed.seconds()) > 0;
	}
};
void effect_explosion_wave(vec2fp ctr, float power)
{
	if (GamePresenter::get()->get_vport().contains( ctr ))
	{
		GamePresenter::get()->add_effect(std::make_unique<GRE_Explowave>(ctr, power));
		Postproc::get().screen_shake(power);
	}
}
