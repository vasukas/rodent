#include "client/effects.hpp"
#include "client/presenter.hpp"
#include "game/level_ctr.hpp"
#include "game/game_core.hpp"
#include "game/game_info_list.hpp"
#include "game/game_mode.hpp"
#include "game/player_mgr.hpp"
#include "utils/noise.hpp"
#include "vaslib/vas_log.hpp"
#include "objs_basic.hpp"
#include "weapon_all.hpp"



EWall::EWall(GameCore& core, const std::vector<std::vector<vec2fp>>& walls)
    : Entity(core), phy(*this, b2BodyDef{})
{
	add_new<EC_RenderModel>(MODEL_LEVEL_STATIC, FColor(0.75, 0.75, 0.75, 1));
	ensure<EC_RenderPos>().disable_culling = true;
	
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
		auto f = phy.body.CreateFixture(&fd);
		
		set_info(*f, FixtureInfo{ FixtureInfo::TYPEFLAG_WALL | FixtureInfo::TYPEFLAG_OPAQUE });
		if (auto p = SoundEngine::get()) p->geom_static_add(shp);
	};
	for (auto& w : walls) addfix(w);
	
	auto& lc = core.get_lc();
	p0 = {};
	p1 = lc.get_size() * GameConst::cell_size;
	p0 += vec2fp::one( GameConst::cell_size/2 );
	p1 -= vec2fp::one( GameConst::cell_size/2 );
	
	std::vector<vec2fp> enc = {
	    {p0.x, p0.y},
	    {p1.x, p0.y},
	    {p1.x, p1.y},
	    {p0.x, p1.y},
	    {p0.x, p0.y},
	};
	addfix(enc);
}



EPhyBox::EPhyBox(GameCore& core, vec2fp at)
    :
	Entity(core),
    phy(*this, bodydef(at, true)),
    hlc(*this, 250)
{
	ui_descr = "Box";
	add_new<EC_RenderModel>(MODEL_BOX_SMALL, FColor(1, 0.6, 0.2, 1));
	phy.add(FixtureCreate::box(
		fixtdef(0.1, 0.5), vec2fp::one(GameConst::hsz_box_small),
		40.f, FixtureInfo{ FixtureInfo::TYPEFLAG_OPAQUE }));
	
//	hlc.hook(phy);
}




EPickable::AmmoPack EPickable::rnd_ammo(GameCore& core)
{
	static const auto cs = normalize_chances<AmmoType, 4>({{
		{AmmoType::Bullet,   1.3},
		{AmmoType::Rocket,   1.0},
		{AmmoType::Energy,   0.8},
		{AmmoType::FoamCell, 0.4}
	}});
	return std_ammo(core.get_random().random_chance(cs));
}
EPickable::AmmoPack EPickable::std_ammo(AmmoType type)
{
	AmmoPack ap = {type, 0};
	switch (type)
	{
	case AmmoType::Bullet:   ap.amount = 50; break;
	case AmmoType::Rocket:   ap.amount = 6;  break;
	case AmmoType::Energy:   ap.amount = 18; break;
	case AmmoType::FoamCell: ap.amount = 20; break;

	case AmmoType::None:
	case AmmoType::TOTAL_COUNT:
		break;
	}
	return ap;
}
void EPickable::create_death_drop(GameCore& core, vec2fp pos, float value)
{
	auto& rnd = core.get_random();
	if (value < rnd.range_n()) return;
	
	float m0 = clampf_n( (value - 0.5) / 0.5 );
	float m1 = std::max(value - 1, 0.f);
	
	EPickable::Value v;
	if (rnd.range_n() < 0.5)
	{
		auto ap = EPickable::rnd_ammo(core);
		ap.amount *= rnd.range(
			lerp(0.2, 0.3, m0),
			lerp(0.7, 2,   m1)
		);
		v = ap;
	}
	else {
		v = EPickable::ArmorShard{rnd.int_range(
			lerp(5, 20,  m0),
			lerp(20, 40, m1))};
	}
	
	new EPickable(core, pos, std::move(v));
}



