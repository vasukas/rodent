#include "utils/noise.hpp"
#include "game_core.hpp"
#include "s_objs.hpp"


EWall::EWall(const std::vector<std::vector<vec2fp>>& walls)
    :
    phy(this, b2BodyDef{}),
    ren(this, MODEL_LEVEL_STATIC, FColor(0.75, 0.75, 0.75, 1))
{
	std::vector<b2Vec2> verts;
	for (auto& w : walls)
	{
		verts.clear();
		verts.reserve(w.size());
		for (auto& p : w) verts.push_back(conv(p));
		
		bool loop = w.front().equals( w.back(), 1e-5 );
		
		b2ChainShape shp;
		if (loop) shp.CreateLoop(verts.data(), verts.size() - 1);
		else shp.CreateChain(verts.data(), verts.size());
		
		b2FixtureDef fd;
		fd.friction = 0.15;
		fd.restitution = 0.1;
		fd.shape = &shp;
		phy.body->CreateFixture(&fd);
	}
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
	phy.add_box(fd, vec2fp::one(GameConst::hsz_box_small), 8.f);
	
	hlc.hook(phy);
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
    team(team),
    logic(this)
{
	b2FixtureDef fd;
	phy.add_circle(fd, GameConst::hsz_box_small, 1);
	
	eqp.infinite_ammo = true;
	eqp.hand = 0;
	
	eqp.wpns.emplace_back( Weapon::create_std(WeaponIndex::Minigun) );
	eqp.set_wpn(0);
}
ETurret::Logic::Logic(Entity* e)
    : EComp(e)
{
	reg(ECompType::StepLogic);
	
	b2FixtureDef fd;
	fd.isSensor = true;
	fd.userData = reinterpret_cast<void*>(1);
	ent->get_phobj().add_circle(fd, TURRET_RADIUS, 1);
	
	EVS_CONNECT1(ent->get_phobj().ev_contact, on_cnt);
}
void ETurret::Logic::step()
{
	vec2fp p0 = ent->get_phy().get_pos();
	
	vec2fp tar;
	float ed = TURRET_RADIUS + 1;
	
	for (size_t i=0; i < tars.size(); ++i)
	{
		if (auto e = GameCore::get().get_ent( tars[i] ))
		{
			auto pos = e->get_phy().get_pos();
			auto rc = GameCore::get().get_phy().raycast_nearest( conv(p0), conv(pos) );
			if (!rc || rc->ent != e) continue;
			
			float d = p0.dist(pos);
			if (d < ed) {
				tar = pos;
				ed = d;
			}
		}
		else {
			tars.erase( tars.begin() + i );
			--i;
		}
	}
	
	if (ed < TURRET_RADIUS) {
		ent->get_eqp()->shoot(tar);
		
		auto& rot = dynamic_cast<EC_RenderBot&>(*ent->get_ren()).rot;
		rot = lerp_angle(rot, (tar - p0).angle(), 0.5);
	}
	else if (!tars.empty()) {
		vec2fp tar = GameCore::get().get_ent( tars.front() )->get_phy().get_pos();
		auto& rot = dynamic_cast<EC_RenderBot&>(*ent->get_ren()).rot;
		rot = lerp_angle(rot, (tar - p0).angle(), 0.5);
	}
	else {
		dynamic_cast<EC_RenderBot&>(*ent->get_ren()).rot += rr_val;
		rr_left -= GameCore::step_len;
		if (rr_left.is_negative())
		{
			rr_left = TimeSpan::seconds(1.5);
			rr_flag = !rr_flag;
			rr_val = rr_flag? 0 : rnd_stat().range(-M_PI, M_PI) * (GameCore::time_mul / rr_left.seconds());
		}
	}
}
void ETurret::Logic::on_cnt(const ContactEvent& ev)
{
	if (!ev.fix_this) return;
	
	if		(ev.type == ContactEvent::T_BEGIN)
	{
		if ((ent->get_team() == TEAM_BOTS && ev.other->get_team() == TEAM_PLAYER) ||
		    (ent->get_team() == TEAM_PLAYER && ev.other->get_team() == TEAM_BOTS))
		{
			tars.push_back(ev.other->index);
		}
	}
	else if (ev.type == ContactEvent::T_END)
	{
		auto it = std::find( tars.begin(), tars.end(), ev.other->index );
		if (it != tars.end()) tars.erase(it);
	}
}
