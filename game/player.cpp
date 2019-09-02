#include "client/presenter.hpp"
#include "core/plr_control.hpp"
#include "render/ren_aal.hpp"
#include "vaslib/vas_log.hpp"
#include "game_core.hpp"
#include "movement.hpp"
#include "physics.hpp"
#include "player.hpp"
#include "weapon.hpp"

#include "render/camera.hpp"
#include "render/control.hpp"
#include "render/ren_imm.hpp"



struct PlayerRender : ECompRender
{
	struct Attach
	{
		ModelType model = MODEL_NONE;
		Transform at;
		FColor clr;
	};
	std::array<Attach, ATT_TOTAL_COUNT> atts;
	
	bool show_ray = false;
	vec2fp tar_ray = {};
	std::optional<std::pair<vec2fp, vec2fp>> tar_next;
	float tar_next_t;
	
	float angle = 0.f; // override
	
	std::string dbg_info;
	std::string dbg_info_real;
	
	
	
	PlayerRender(Entity* ent): ECompRender(ent) {}
	void on_destroy() override
	{
		parts(MODEL_PC_RAT, ME_DEATH, {});
	}
	void step() override
	{
		const Transform fixed{get_pos().pos, angle};
		RenAAL::get().draw_inst(fixed, FColor(0.4, 0.9, 1, 1), MODEL_PC_RAT);
		
		for (auto& a : atts) {
			if (a.model != MODEL_NONE)
				RenAAL::get().draw_inst(fixed.get_combined(a.at), a.clr, a.model);
		}
		
		if (show_ray)
		{
			vec2fp src = get_pos().pos;
			
			vec2fp dt = tar_ray - src;
			dt.norm_to( ent->get_phy().get_radius() );
			
			RenAAL::get().draw_line(src + dt, tar_ray, FColor(1, 0, 0, 0.6).to_px(), 0.07, 1.5f);
			
			if (tar_next) {
				float d = GamePresenter::get()->get_passed().seconds() / GameCore::get().step_len.seconds();
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
		
		if (true)
		{
			float k = 1.f / RenderControl::get().get_world_camera()->get_state().mag;
			RenImm::get().draw_text( get_pos().pos, dbg_info_real, RenImm::White, false, k );
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
	void sync() override
	{
		dbg_info_real = std::move(dbg_info);
	}
	void set_ray_tar(vec2fp new_tar)
	{
		auto cur = get_pos().pos;
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
};



struct PlayerLogic : EComp
{
	EVS_SUBSCR;
	std::shared_ptr<PlayerController> ctr;
	
	const float push_angle = deg_to_rad(60.f); // pushoff params
	const float push_imp_max = 120.f;
	
	const float spd_norm = 8; // movement speed
	const float spd_accel = 17; // 14
	
	const float min_tar_dist = 1.2; // minimal target distance
	vec2fp prev_tar; // used if current dist < minimal
	
	bool side_col[4] = {}; // side collision flag
	
	
	
	PlayerLogic(Entity* ent, std::shared_ptr<PlayerController> ctr):
	    EComp(ent), ctr(std::move(ctr))
	{
		reg(ECompType::StepLogic);
		
		auto& phy = dynamic_cast<EC_Physics&>(ent->get_phy());
		EVS_CONNECT1(phy.ev_contact, on_cnt);
		
		// set sidecol
		
		b2FixtureDef fd;
		fd.isSensor = true;
		
		const float off = GameConst::hsz_rat - 0.05;
		const float len = GameConst::hsz_rat - 0.1;
		const float wid = spd_accel * GameCore::time_mul + 0.1;
		
		for (int i=0; i<4; ++i)
		{
			auto sz  = vec2fp(wid, len).get_rotated( M_PI_2 * i );
			auto pos = vec2fp(off, 0)  .get_rotated( M_PI_2 * i );
			
			fd.userData = reinterpret_cast<void*> (i + 1);
			phy.add_box(fd, sz, 1, Transform{pos});
		}
	}
	void step() override;
	void on_cnt(const ContactEvent& ce)
	{
		if (ce.fix_this && dynamic_cast<EC_Physics&>(ce.other->get_phy()).body->GetType() == b2_staticBody)
		{
			auto i = -1 + reinterpret_cast<intptr_t> (ce.fix_this);
			if		(ce.type == ContactEvent::T_BEGIN) side_col[i] = true;
			else if (ce.type == ContactEvent::T_END)   side_col[i] = false;
		}
	}
	void set_mov_vel(vec2fp dir, bool is_accel);
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
		bd.fixedRotation = true;
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
		b2FixtureDef fd;
		fd.friction = 0.3;
		fd.restitution = 0.5;
		phy.add_circle(fd, GameConst::hsz_rat, 15.f);
		
		eqp.wpns.emplace_back( Weapon::create_std(WeaponIndex::Minigun) );
		eqp.wpns.emplace_back( Weapon::create_std(WeaponIndex::Rocket) );
		eqp.set_wpn(0);
		
		mov.damp_lin = 2.f;
		mov.dust_vel = log.spd_accel - 1;
		
		log.prev_tar = phy.get_pos() + vec2fp(1, 0);
	}
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
	EC_Health*    get_hlc() override {return &hlc;}
	EC_Equipment* get_eqp() override {return &eqp;}
	size_t get_team() const override {return TEAM_PLAYER;}
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
	
	for (auto& a : cst.acts)
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
		else if (a == PlayerController::A_LASER_DESIG)
			self->ren.show_ray = !self->ren.show_ray;
	}
	
	set_mov_vel(cst.mov, accel);
	
	auto spos = self->phy.get_pos();
	auto tar = cst.tar_pos;
	
	if (spos.dist(tar) < min_tar_dist)
		tar = prev_tar;
	prev_tar = tar;
	
	if (shooting)
		eqp.shoot(tar);
	
	self->ren.angle = (tar - spos).angle();
	
	if (self->ren.show_ray)
	{
		tar -= spos;
		tar.norm();
		
		if (auto r = GameCore::get().get_phy().raycast_nearest(conv(spos), conv(spos + 1000.f * tar)))
			tar = conv(r->poi);
		else
			tar = spos + tar * 1.5f;
		
		self->ren.set_ray_tar(tar);
	}
}
void PlayerLogic::set_mov_vel(vec2fp dir, bool is_accel)
{
	auto& mov = static_cast<PlayerEntity*>(ent)->mov;
	
	vec2fp vd = prev_tar - ent->get_phy().get_pos();
	if (vd.len2() > 0.01) vd.norm_to(0.2);
	vd = {};
	
	if (dir.x >  0.01 && side_col[0]) {dir.x = 0; dir.y += vd.y;}
	if (dir.y >  0.01 && side_col[1]) {dir.y = 0; dir.x += vd.x;}
	if (dir.x < -0.01 && side_col[2]) {dir.x = 0; dir.y += vd.y;}
	if (dir.y < -0.01 && side_col[3]) {dir.y = 0; dir.x += vd.x;}
	
	if (dir.len2() > 0.01) dir.norm_to(is_accel ? spd_accel : spd_norm);
	mov.dec_inert = TimeSpan::seconds(is_accel ? 2 : 1);
	mov.set_app_vel(dir);
}