EPickable::EPickable(GameCore& core, vec2fp pos, Value val)
	:
	Entity(core),
	phy(*this, bodydef(pos, false)),
	val(std::move(val))
{
	phy.add(FixtureCreate::circle( fixtsensor(), GameConst::hsz_supply + 0.2, 0 ));
	EVS_CONNECT1(phy.ev_contact, on_cnt);
	
	auto& ren = add_new<EC_RenderModel>();
	std::visit(overloaded
	{
		[&](const AmmoPack& v)
		{
			ren.model = ammo_model(v.type);
			ren.clr = FColor(1, 1, 1);
			ui_descr = ammo_name(v.type);
		},
		[&](const ArmorShard&)
		{
			ren.model = MODEL_ARMOR;
			ren.clr = FColor(0.3, 1, 0.2);
			ui_descr = "Armor shard";
		},
		[&](const SecurityKey&)
		{
			ren.model = MODEL_TERMINAL_KEY;
			ren.clr = FColor(1, 0.8, 0.4);
			ui_descr = "Security token";
		}
	}, val);
}
void EPickable::on_cnt(const CollisionEvent& ce)
{
	if (ce.type != CollisionEvent::T_BEGIN) return;
	if (!core.get_pmg().is_player( *ce.other )) return;
	
	bool del = std::visit(overloaded
	{
		[&](AmmoPack& v)
		{
			if (v.amount <= 0) { // rare bug
				SoundEngine::once(SND_UI_PICKUP, {});
				return true;
			}

			auto& eqp = ce.other->ref_eqp();
			int delta = eqp.get_ammo(v.type).add(v.amount);
			
			if (delta > 0)
			{
				SoundEngine::once(SND_UI_PICKUP, {});
				v.amount -= delta;
				if (v.amount <= 0) return true;
				else this->ref<EC_RenderModel>().parts(ME_DEATH, {});
			}
			return false;
		},
		[&](ArmorShard& v)
		{
			ce.other->ref_hlc().foreach_filter([&](DamageFilter& f) {
				if (auto p = dynamic_cast<DmgArmor*>(&f)) {
					int delta = p->get_hp().apply(v.amount);
					v.amount -= delta;
					if (delta) SoundEngine::once(SND_UI_PICKUP, {});
				}
			});
			return !v.amount;
		},
		[&](SecurityKey&)
		{
			SoundEngine::once(SND_UI_PICKUP_POWER, {});
			dynamic_cast<GameMode_Normal&>(core.get_gmc()).inc_objective();
			return true;
		}
	}, val);
	
	if (del)
		destroy();
}



