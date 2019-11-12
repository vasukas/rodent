#include "game_ai/ai_group.hpp"
#include "render/ren_aal.hpp"
#include "utils/noise.hpp"
#include "vaslib/vas_log.hpp"
#include "game_core.hpp"
#include "player_mgr.hpp"
#include "s_objs.hpp"
#include "weapon_all.hpp"



EWall::EWall(const std::vector<std::vector<vec2fp>>& walls)
    :
    phy(this, b2BodyDef{}),
    ren(this, MODEL_LEVEL_STATIC, FColor(0.75, 0.75, 0.75, 1))
{
	ren.disable_culling = true;
	
	std::vector<b2Vec2> verts;
	vec2fp p0 = vec2fp::one(std::numeric_limits<float>::max());
	vec2fp p1 = vec2fp::one(std::numeric_limits<float>::lowest());
	
	auto addfix = [&](const std::vector<vec2fp>& w)
	{
		verts.clear();
		verts.reserve(w.size());
		for (auto& p : w) {
			verts.push_back(conv(p));
			p0 = min(p, p0);
			p1 = max(p, p1);
		}
		
		bool loop = w.front().equals( w.back(), 1e-5 );
		
		b2ChainShape shp;
		if (loop) shp.CreateLoop(verts.data(), verts.size() - 1);
		else shp.CreateChain(verts.data(), verts.size());
		
		b2FixtureDef fd;
		fd.friction = 0.15;
		fd.restitution = 0.1;
		fd.shape = &shp;
		auto f = phy.body->CreateFixture(&fd);
		
		setptr(f, new FixtureInfo( FixtureInfo::TYPEFLAG_WALL | FixtureInfo::TYPEFLAG_OPAQUE ));
	};
	for (auto& w : walls) addfix(w);
	
	auto& lc = LevelControl::get();
	p0 = {};
	p1 = lc.get_size() * lc.cell_size;
	p0 += vec2fp::one( lc.cell_size/2 );
	p1 += vec2fp::one( lc.cell_size/2 );
	
	std::vector<vec2fp> enc = {
	    {p0.x, p0.y},
	    {p1.x, p0.y},
	    {p1.x, p1.y},
	    {p0.x, p1.y},
	    {p0.x, p0.y},
	};
	addfix(enc);
}



static b2BodyDef EPhyBox_bd(vec2fp at)
{
	b2BodyDef bd;
	bd.type = b2_dynamicBody;
	bd.position = conv(at);
	return bd;
}
EPhyBox::EPhyBox(vec2fp at)
    :
    phy(this, EPhyBox_bd(at)),
    ren(this, MODEL_BOX_SMALL, FColor(1, 0.6, 0.2, 1)),
    hlc(this, 250)
{
	b2FixtureDef fd;
	fd.friction = 0.1;
	fd.restitution = 0.5;
	phy.add_box(fd, vec2fp::one(GameConst::hsz_box_small), 40.f, new FixtureInfo( FixtureInfo::TYPEFLAG_OPAQUE ));
	
	hlc.hook(phy);
}



static ModelType get_model(const EPickable::Value& val)
{
	if		(auto v = std::get_if<EPickable::AmmoPack>(&val)) return ammo_model(v->type);
	else if (auto v = std::get_if<EPickable::Func>    (&val)) return v->model;
	return MODEL_ERROR;
}
static FColor get_color(const EPickable::Value& val)
{
	if		(         std::get_if<EPickable::AmmoPack>(&val)) return FColor(1, 1, 1);
	else if (auto v = std::get_if<EPickable::Func>    (&val)) return v->clr;
	return FColor(1, 1, 1);
}
EPickable::AmmoPack EPickable::std_ammo(AmmoType type)
{
	AmmoPack ap = {type, 0};
	switch (type)
	{
	case AmmoType::Bullet: ap.amount = 50; break;
	case AmmoType::Rocket: ap.amount = 10; break;
	case AmmoType::Energy: ap.amount = 20; break;

	case AmmoType::None:
	case AmmoType::TOTAL_COUNT:
		break;
	}
	return ap;
}
EPickable::EPickable(vec2fp pos, Value val)
	:
    phy(this, [&]{b2BodyDef d; d.position = conv(pos); return d;}()),
    ren(this, get_model(val), get_color(val)),
    val(std::move(val))
{
	b2FixtureDef fd;
	fd.isSensor = true;
	phy.add_circle(fd, GameConst::hsz_supply, 0);
	EVS_CONNECT1(phy.ev_contact, on_cnt);
}
void EPickable::on_cnt(const CollisionEvent& ce)
{
	if (ce.type != CollisionEvent::T_BEGIN) return;
	if (!GameCore::get().get_pmg().is_player( ce.other )) return;
	
	if (auto v = std::get_if<AmmoPack>(&val))
	{
		auto eqp = ce.other->get_eqp();
		float delta = eqp->get_ammo(v->type).add(v->amount);
		
		if (delta > 1e-5)
		{
			v->amount -= delta;
			
			if (v->amount < 1e-5) destroy();
			else ren.parts(ren.model, ME_DEATH, {});
		}
	}
	else if (auto v = std::get_if<Func>(&val))
	{
		if (v->f(*ce.other))
			destroy();
	}
}



