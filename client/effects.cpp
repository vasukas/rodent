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
	
	tex_explowave.reset( Texture::load("res/explosion_wave.png") );
}



void effect_lightning(vec2fp a, vec2fp b, EffectLightning type, TimeSpan length, FColor clr)
{
	float total_squ = a.dist_squ(b);
	if (total_squ < 0.1) return;
	
	struct Line {
		vec2fp a, b;
		float r;
	};
	struct Effect {
		std::vector<Line> ls;
		float t = 1, tps;
		FColor clr;
		
		bool operator()(TimeSpan passed) {
			for (auto& l : ls)
				RenAAL::get().draw_line(l.a, l.b, clr.to_px(), 0.1f, l.r * 5.f, l.r * t * 2.5);
			return (t -= tps * passed.seconds()) > 0;
		}
	};
	
	Effect eff;
	eff.tps = 1 / length.seconds();
	eff.clr = clr;
	
	if (type == EffectLightning::Straight)
	{
		auto& ln = eff.ls.emplace_back();
		ln.a = a;
		ln.b = b;
		ln.r = 1;
	}
	else
	{
		constexpr float min_len = 0.7;
		constexpr float min_squ = min_len * min_len;
		
		auto& ls = eff.ls;
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
	
	GamePresenter::get()->add_effect(std::move(eff));
}



void effect_explosion_wave(vec2fp ctr, float power)
{
	constexpr float t_dur = 1; // duration, seconds
	constexpr float r_max = 3.5; // max radius, meters
	constexpr float a_thr = 0.4; // fade below this 't'
	
	struct Effect
	{
		vec2fp pos;
		float t = 1, tps, szk;
		
		Effect(vec2fp ctr, float power) {
			pos = ctr;
			tps = 1 / (power * t_dur);
			szk = r_max * power;
		}
		bool operator()(TimeSpan passed) {
			int a;
			if (t > a_thr) a = 255 * lerp(1 - a_thr, 1, (t - a_thr) / (1 - a_thr));
			else a = 255 * (1 - a_thr) * (t / a_thr);
			RenImm::get().draw_image( Rectfp::from_center(pos, vec2fp::one( (1 - t*t*t) * szk )), tex_explowave.get(), 0xffffff00 | a );
			return (t -= tps * passed.seconds()) > 0;
		}
	};
	GamePresenter::get()->add_effect(Effect(ctr, power));
	
	Postproc::get().screen_shake(power);
}
