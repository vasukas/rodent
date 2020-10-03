#include "game/game_core.hpp"
#include "game/physics.hpp"
#include "render/ren_aal.hpp"
#include "render/ren_imm.hpp"
#include "render/ren_light.hpp"
#include "utils/time_utils.hpp"
#include "ec_render.hpp"
#include "effects.hpp"
#include "presenter.hpp"



EC_RenderPos::EC_RenderPos(Entity& ent)
    : EDynComp(ent)
{
	pos = ent.ref_pc().get_trans();
	GamePresenter::get()->on_add(*this);
}
EC_RenderPos::~EC_RenderPos()
{
	GamePresenter::get()->on_rem(*this);
}
void EC_RenderPos::parts(PGG_Pointer gen, const ParticleBatchPars& pars)
{
	if (gen) GamePresenter::get()->add_cmd(PresCmdParticles{ ent.index, gen.p, pars });
}



EC_RenderComp::EC_RenderComp(Entity& ent)
	: EDynComp(ent)
{
	ent.ensure<EC_RenderPos>();
	GamePresenter::get()->on_add(*this);
}
EC_RenderComp::~EC_RenderComp()
{
	GamePresenter::get()->on_rem(*this);
}
void EC_RenderComp::tmp_cull(bool& state, bool disable_culling)
{
	if (state != disable_culling) {
		ent.ref<EC_RenderPos>().disable_culling += disable_culling ? 1 : -1;
		state = disable_culling;
	}
}



EC_RenderModel::~EC_RenderModel()
{
	auto& r = ent.ref<EC_RenderPos>();
	
	if (death == DEATH_PARTICLES)
	{
		r.parts({model, ME_DEATH}, {{}, 1, clr});
	}
	else if (death == DEATH_AND_EXPLOSION)
	{
		r.parts({model, ME_DEATH}, {{}, 1, clr});
		GamePresenter::get()->effect( FE_EXPLOSION_FRAG, { Transform{r.get_cur()} } );
		effect_explosion_wave( r.get_cur().pos );
	}
}
void EC_RenderModel::parts(ModelEffect effect, const ParticleBatchPars& pars)
{
	ent.ref<EC_RenderPos>().parts({model, effect}, pars);
}
void EC_RenderModel::render(const EC_RenderPos& p, TimeSpan)
{
	RenAAL::get().draw_inst(p.get_cur(), clr, model);
}



void EC_RenderEquip::attach(AttachType type, Transform at, ModelType model, FColor clr)
{
	auto& a = atts[type];
	a.model = model;
	a.at = at;
	a.clr = clr;
}
void EC_RenderEquip::render(const EC_RenderPos& p, TimeSpan)
{
	auto& tr = p.get_cur();
	for (auto& a : atts) {
		if (a.model != MODEL_NONE)
			RenAAL::get().draw_inst(tr.get_combined(a.at), a.clr, a.model);
	}
}



void EC_ParticleEmitter::Channel::once(PGG_Pointer gen, const ParticleBatchPars& pars, std::optional<TimeSpan> new_period)
{
	if (!pe() || !gen.p) return;
	if (after > TimeSpan::since_start()) return;
	
	after = TimeSpan::since_start() + new_period.value_or(period);
	pe()->ent.ref<EC_RenderPos>().parts(gen, pars);
}
bool EC_ParticleEmitter::Channel::is_playing() const
{
	return !!em || (pe() && after > TimeSpan::since_start());
}
void EC_ParticleEmitter::Channel::play(PGG_Pointer gen, const ParticleBatchPars& pars, TimeSpan period, TimeSpan total_time)
{
	em.emplace(gen, pars, period, total_time);
	after = TimeSpan::since_start() + total_time + period;
}
void EC_ParticleEmitter::Channel::stop(bool force)
{
	em.reset();
	if (force) after = {};
}



EC_ParticleEmitter::Emit::Emit(PGG_Pointer gen, const ParticleBatchPars& pars, TimeSpan period, TimeSpan total_time)
{
	this->period = period;
	this->tmo = {};
	this->left = std::ceil( total_time / period );
	this->gen = gen.p;
	this->pars = pars;
	this->tr = pars.tr;
}
void EC_ParticleEmitter::effect(PGG_Pointer gen, const ParticleBatchPars& pars, TimeSpan period, TimeSpan total_time)
{
	if (gen) ems.emplace_back(gen, pars, period, total_time);
}
std::unique_ptr<EC_ParticleEmitter::Channel> EC_ParticleEmitter::new_channel()
{
	return std::unique_ptr<Channel>(chs.new_ref());
}
void EC_ParticleEmitter::render(const EC_RenderPos& p, TimeSpan passed)
{
	auto proc = [&](Emit& e)
	{
		if (e.tmo.is_positive()) {
			e.tmo -= passed;
			return true;
		}
		
		e.tmo += e.period;
		e.pars.tr = p.get_cur().get_combined(e.tr);
		e.gen->draw(e.pars);
		
		if (auto w = GamePresenter::get()->net_writer)
			w->on_pgg(e.gen, e.pars);
		
		if (!e.left) return false;
		--e.left;
		return true;
	};
	
	for (auto it = ems.begin(); it != ems.end(); )
	{
		if (proc(*it)) ++it;
		else it = ems.erase(it);
	}
	for (auto& c : chs) {
		if (c && c->em && !proc(*c->em))
			c->em.reset();
	}
}
void EC_ParticleEmitter::resource_reinit_hack()
{
	ems.clear();
	for (auto& c : chs)
		c->em.reset();
}



