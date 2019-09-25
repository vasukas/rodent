#include "utils/noise.hpp"
#include "game_core.hpp"
#include "s_objs.hpp"


EWall::EWall(const std::vector<std::vector<vec2fp>>& walls)
    :
    phy(this, b2BodyDef{}),
    ren(this, MODEL_LEVEL_STATIC, FColor(0.75, 0.75, 0.75, 1))
{
	std::vector<b2Vec2> verts;
	vec2fp p0 = vec2fp::one(std::numeric_limits<float>::max());
	vec2fp p1 = vec2fp::one(std::numeric_limits<float>::min());
	
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
	
	p0 -= vec2fp::one(10);
	p1 += vec2fp::one(10);
	
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
    hlc(this, 200)
{
	b2FixtureDef fd;
	fd.friction = 0.1;
	fd.restitution = 0.5;
	phy.add_box(fd, vec2fp::one(GameConst::hsz_box_small), 8.f, new FixtureInfo( FixtureInfo::TYPEFLAG_OPAQUE ));
	
	hlc.hook(phy);
}



ESupply::ESupply(vec2fp pos, AmmoPack ap)
	:
    phy(this, [&]{b2BodyDef d; d.position = conv(pos); return d;}()),
    ren(this, ammo_model(ap.type)),
    val(ap)
{
	b2FixtureDef fd;
	fd.isSensor = true;
	phy.add_circle(fd, GameConst::hsz_supply, 0);
	EVS_CONNECT1(phy.ev_contact, on_cnt);
}
void ESupply::on_cnt(const CollisionEvent& ce)
{
	if (ce.type != CollisionEvent::T_BEGIN) return;
	
	if (std::holds_alternative<AmmoPack>(val))
	{
		if (ce.other->get_team() != TEAM_PLAYER)
			return;
		
		auto eqp = ce.other->get_eqp();
		if (!eqp) return;
		
		for (auto& w : eqp->wpns)
		{
			auto ammo = w->get_ammo();
			if (!ammo) continue;
			
			if (ammo->type != std::get<AmmoPack>(val).type)
				continue;
			
			if (!ammo->add(std::get<AmmoPack>(val).amount))
				continue;
			
			destroy();
			break;
		}
	}
}


#define TURRET_RADIUS (20.f)

static b2BodyDef ETurret_bd(vec2fp at)
{
	b2BodyDef bd;
	bd.position = conv(at);
	bd.fixedRotation = true;
	return bd;
}
ETurret::ETurret(vec2fp at, size_t team)
	:
    phy(this, ETurret_bd(at)),
    ren(this, MODEL_BOX_SMALL, FColor(1, 0, 1, 1)),
    hlc(this, 400),
    eqp(this),
    logic(this, std::make_unique<AI_TargetSensor>(phy, 20), nullptr),
    team(team)
{
	b2FixtureDef fd;
	phy.add_circle(fd, GameConst::hsz_box_small, 1);
	
	eqp.infinite_ammo = true;
	eqp.hand = 0;
	
	eqp.wpns.emplace_back( Weapon::create_std(WeaponIndex::Minigun) );
	eqp.set_wpn(0);
}


static std::unique_ptr<AI_TargetProvider> drone_prov(Entity* ent)
{
	static std::shared_ptr<AI_TargetNetwork::Group> all = std::make_shared<AI_TargetNetwork::Group>();
	return std::make_unique<AI_TargetNetwork>(ent, all, std::make_unique<AI_TargetPlayer>(ent, 25));
}

EEnemyDrone::EEnemyDrone(vec2fp at)
	:
	phy(this, [](vec2fp at){
        b2BodyDef def;
		def.position = conv(at);
		def.type = b2_dynamicBody;
		def.fixedRotation = true;
		return def;
	}(at)),
	ren(this, MODEL_DRONE, FColor(1, 0, 0, 1)),
	hlc(this, 70),
	eqp(this),
	mov(this, 4, 7, 9),
	logic(this, drone_prov(this), &mov)
{
	b2FixtureDef fd;
	fd.friction = 0.8;
	fd.restitution = 0.4;
	phy.add_box(fd, vec2fp::one(GameConst::hsz_drone), 25);
	
	hlc.add_filter(std::make_shared<DmgShield>(100, 20));
	hlc.ph_thr = 100;
	hlc.ph_k = 0.2;
	hlc.hook(phy);
	
	eqp.infinite_ammo = true;
	eqp.hand = 0;
	
	eqp.wpns.emplace_back( Weapon::create_std(WeaponIndex::Rocket) );
	eqp.set_wpn(0);
	
	logic.min_dist = 8;
	logic.opt_dist = 14;
}