void EDoor::on_cnt(const CollisionEvent& ce)
{
	if (!plr_only) {if (!ce.other->is_creature()) return;}
	else if (!core.get_pmg().is_player( *ce.other )) return;

	if (ce.type == CollisionEvent::T_BEGIN) {
		++num_cnt;
		open();
	}
	else if (ce.type == CollisionEvent::T_END) --num_cnt;
}
void EDoor::open()
{
	if (state == ST_OPEN) tm_left = keep_time;
	else if (state == ST_CLOSED && !tm_left.is_positive())
		tm_left = wait_time;
	
	reg_this();
}
void EDoor::step()
{
	if (state == ST_OPEN)
	{
		ref<EC_RenderDoor>().set_state(0);
		
		tm_left -= GameCore::step_len;
		if (tm_left.is_negative())
		{
			state = ST_TO_CLOSE;
			tm_left = anim_time;
			fix.set_enabled(*this, true);
			SoundEngine::once(SND_OBJ_DOOR_CLOSE, get_pos());
		}
	}
	else if (state == ST_TO_OPEN)
	{
		ref<EC_RenderDoor>().set_state( tm_left / anim_time );
		
		tm_left -= GameCore::step_len;
		if (tm_left.is_negative())
		{
			state = ST_OPEN;
			tm_left = plr_only ? keep_time_plr : keep_time;
		}
	}
	else if (state == ST_TO_CLOSE)
	{
		ref<EC_RenderDoor>().set_state( 1 - tm_left / anim_time );
		
		tm_left -= GameCore::step_len;
		if (tm_left.is_negative())
		{
			state = ST_CLOSED;
			fix.set_enabled(*this, true);
			
			if (num_cnt) tm_left = wait_time;
			else unreg_this();
		}
	}
	else if (state == ST_CLOSED)
	{
		ref<EC_RenderDoor>().set_state(1);
		
		tm_left -= GameCore::step_len;
		if (tm_left.is_negative())
		{
			state = ST_TO_OPEN;
			tm_left = anim_time;
			fix.set_enabled(*this, false);
			SoundEngine::once(SND_OBJ_DOOR_OPEN, get_pos());
		}
	}
}
EDoor::EDoor(GameCore& core, vec2i TL_origin, vec2i door_ext, vec2i room_dir, bool plr_only)
: EDoor(core, [&]
{
	Init init;
	init.plr_only = plr_only;
	init.is_x_ext = (door_ext.x != 0);

	float cz = GameConst::cell_size;
	vec2fp pos = vec2fp(TL_origin) * cz;
	
	if (init.is_x_ext)
	{
		pos.x += door_ext.x * cz /2;
		pos.y += cz * ((room_dir.y > 0) ? (1 - offset - width /2) : (offset + width /2));
	}
	else
	{
		pos.y += door_ext.y * cz /2;
		pos.x += cz * ((room_dir.x > 0) ? (1 - offset - width /2) : (offset + width /2));
	}
	
	if (init.is_x_ext) init.fix_he.set( door_ext.x * cz /2, width /2 );
	else               init.fix_he.set( width /2, door_ext.y * cz /2 );
	
	if (init.is_x_ext) init.sens_he.set( door_ext.x * cz /2, cz * sens_width );
	else               init.sens_he.set( cz * sens_width, door_ext.y * cz /2 );
	
	init.def.position = conv(pos);
	return init;
}())
{}
EDoor::EDoor(GameCore& core, Init init)
    :
	Entity(core),
    phy(*this, init.def),
	fix(FixtureCreate::box( fixtdef(0.15, 0.1), init.fix_he, 0,
                            FixtureInfo{ FixtureInfo::TYPEFLAG_WALL | FixtureInfo::TYPEFLAG_OPAQUE } )),
    plr_only(init.plr_only)
{
	phy.add(FixtureCreate::box( fixtsensor(), init.sens_he, 0 ));
	EVS_CONNECT1(phy.ev_contact, on_cnt);
	
	fix.set_enabled(*this, true);
	
	//
	
	ui_descr = "Door";
	add_new<EC_RenderDoor>(init.fix_he, init.is_x_ext, plr_only ? FColor(0.3, 0.8, 0.3) : FColor(0, 0.8, 0.8))
	.set_state(1);
}



EFinalTerminal::EFinalTerminal(GameCore& core, vec2fp at)
	:
	EInteractive(core),
    phy(*this, bodydef(at, false))
{
	phy.add(FixtureCreate::circle( fixtdef(0.5, 0), GameConst::hsz_termfin, 0,
	                               FixtureInfo{FixtureInfo::TYPEFLAG_INTERACTIVE | FixtureInfo::TYPEFLAG_OPAQUE}));
	add_new<EC_RenderModel>(MODEL_TERMINAL_FIN, FColor(1, 0.8, 0.4));
	ui_descr = "Control terminal";
}
void EFinalTerminal::step()
{
	auto& gmc = dynamic_cast<GameMode_Normal&>(core.get_gmc());
	if (gmc.get_state() != GameMode_Normal::State::Booting) {
		unreg_this();
		snd.stop();
	}
	else {
		float t = gmc.get_boot_left().seconds();
		GamePresenter::get()->dbg_text(get_pos(), FMT_FORMAT("Boot-up in progress: {:2.1f}", t));
	}
}
std::pair<bool, std::string> EFinalTerminal::use_string()
{
	auto state = dynamic_cast<GameMode_Normal&>(core.get_gmc()).get_state();
	if		(state == GameMode_Normal::State::NoTokens)  return {0, "Security tokens required"};
	else if (state == GameMode_Normal::State::HasTokens) return {1, "Activate control terminal"};
	else if (state == GameMode_Normal::State::Booting)   return {0, "Boot-up in progress"};
	else if (state == GameMode_Normal::State::Cleanup)   return {0, "Hostile elements detected"};
	else if (state == GameMode_Normal::State::Final)     return {1, "Gain level control"};
	return {};
}
void EFinalTerminal::use(Entity* by)
{
	if (!by || !core.get_pmg().is_player(*by))
		return;
	
	auto& gmc = dynamic_cast<GameMode_Normal&>(core.get_gmc());
	auto state = gmc.get_state();
	
	if		(state == GameMode_Normal::State::NoTokens) {
		GamePresenter::get()->add_float_text({ get_pos(), "Access denied" });
		SoundEngine::once(SND_OBJ_TERMINAL_FAIL, get_pos());
	}
	else if (state == GameMode_Normal::State::HasTokens) {
		GamePresenter::get()->add_float_text({ get_pos(), "Boot sequence initialized" });
		reg_this();
		gmc.terminal_use();
		SoundEngine::once(SND_OBJ_TERMINAL_OK, get_pos());
		snd.update({SND_OBJAMB_FINALTERM_WORK, get_pos()});
	}
	else if (state == GameMode_Normal::State::Final) {
		GamePresenter::get()->add_float_text({ get_pos(), "Terminal active" });
		gmc.terminal_use();
	}
	else {
		SoundEngine::once(SND_OBJ_TERMINAL_FAIL, get_pos());
	}
}