static std::shared_ptr<AI_Drone::State> idle_noop()
{
	static auto p = std::make_shared<AI_Drone::State>( AI_Drone::IdleNoop{} );
	return p;
}
static std::shared_ptr<AI_DroneParams> pars_turret()
{
	static std::shared_ptr<AI_DroneParams> pars;
	if (!pars) {
		pars = std::make_shared<AI_DroneParams>();
		pars->dist_suspect = pars->dist_visible = 20;
	}
	return pars;
}
static b2BodyDef ETurret_bd(vec2fp at)
{
	b2BodyDef bd;
	bd.position = conv(at);
	bd.fixedRotation = true;
	return bd;
}
ETurret::ETurret(vec2fp at, std::shared_ptr<AI_Group> grp, size_t team)
	:
    phy(this, ETurret_bd(at)),
    ren(this, MODEL_BOX_SMALL, FColor(1, 0, 1, 1)),
    hlc(this, 400),
    eqp(this),
    logic(this, pars_turret(), std::move(grp), idle_noop()),
    l_tar(&logic),
    team(team)
{
	b2FixtureDef fd;
	phy.add_circle(fd, GameConst::hsz_box_small, 1);
	
	eqp.add_wpn(new WpnMinigun);
	eqp.set_wpn(0);
}



EEnemyDrone::EEnemyDrone(vec2fp at, const Init& init)
	:
	phy(this, [](vec2fp at){
        b2BodyDef def;
		def.position = conv(at);
		def.type = b2_dynamicBody;
		return def;
	}(at)),
	ren(this, MODEL_DRONE, FColor(1, 0, 0, 1)),
	hlc(this, 70),
	eqp(this),
	logic(this, init.pars, init.grp, std::make_shared<AI_Drone::State>(AI_Drone::IdlePoint{at})),
	l_tar(&logic),
	mov(&logic)
{
	b2FixtureDef fd;
	fd.friction = 0.8;
	fd.restitution = 0.4;
	phy.add_circle(fd, GameConst::hsz_drone * 1.4, 25); // sqrt2 - diagonal
	
	hlc.add_filter(std::make_shared<DmgShield>(100, 20));
	hlc.ph_thr = 100;
	hlc.ph_k = 0.2;
	hlc.hook(phy);
	
	eqp.add_wpn(new WpnRocket);
	eqp.set_wpn(0);
}