void EC_LaserDesigRay::set_target(vec2fp new_tar)
{
	auto cur =ent.ref<EC_RenderPos>().get_cur().pos;
	float ad = (new_tar - cur).angle() - (tar_ray - cur).angle();
	
	if (std::fabs(wrap_angle(ad)) < deg_to_rad(25))
	{
		tar_next = {tar_ray, new_tar};
		tar_next_t = 0;
	}
	else {
		tar_next.reset();
		tar_ray = new_tar;
	}
}
void EC_LaserDesigRay::find_target(vec2fp dir)
{
	vec2fp spos = ent.get_pos();
	
	b2Filter ft;
	ft.maskBits = ~(EC_Physics::CF_BULLET);
	
	if (auto r = ent.core.get_phy().raycast_nearest(conv(spos), conv(spos + 1000.f * dir), {{}, ft}))
		set_target(conv(r->poi));
	else
		set_target(spos + dir * 1.5f);
}
void EC_LaserDesigRay::render(const EC_RenderPos& ecp, TimeSpan passed)
{
	tmp_cull(cull_state, enabled);
	if (!enabled) return;
	vec2fp src = ecp.get_cur().pos;
	
	vec2fp dt = tar_ray - src;
	dt.norm_to( ent.ref_pc().get_radius() );
	
	RenAAL::get().draw_line(src + dt, tar_ray, clr.to_px(), 0.06, 1.f, std::max(clr.a, 1.f));
	
	if (tar_next) {
		float d = passed.seconds() / ent.core.step_len.seconds();
		tar_next_t += d;
		if (tar_next_t < 1) {
			tar_ray = lerp(tar_next->first, tar_next->second, tar_next_t);
		}
		else {
			tar_ray = tar_next->second;
			tar_next.reset();
		}
	}
}



void EC_Uberray::trigger(vec2fp target)
{
	left = left_max;
	b_last = target;
}
void EC_Uberray::render(const EC_RenderPos& p, TimeSpan passed)
{
	tmp_cull(cull_state, left.is_positive());
	if (!left.is_positive()) return;
	
	vec2fp a = p.get_cur().pos;
	vec2fp b = b_last;
	
	if (left < left_max) {
		vec2fp v((b - a).fastlen() * (left / left_max), 0);
		v.fastrotate( p.get_cur().rot );
		b = a + v;
	}
	left -= passed;
	
	float ta = time_sine(TimeSpan::seconds(2), 0.85);
	RenAAL::get().draw_line(a, b, clr.to_px() | 0xff, 0.2, 4, left / left_max * clr.a * ta);
}



void EC_RenderDoor::render(const EC_RenderPos& p, TimeSpan)
{
	vec2fp p0 = p.get_cur().pos - fix_he;
	vec2fp p1 = p.get_cur().pos + fix_he;
	
	vec2fp o_len, o_wid; // offsets
	if (is_x_ext)
	{
		o_len.set( lerp(min_off, fix_he.x, t_ext), 0 );
		o_wid.set( 0, fix_he.y *2 );
	}
	else
	{
		o_len.set( 0, lerp(min_off, fix_he.y, t_ext) );
		o_wid.set( fix_he.x *2, 0 );
	}

	//
	
	float l_wid = 0.1f;
	float l_aa = 3.f;
	float l_off = l_wid * 1;
	
	p0 += o_len * l_off;
	p1 -= o_len * l_off;
	o_len *= 1 - l_off;
	
	RenAAL::get().draw_line(p0,         p0 + o_len,         l_clr.to_px(), l_wid, l_aa);
	RenAAL::get().draw_line(p0 + o_wid, p0 + o_len + o_wid, l_clr.to_px(), l_wid, l_aa);
	RenAAL::get().draw_line(p0 + o_len, p0 + o_len + o_wid, l_clr.to_px(), l_wid, l_aa);
	
	RenAAL::get().draw_line(p1,         p1 - o_len,         l_clr.to_px(), l_wid, l_aa);
	RenAAL::get().draw_line(p1 - o_wid, p1 - o_len - o_wid, l_clr.to_px(), l_wid, l_aa);
	RenAAL::get().draw_line(p1 - o_len, p1 - o_len - o_wid, l_clr.to_px(), l_wid, l_aa);
}



EC_LightSource::EC_LightSource(Entity& ent): EDynComp(ent) {}
EC_LightSource::~EC_LightSource() = default;
void EC_LightSource::add(vec2fp offset, float angle, FColor clr, float radius) {
	lights.emplace_back();
	lights.back().set_color(clr);
	lights.back().set_type(ent.get_pos() + offset, radius, angle);
}