EDispenser::EDispenser(GameCore& core, vec2fp at, float rot, bool increased_amount)
	:
	EInteractive(core),
	phy(*this, bodydef(at, false, rot)),
    increased(increased_amount)
{
	ui_descr = "Dispenser";
	add_new<EC_RenderModel>(MODEL_DISPENSER, FColor(0.7, 0.9, 0.7));
	phy.add(FixtureCreate::circle( fixtsensor(), 0.5, 0, FixtureInfo{FixtureInfo::TYPEFLAG_INTERACTIVE} ));
	
	left = core.get_random().int_range(
		!increased ? 0 : 3,
		!increased ? 4 : 8 );
	
	gen_at = {GameConst::cell_size /2, 0};
	gen_at.fastrotate(rot + M_PI);
	gen_at += at;
}
std::pair<bool, std::string> EDispenser::use_string()
{
	if (!left) return {0, "Empty"};
	TimeSpan left = usable_after - core.get_step_time();
	if (left.is_negative()) return {1, "Get supplies"};
	return {0, FMT_FORMAT("Not ready ({:.1f} seconds)", left.seconds())};
}
void EDispenser::use(Entity*)
{
	if (!left) {
		SoundEngine::once(SND_OBJ_DISPENSER_EMPTY, get_pos());
		return;
	}
	TimeSpan now = core.get_step_time();
	if (usable_after >= now) {
		SoundEngine::once(SND_OBJ_DISPENSER_WAIT, get_pos());
		return;
	}
	
	auto ap = EPickable::rnd_ammo(core);
	if (increased) ap.amount *= core.get_random().range(1.5, 3);
	new EPickable(core, gen_at, ap);
	SoundEngine::once(SND_OBJ_DISPENSER, get_pos());
	
	usable_after = now + TimeSpan::seconds(10);
	--left;
}