EDoor::RenDoor::RenDoor(Entity* ent)
	: ECompRender(ent)
{}
void EDoor::RenDoor::step()
{
	auto door = static_cast<EDoor*>(ent);
	float t_ext;
	
	switch (door->state)
	{
	case ST_OPEN:
		t_ext = 0;
		break;
		
	case ST_TO_OPEN:
		t_ext = door->tm_left / door->anim_time;
		break;
		
	case ST_TO_CLOSE:
		t_ext = 1 - door->tm_left / door->anim_time;
		break;
		
	case ST_CLOSED:
		t_ext = 1;
		break;
	}
	
	//
	
	vec2fp p0 = get_pos().pos - door->fix_he;
	vec2fp p1 = get_pos().pos + door->fix_he;
	
	vec2fp o_len, o_wid; // offsets
	if (door->is_x_ext)
	{
		o_len.set( lerp(min_off, door->fix_he.x, t_ext), 0 );
		o_wid.set( 0, door->fix_he.y *2 );
	}
	else
	{
		o_len.set( 0, lerp(min_off, door->fix_he.y, t_ext) );
		o_wid.set( door->fix_he.x *2, 0 );
	}

	//
	
	uint32_t l_clr = FColor(0, 0.8, 0.8).to_px();
	if (door->plr_only) l_clr = FColor(0.3, 0.8, 0.3).to_px();
	float l_wid = 0.1f;
	float l_aa = 3.f;
	
	RenAAL::get().draw_line(p0,         p0 + o_len,         l_clr, l_wid, l_aa);
	RenAAL::get().draw_line(p0 + o_wid, p0 + o_len + o_wid, l_clr, l_wid, l_aa);
	RenAAL::get().draw_line(p0 + o_len, p0 + o_len + o_wid, l_clr, l_wid, l_aa);
	
	RenAAL::get().draw_line(p1,         p1 - o_len,         l_clr, l_wid, l_aa);
	RenAAL::get().draw_line(p1 - o_wid, p1 - o_len - o_wid, l_clr, l_wid, l_aa);
	RenAAL::get().draw_line(p1 - o_len, p1 - o_len - o_wid, l_clr, l_wid, l_aa);
}
void EDoor::on_cnt(const CollisionEvent& ce)
{
	if (!plr_only)
	{
		if (!ce.other->get_eqp()) return;
		if (ce.type == CollisionEvent::T_BEGIN) open();
	}
	else if (GameCore::get().get_pmg().is_player( ce.other ))
	{
		if      (ce.type == CollisionEvent::T_BEGIN) open();
		else if (ce.type == CollisionEvent::T_END)
		{
			if (state == ST_TO_OPEN) tm_left = anim_time - tm_left;
			else if (state == ST_OPEN) tm_left = anim_time;
			else return;

			state = ST_TO_CLOSE;
			upd_fix();
		}
	}
}
void EDoor::open()
{
	if      (state == ST_OPEN)   tm_left = keep_time;
	else if (state == ST_CLOSED) tm_left = wait_time;
}
void EDoor::step()
{
	if (state == ST_OPEN)
	{
		tm_left -= GameCore::step_len;
		if (tm_left.is_negative())
		{
			state = ST_TO_CLOSE;
			tm_left = anim_time;
			upd_fix();
		}
	}
	else if (state == ST_TO_OPEN)
	{
		tm_left -= GameCore::step_len;
		if (tm_left.is_negative())
		{
			state = ST_OPEN;
			tm_left = keep_time;
		}
	}
	else if (state == ST_TO_CLOSE)
	{
		tm_left -= GameCore::step_len;
		if (tm_left.is_negative())
		{
			state = ST_CLOSED;
			upd_fix();
		}
	}
	else if (state == ST_CLOSED)
	{
		if (tm_left.is_positive())
		{
			tm_left -= GameCore::step_len;
			if (tm_left.is_negative())
			{
				state = ST_TO_OPEN;
				tm_left = anim_time;
				upd_fix();
			}
		}
	}
}
void EDoor::upd_fix()
{
	if (state == ST_OPEN || state == ST_TO_OPEN)
	{
		if (!fix) return;
		GameCore::get().get_phy().post_step([this]
		{
			phy.destroy(fix);
			fix = nullptr;
		});
	}
	else if (!fix)
	{
		GameCore::get().get_phy().post_step([this]
		{
			b2FixtureDef fd;
			fd.friction = 0.15;
			fd.restitution = 0.1;
			fix = phy.add_box(fd, fix_he, 0, new FixtureInfo( FixtureInfo::TYPEFLAG_WALL | FixtureInfo::TYPEFLAG_OPAQUE ));
		});
	}
}
EDoor::EDoor(vec2i TL_origin, vec2i door_ext, vec2i room_dir, bool plr_only)
	:
    phy(this, [&]
	{
		this->is_x_ext = (door_ext.x != 0);
	
		b2BodyDef def;
		float cz = LevelControl::get().cell_size;
		vec2fp pos = vec2fp(TL_origin) * cz;
		
		if (is_x_ext)
		{
			pos.x += door_ext.x * cz /2;
			pos.y += cz * ((room_dir.y > 0) ? (1 - offset - width /2) : (offset + width /2));
		}
		else
		{
			pos.y += door_ext.y * cz /2;
			pos.x += cz * ((room_dir.x > 0) ? (1 - offset - width /2) : (offset + width /2));
		}
		
		def.position = conv(pos);
		return def;
	}()),
    ren(this),
    plr_only(plr_only)
{
	reg();
	EVS_CONNECT1(phy.ev_contact, on_cnt);
	
	float cz = LevelControl::get().cell_size;
	vec2fp sens_he;
	if (is_x_ext) sens_he.set( door_ext.x * cz /2, cz );
	else          sens_he.set( cz, door_ext.y * cz /2 );
	
	b2FixtureDef fd;
	fd.friction = 0.15;
	fd.restitution = 0.1;
	fd.isSensor = true;
	phy.add_box(fd, sens_he, 0, new FI_Sensor);
	
	if (is_x_ext) fix_he.set( door_ext.x * cz /2, width /2 );
	else          fix_he.set( width /2, door_ext.y * cz /2 );
	upd_fix();
}



