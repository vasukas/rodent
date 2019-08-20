#include "client/presenter.hpp"
#include "core/plr_control.hpp"
#include "render/ren_aal.hpp"
#include "vaslib/vas_log.hpp"
#include "game_core.hpp"
#include "movement.hpp"
#include "physics.hpp"
#include "player.hpp"
#include "weapon.hpp"



struct PlayerRender : ECompRender
{
	struct Attach
	{
		ModelType model = MODEL_NONE;
		Transform at;
		FColor clr;
	};
	std::array<Attach, ATT_TOTAL_COUNT> atts;
	
	bool show_ray = true;
	vec2fp tar_ray = {};
	
	
	
	PlayerRender(Entity* ent): ECompRender(ent) {}
	void on_destroy() override
	{
		parts(MODEL_PC_RAT, ME_DEATH, {});
	}
	void step() override
	{
		RenAAL::get().draw_inst(get_pos(), FColor(0.4, 0.9, 1, 1), MODEL_PC_RAT);
		
		for (auto& a : atts) {
			if (a.model != MODEL_NONE)
				RenAAL::get().draw_inst(get_pos().get_combined(a.at), a.clr, a.model);
		}
		
		if (show_ray)
		{
			vec2fp src = get_pos().pos;
			
			vec2fp dt = tar_ray - src;
			dt.norm_to( ent->get_phy().get_radius() );
			
			RenAAL::get().draw_line(src + dt, tar_ray, FColor(1, 0, 0, 1).to_px(), 0.07, 1.5f);
		}
	}
	void proc(const PresCommand& c) override
	{
		if (c.type == PresCommand::T_ATTACH)
		{
			auto& a = atts[c.ix0];
			a.model = static_cast<ModelType>(c.ix1);
			a.at = c.pos;
			a.clr = c.clr;
		}
		else THROW_FMTSTR("PlayerRender::proc() not implemented ({})", ent->dbg_id());
	}
};



struct PlayerLogic : EComp
{
	EVS_SUBSCR;
	std::shared_ptr<PlayerController> ctr;
	
	const float push_angle = deg_to_rad(60.f); // pushoff params
	const float push_imp_max = 120.f;
	
	const float spd_norm = 8; // movement speed
	const float spd_accel = 14;
	
	const float min_tar_dist = 0.5; // minimal target distance
	
	
	
	PlayerLogic(Entity* ent, std::shared_ptr<PlayerController> ctr):
	    EComp(ent), ctr(std::move(ctr))
	{
		reg(ECompType::StepLogic);
//		EVS_CONNECT1(dynamic_cast<EC_Physics&>(ent->get_phy()).ev_contact, on_cnt);
	}
	void step() override;
	void on_cnt(const ContactEvent& ce)
	{
		if (ce.type != ContactEvent::T_BEGIN) return;
		
		vec2fp rpos = ce.other->get_phy().get_pos() - ent->get_phy().get_pos();
		float ang = std::fabs (wrap_angle (rpos.angle()));
		if (ang > push_angle) return;
		
		float k = push_imp_max;
		k *= lerp(1, .5f, ang / push_angle);
		ce.other->get_phobj().body->ApplyLinearImpulseToCenter(k * conv(rpos), true);
		
		Transform at;
		at.rot = rpos.angle();
		at.pos = ent->get_phy().get_pos() + vec2fp(ent->get_phy().get_radius(), 0).get_rotated(at.rot);
		GamePresenter::get()->effect(FE_SHOT_DUST, {at});
	}
};



class PlayerEntity final : public Entity
{
public:
	EC_Physics   phy;
	PlayerRender ren;
	EC_Movement  mov;
	EC_Health    hlc;
	EC_Equipment eqp;
	PlayerLogic  log;
	
	static b2BodyDef ph_def(vec2fp pos)
	{
		b2BodyDef bd;
		bd.type = b2_dynamicBody;
		bd.position = conv(pos);
		return bd;
	}
	PlayerEntity(vec2fp pos, std::shared_ptr<PlayerController> ctr)
	    :
	    phy(this, ph_def(pos)),
	    ren(this),
	    mov(this),
	    hlc(this),
	    eqp(this),
	    log(this, std::move(ctr))
	{
		dbg_name = "Player";
		
		b2FixtureDef fd;
		fd.friction = 0.3;
		fd.restitution = 0.5;
		phy.add_circle(fd, GameConst::hsz_rat, 15.f);
		
		eqp.wpns.emplace_back( Weapon::create_std(WeaponIndex::Minigun) );
		eqp.wpns.emplace_back( Weapon::create_std(WeaponIndex::Rocket) );
		eqp.set_wpn(0);
		
		mov.damp_lin = 2.f;
	}
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
	EC_Health*    get_hlc() override {return &hlc;}
	EC_Equipment* get_eqp() override {return &eqp;}
	size_t get_team() const override {return 1;}
};
Entity* create_player(vec2fp pos, std::shared_ptr<PlayerController> ctr)
{
	return new PlayerEntity(pos, std::move(ctr));
}



void PlayerLogic::step()
{
	auto self = static_cast<PlayerEntity*>(ent);
	auto& eqp = self->eqp;
	
	auto ctr_lock = ctr->lock();
	ctr->update();
	
	auto& cst = ctr->get_state();
	
	bool accel    = cst.is[PlayerController::A_ACCEL];
	bool shooting = cst.is[PlayerController::A_SHOOT];
	
	for (auto& a : ctr->get_acts())
	{
		if		(a == PlayerController::A_WPN_PREV)
		{
			size_t i = eqp.wpn_index();
			if (i != size_t_inval && i) --i;
			eqp.set_wpn(i);
		}
		else if (a == PlayerController::A_WPN_NEXT)
		{
			size_t i = eqp.wpn_index();
			if (i != size_t_inval && i != eqp.wpns.size() - 1) ++i;
			eqp.set_wpn(i);
		}
		else if (a == PlayerController::A_WPN_1) eqp.set_wpn(0);
		else if (a == PlayerController::A_WPN_2) eqp.set_wpn(1);
	}
	
	auto& mov = self->mov;
	vec2fp mv = cst.mov;
	mv *= accel ? spd_accel : spd_norm;
	
	mov.dec_inert = TimeSpan::seconds(accel ? 2 : 1);
	mov.set_app_vel(mv);
	
	auto spos = self->phy.get_pos();
	auto tar = cst.tar_pos;
	
	if (spos.dist(tar) < min_tar_dist)
		tar = spos + self->phy.get_norm_dir();
	
	float ta = (tar - spos).angle();
	float ca = self->phy.body->GetAngle();
	if (std::fabs(wrap_angle(ta - ca)) > 0.1)
	{
		float a = std::remainder(ta - ca, M_PI*2) / M_PI / GameCore::time_mul;
		self->phy.body->ApplyAngularImpulse(a, true);
	}
	
	if (shooting)
		eqp.shoot(tar);
	
	if (self->ren.show_ray)
	{
		tar -= spos;
		tar.norm();
		
		if (auto r = GameCore::get().get_phy().raycast_nearest(conv(spos), conv(spos + 1000.f * tar)))
			tar = conv(r->poi);
		else
			tar = spos + tar * 1.5f;
		
		self->ren.tar_ray = tar;
	}
}