ETeleport::ETeleport(GameCore& core, vec2fp at)
	:
	EInteractive(core),
	phy(*this, bodydef(at, false))
{
	ui_descr = "Teleporter";
	add_new<EC_RenderModel>(MODEL_TELEPAD, FColor(0.3, 0.3, 0.3));
	phy.add(FixtureCreate::circle( fixtsensor(), 0.5, 0, FixtureInfo{FixtureInfo::TYPEFLAG_INTERACTIVE} ));
	
	EVS_CONNECT1(phy.ev_contact, on_cnt);
	core.get_info().get_teleport_list().emplace_back(*this);
}
std::pair<bool, std::string> ETeleport::use_string()
{
	return {true, "Teleport"};
}
void ETeleport::use(Entity* by)
{
	if (by && core.get_pmg().is_player(*by))
		activate(true);
}
void ETeleport::on_cnt(const CollisionEvent& ev)
{
	if (ev.type == CollisionEvent::T_BEGIN && core.get_pmg().is_player(*ev.other))
		activate(false);
}
void ETeleport::activate(bool menu)
{
	size_t i;
	if (menu)
	{
		i = core.get_info().find_teleport(*this);
		core.get_info().set_menu_teleport(i);
		
		if (auto p = dynamic_cast<GameMode_Normal*>(&core.get_gmc()))
			p->on_teleport_activation();
	}
	else if (!activated)
		i = core.get_info().find_teleport(*this);
	
	if (!activated)
	{
		activated = true;
		core.get_info().get_teleport_list()[i].discovered = true;
		
		ref<EC_RenderModel>().clr = FColor(0.3, 0.6, 0.6);
		ref<EC_RenderModel>().parts(ME_AURA, {{}, 1.f, FColor(0.25, 0.4, 0.4, 0.3)});
		SoundEngine::once(SND_OBJ_TELEPORT_ACTIVATE, get_pos());
	}
}
void ETeleport::teleport_player()
{
	auto& plr = core.get_pmg().ref_ent();
	GamePresenter::get()->effect(FE_SPAWN, {Transform{plr.get_pos()}, plr.ref_pc().get_radius()});
	plr.ref_phobj().teleport(get_pos());
	GamePresenter::get()->effect(FE_SPAWN, {Transform{get_pos()}, plr.ref_pc().get_radius()});
	SoundEngine::once(SND_OBJ_TELEPORT, {});
}



void EMinidock::on_cnt(const CollisionEvent& ev)
{
	if (core.get_pmg().is_player( *ev.other ))
	{
		plr = ev.other; // contact will end before it's invalidated
		if (ev.type == CollisionEvent::T_BEGIN) reg_this();
		else {
			plr = nullptr;
			unreg_this();
		}
	}
}
void EMinidock::step()
{
	TimeSpan wait = TimeSpan::seconds(0.5);
	float hps = 0.15 * wait.seconds(); // health
	float sps = 0.2 * wait.seconds(); // shields
	float max_heal = 1.5;
	
	TimeSpan recharge = TimeSpan::seconds(20);
	float charge_inc = wait / recharge;
	float charge_dec = wait / TimeSpan::seconds(8);
	
	TimeSpan dmg_tmo = TimeSpan::seconds(4); // heal only if not damaged for that long
	
	//
	
	auto& hc = plr->ref_hlc();
	
	auto now = core.get_step_time();
	if (usable_after > now) return;
	
	if (hc.since_damaged() < dmg_tmo)
	{
		usable_after = now + TimeSpan::seconds(0.2);
		return;
	}
	
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
	
	heal(hc.get_hp(), hps);
	hc.foreach_filter([&](auto& f)
	{
		if (auto p = dynamic_cast<DmgShield*>(&f))
			heal(p->get_hp(), sps);
	});
	
	//
	
	if (!heal_any) return;
	TimeSpan last_use = usable_after - wait;
	usable_after = now + wait;
	
	charge = std::min(1., charge + charge_inc * ((now - last_use) / wait));
	charge -= charge_dec + charge_inc;
	if (charge < 0) {
		charge = 1;
		usable_after = now + recharge;
	}
	
	//
	
	effect_lightning( get_pos(), plr->get_pos(), EffectLightning::Regular, wait, FColor(0.5, 0.9, 0.6));
	SoundEngine::once(SND_OBJ_MINIDOCK, get_pos());
}
EMinidock::EMinidock(GameCore& core, vec2fp at, float rot)
    :
	Entity(core),
    phy(*this, bodydef(at, false, rot))
{
	ui_descr = "Minidoc";
	add_new<EC_RenderModel>(MODEL_MINIDOCK, FColor(0.5, 0.7, 0.9));
	phy.add(FixtureCreate::box( fixtsensor(), vec2fp::one(2.5), Transform{{-GameConst::cell_size /2, 0}}, 0 ));
	EVS_CONNECT1(phy.ev_contact, on_cnt);
}