EFinalTerminal::EFinalTerminal(vec2fp at)
	:
    phy(this, [&]{b2BodyDef d; d.position = conv(at); return d;}()),
    ren(this, MODEL_TERMINAL_FIN, FColor(1, 0.8, 0.4))
{
	b2FixtureDef fd;
	fd.restitution = 0;
	fd.friction = 0.5;
	phy.add_circle(fd, GameConst::hsz_termfin, 0, new FixtureInfo(FixtureInfo::TYPEFLAG_INTERACTIVE | FixtureInfo::TYPEFLAG_OPAQUE));
}
void EFinalTerminal::step()
{
	float t = (timer_end - GameCore::get().get_step_time()).seconds();
	GamePresenter::get()->dbg_text(get_pos(), FMT_FORMAT("Boot-up in process: {:2.1f}", t));
	
	if (timer_end < GameCore::get().get_step_time() || is_activated)
		unreg();
}
std::pair<bool, std::string> EFinalTerminal::use_string()
{
	if (!enabled)
		return {0, "Security tokens required"};
	if (!timer_end.is_positive())
		return {1, "Activate control terminal"};
	if (timer_end < GameCore::get().get_step_time()) 
		return {1, "Gain level control"};
	else
		return {0, "Boot-up in process"};
}
void EFinalTerminal::use(Entity*)
{
	if (!enabled) {
		GamePresenter::get()->add_float_text({ get_pos(), "Access denied" });
	}
	else if (!timer_end.is_positive())
	{
		timer_end = GameCore::get().get_step_time() + TimeSpan::seconds(5);
		GamePresenter::get()->add_float_text({ get_pos(), "Boot sequence initialized" });
		reg();
	}
	else if (timer_end < GameCore::get().get_step_time())
	{
		is_activated = true;
		GamePresenter::get()->add_float_text({ get_pos(), "Terminal active" });
	}
}



EDispenser::EDispenser(vec2fp at, float rot, bool increased_amount)
	:
    phy(this, [&]{b2BodyDef d; d.position = conv(at); d.angle = rot; return d;}()),
    ren(this, MODEL_DISPENSER, FColor(0.7, 0.8, 0.8)),
    increased(increased_amount)
{
	b2FixtureDef fd;
	fd.isSensor = true;
	phy.add_circle(fd, 0.5, 0, new FixtureInfo(FixtureInfo::TYPEFLAG_INTERACTIVE));
	
	gen_at = {LevelControl::get().cell_size /2, 0};
	gen_at.fastrotate(rot + M_PI);
	gen_at += at;
}
std::pair<bool, std::string> EDispenser::use_string()
{
	TimeSpan left = usable_after - GameCore::get().get_step_time();
	if (left.is_negative()) return {1, "Get supplies"};
	return {0, FMT_FORMAT("Not ready ({:.1f} seconds)", left.seconds())};
}
void EDispenser::use(Entity*)
{
	TimeSpan now = GameCore::get().get_step_time();
	if (usable_after >= now) return;
	
	size_t i = GameCore::get().get_random().range_index( size_t(AmmoType::TOTAL_COUNT), size_t(AmmoType::None) + 1 );
	auto ap = EPickable::std_ammo( AmmoType(i) );
	if (increased) ap.amount *= GameCore::get().get_random().range(1.5, 3);
	new EPickable(gen_at, ap);
	
	usable_after = now + TimeSpan::seconds(10);
}