EStorageBox::EStorageBox(GameCore& core, vec2fp at)
	:
	Entity(core),
    phy(*this, bodydef(at, false)),
    hlc(*this, 500)
{
	ui_descr = "Autobox";
	add_new<EC_RenderModel>(MODEL_STORAGE, FColor(0.7, 0.9, 0.7), EC_RenderModel::DEATH_AND_EXPLOSION);
	phy.add(FixtureCreate::box( fixtdef(0.15, 0.1), ResBase::get().get_size(MODEL_STORAGE).size() /2, 0,
	                            FixtureInfo{FixtureInfo::TYPEFLAG_OPAQUE | FixtureInfo::TYPEFLAG_WALL} ));
	
	hlc.add_filter(std::make_unique<DmgShield>(100, 20, TimeSpan::seconds(15)));
}
EStorageBox::~EStorageBox()
{
	if (!core.is_freeing())
	{
		for (int i=0; i<5; ++i) {
			float rot = core.get_random().range_n2() * M_PI;
			float len = core.get_random().range(0, GameConst::cell_size /2);
			EPickable::create_death_drop(core, get_pos() + vec2fp(len, 0).rotate(rot), 0.8);
		}
		
		auto& lc = core.get_lc();
		lc.mut_cell(lc.to_cell_coord( get_pos() )).is_wall = false;
		
		GamePresenter::get()->effect(FE_WPN_EXPLOSION, {Transform{get_pos()}, 3.f});
		SoundEngine::once(SND_ENV_LARGE_EXPLOSION, get_pos());
	}
}



EMiningDrill::EMiningDrill(GameCore& core, vec2fp at, float rot)
    :
	EInteractive(core),
	phy(*this, bodydef(at, false, rot)),
	eqp(*this)
{
	ui_descr = "Drilling rig";
	add_new<EC_RenderModel>(MODEL_MINEDRILL_MINI, FColor(0.7, 0.7, 0.7));
	
	flags_t ff = FixtureInfo::TYPEFLAG_OPAQUE | FixtureInfo::TYPEFLAG_WALL | FixtureInfo::TYPEFLAG_INTERACTIVE;
	phy.add(FixtureCreate::box( fixtdef(0.15, 0.1), ResBase::get().get_size(MODEL_MINEDRILL_MINI).size() /2, 0,
	                            FixtureInfo{ff} ));
	
	eqp.no_overheat = true;
	eqp.hand.reset();
	eqp.add_wpn(std::make_unique<WpnUber>());
}
void EMiningDrill::use(Entity*)
{
	if (!stage) {
		stage = 1;
		left = TimeSpan::seconds(3);
		reg_this();
	}
	else if (stage == 3) {
		ref<EC_RenderPos>().parts({ FE_WPN_CHARGE }, {Transform{vec2fp(phy.get_radius(), 0)}, 3.f, FColor(1, 0.3, 0)});
		SoundEngine::once(SND_OBJ_DRILL_FIZZLE, get_pos());
	}
}
void EMiningDrill::step()
{
	if		(stage == 1) {
		ref<EC_RenderPos>().parts({ FE_WPN_CHARGE }, {Transform{vec2fp(phy.get_radius(), 0)}, 0.2f, FColor(1, 0.1, 0)});
	}
	else if (stage == 2) {
		vec2fp off = phy.get_norm_dir() * phy.get_radius() * 2;
		eqp.shoot(phy.get_pos() + off, true, false);
	}
	
	left -= core.step_len;
	if (left.is_negative()) {
		if (stage == 1) {
			stage = 2;
			left = TimeSpan::seconds(core.get_random().range(4, 8));
		}
		else {
			stage = 3;
			unreg_this();
		}
	}
}



EDecor::EDecor(GameCore& core, const char *ui_name, Rect at, float rot, ModelType model, FColor clr)
	:
	Entity(core),
	phy(*this, [&]{
		b2BodyDef d;
		vec2fp ctr = ResBase::get().get_size(model).center();
		ctr.rotate(rot);
		d.position = conv( ctr + at.fp_center() * GameConst::cell_size );
		d.angle = rot;
		return d;
	}())
{
	ui_descr = ui_name;
	add_new<EC_RenderModel>(model, clr);
	phy.add(FixtureCreate::box( fixtdef(0.15, 0.1), ResBase::get().get_size(model).size() /2, 0,
	                            FixtureInfo{FixtureInfo::TYPEFLAG_OPAQUE | FixtureInfo::TYPEFLAG_WALL} ));
}
EDecorDestructible::EDecorDestructible(GameCore& core, const char *ui_name, int hp_amount,
                                       Rect at, float rot, ModelType model, FColor clr)
    :
	EDecor(core, ui_name, at, rot, model, clr),
	hlc(*this, hp_amount)
{}



EAssembler::EAssembler(GameCore& core, vec2i at, float rot)
    : EDecorDestructible(core, "Assembler", 240, Rect{at, {1,1}, true}, rot, MODEL_ASSEMBLER, FColor(0.8, 0.6, 0.6))
{
	hlc.add_filter(std::make_unique<DmgShield>(60, 10, TimeSpan::seconds(15)));
	
	vec2fp prod_pos = core.get_lc().to_center_coord(at) + vec2fp(GameConst::cell_size, 0).rotate(rot + M_PI); // wtf?
	core.get_info().get_assembler_list().push_back({ index, prod_pos });
	
	ensure<EC_ParticleEmitter>().effect({MODEL_ASSEMBLER, ME_AURA}, {{}, 0.3, FColor(0.9, 0.7, 1, 0.05)},
	                                    TimeSpan::seconds(rnd_stat().range(0.5, 0.6)), TimeSpan::nearinfinity);
}
EAssembler::~EAssembler()
{
	if (!core.is_freeing()) {
		auto& list = core.get_info().get_assembler_list();
		erase_if(list, [&](auto& v) {return v.eid == index;});
		
		bool msg = true;
		for (auto& p : list) {
			if (core.get_lc().get_room(p.prod_pos) == core.get_lc().get_room(get_pos())) {
				msg = false;
				break;
			}
		}
		if (msg) dynamic_cast<GameMode_Normal&>(core.get_gmc()).factory_down(list.empty());
		
		ref<EC_RenderPos>().parts(FE_CIRCLE_AURA,   {{}, 3.f, FColor(0.8, 1, 1, 1.5)});
		ref<EC_RenderPos>().parts(FE_WPN_EXPLOSION, {{}, 2.f});
		SoundEngine::once(SND_ENV_LARGE_EXPLOSION, get_pos());
	}
}



EDecorGhost::EDecorGhost(GameCore& core, Transform at, ModelType model, FColor clr)
    :
	Entity(core),
	phy(*this, at)
{
	add_new<EC_RenderModel>(model, clr);
}



ETutorialMsg::ETutorialMsg(GameCore& core, vec2fp pos, std::string msg)
    : Entity(core), phy(*this, Transform{pos})
{
	add_new<EC_RenderFadeText>(std::move(msg));
}



ETutorialDummy::ETutorialDummy(GameCore& core, vec2fp pos)
    :
	Entity(core),
	phy(*this, bodydef(pos, false, core.get_random().range_n2() * M_PI)),
	hlc(*this, 350)
{
	ui_descr = "Dummy";
	phy.add(FixtureCreate::circle(fixtdef(0.5, 0.2), GameConst::hsz_box_small, 0));
	add_new<EC_RenderModel>(MODEL_WORKER, FColor(1, 0.5, 0, 1));
	EVS_CONNECT1(hlc.on_damage, on_dmg);
}
void ETutorialDummy::on_dmg(const DamageQuant& q)
{
	FloatText text;
	text.at = q.wpos.value_or(get_pos());
	text.str = std::to_string(q.amount);
	text.color = hlc.get_hp().is_alive() ? 0x40ff'ffff : 0xff40'40ff;
	text.size = 2;
	text.spread_strength = 5;
	GamePresenter::get()->add_float_text(std::move(text));
}



ERespawnFunc::ERespawnFunc(GameCore& core, vec2fp pos,
                           std::function<Entity*()> f_in, TimeSpan initial_delay)
    : Entity(core), phy(*this, Transform{pos}), f(std::move(f_in))
{
	tmo = initial_delay;
	if (!tmo.is_positive()) {
		child = f()->index;
	}
	reg_this();
}
void ERespawnFunc::step()
{
	if (!child) {
		tmo -= core.step_len;
		if (tmo.is_negative()) {
			child = f()->index;
			GamePresenter::get()->effect(FE_SPAWN, {Transform{get_pos()}});
			SoundEngine::once(SND_OBJ_SPAWN, get_pos());
		}
	}
	else if (!core.valid_ent(child)) {
		tmo = period;
	}
}