void EMinidock::on_cnt(const CollisionEvent& ev)
{
	if (GameCore::get().get_pmg().is_player( ev.other ))
	{
		plr = ev.other; // contact will end before it's invalidated
		if (ev.type == CollisionEvent::T_BEGIN) reg();
		else {
			plr = nullptr;
			unreg();
		}
	}
}
void EMinidock::step()
{
	auto now = GameCore::get().get_step_time();
	if (usable_after > now) return;
	
	if (GameCore::get().get_phy().point_cast( conv(get_pos()), LevelControl::get().cell_size, {
		[&](auto ent, auto) {
			auto p = dynamic_cast<EMinidock*>(ent);
			return p && p != this && p->get_target() == get_target();
		}
		, {}, false
	}))
	{
		usable_after = now + TimeSpan::seconds(0.2);
		return;
	}
	
	//
	
	TimeSpan wait = TimeSpan::seconds(0.5);
	float hps = 0.15 * wait.seconds(); // health
	float sps = 0.2 * wait.seconds(); // shields
	float max_heal = 1.5;
	
	TimeSpan recharge = TimeSpan::seconds(20);
	float charge_inc = wait / recharge;
	float charge_dec = wait / TimeSpan::seconds(10);
	
	//
	
	bool heal_any = false;
	
	auto heal = [&](HealthPool& hp, float am)
	{
		auto [cur, max] = hp.exact();
		float t = float(cur) / max;
		if (t < max_heal) {
			hp.apply(am * max, false);
			heal_any = true;
		}
	};
	
	auto hc = plr->get_hlc();
	heal(hc->get_hp(), hps);
	
	for (auto& f : hc->raw_fils())
	{
		if (auto p = dynamic_cast<DmgShield*>(f.get()))
			heal(p->get_hp(), sps);
	}
	for (auto& f : hc->raw_prot())
	{
		if (auto p = dynamic_cast<DmgShield*>(f.get()))
			heal(p->get_hp(), sps);
	}
	
	//
	
	if (!heal_any) return;
	usable_after = now + wait;
	
	charge = std::min(1., charge + charge_inc * ((now - last_use) / wait));
	charge -= charge_dec + charge_inc;
	if (charge < 0) usable_after = now + recharge;
	
	//
	
	struct Ren // lightning from bolter with another color
	{
		vec2fp a, b;
		float t, tps;
		
		Ren(vec2fp a, vec2fp b, TimeSpan lt) : a(a), b(b), t(1), tps(1.f / lt.seconds()) {}
		bool operator()(TimeSpan passed) {
			RenAAL::get().draw_line(a, b, FColor(0.5, 0.9, 0.6).to_px(), 0.1f, 5.f, t * 2.5);
			t -= tps * passed.seconds();
			return t > 0;
		}
	};
	GamePresenter::get()->add_effect(Ren( get_pos(), plr->get_pos(), wait ));
}
EMinidock::EMinidock(vec2fp at, float rot)
    :
    phy(this, [&]{b2BodyDef d; d.position = conv(at); d.angle = rot; return d;}()),
    ren(this, MODEL_MINIDOCK, FColor(0.5, 0.7, 0.9))
{
	b2FixtureDef fd;
	fd.isSensor = true;
	phy.add_box(fd, vec2fp::one(2.5), 0, Transform{{-LevelControl::get().cell_size /2, 0}});
	
	EVS_CONNECT1(phy.ev_contact, on_cnt);
}



EDecor::EDecor(const char *ui_name, Rect at, float rot, ModelType model, FColor clr)
	:
	phy(this, [&]{
		b2BodyDef d;
		d.position = conv( at.fp_center() * LevelControl::get().cell_size );
		d.angle = rot;
		return d;
	}()),
    ren(this, model, clr),
    ui_name(ui_name)
{
	b2FixtureDef fd;
	fd.friction = 0.15;
	fd.restitution = 0.1;
	phy.add_box(fd, vec2fp(at.size()) * LevelControl::get().cell_size /2, 0, new FixtureInfo(FixtureInfo::TYPEFLAG_OPAQUE));
}
EDecorGhost::EDecorGhost(Transform at, ModelType model, FColor clr)
    :
	phy(this, at),
    ren(this, model, clr)
{}
