#include "vaslib/vas_cpp_utils.hpp"
#include "game_core.hpp"
#include "logic.hpp"
#include "physics.hpp"

#include "render/ren_imm.hpp"
#include "presenter.hpp"



EC_Physics::EC_Physics(const b2BodyDef& def)
{
	auto& ph = GameCore::get().get_phy();
	
	body = ph.world.CreateBody(&def);
	body->SetUserData(this);
}
EC_Physics::~EC_Physics()
{
	for (auto& j : js) destroy(j);
	body->GetWorld()->DestroyBody(body);
}
void EC_Physics::add_circle(b2FixtureDef& fd, float radius, float mass)
{
	b2CircleShape shp;
	shp.m_radius = radius;
	
	float area = M_PI * shp.m_radius * shp.m_radius;
	fd.density = mass / area;
	fd.shape = &shp;
	body->CreateFixture(&fd);
}
void EC_Physics::add_box(b2FixtureDef& fd, vec2fp half_extents, float mass)
{
	b2PolygonShape shp;
	b2Vec2 v = conv(half_extents);
	shp.SetAsBox(v.x, v.y);
	
	float area = v.x * v.y * 4;
	fd.density = mass / area;
	fd.shape = &shp;
	body->CreateFixture(&fd);
}
void EC_Physics::attach_to(EC_Physics& target)
{
	b2WeldJointDef def;
	def.bodyA = target.body;
	def.bodyB = body;
	def.localAnchorA = {0, 0};
	def.localAnchorB = body->GetWorldCenter() - target.body->GetWorldCenter();
	
	auto j = body->GetWorld()->CreateJoint(&def);
	js.push_back(j);
	target.js.push_back(j);
}
void EC_Physics::detach()
{
	for (size_t i = 0; i < js.size(); ) {
		if (js[i]->GetBodyA() == body) ++i;
		else {
			destroy(js[i]);
			js.erase(js.begin() + i);
		}
	}
}
void EC_Physics::destroy(b2Joint* j)
{
	b2Body* b = j->GetBodyA();
	if (b == body) b = j->GetBodyB();
	auto p = getptr(b);
	auto it = std::find(p->js.begin(), p->js.end(), j);
	p->js.erase(it);
	body->GetWorld()->DestroyJoint(j);
}



class PHW_Draw : public b2Draw
{
public:
	float ln_wid = 0.2f;
	
	uint32_t getclr(const b2Color& color)
	{
		uint32_t c = 0;
		c |= uint8_t(clampf(color.r, 0, 1) * 255); c <<= 8;
		c |= uint8_t(clampf(color.g, 0, 1) * 255); c <<= 8;
		c |= uint8_t(clampf(color.b, 0, 1) * 255); c <<= 8;
		c |= uint8_t(clampf(color.a, 0, 1) * 255);
		return c;
	}
	void draw_line(const b2Vec2& p1, const b2Vec2& p2, const b2Color& color)
	{
		RenImm::get().draw_line(conv(p1), conv(p2), getclr(color), ln_wid);
	}
	void draw(const b2Vec2* ivs, int32 ivn, const b2Color& color)
	{
		std::vector<vec2fp> vs;
		vs.reserve(ivn);
		for (int i=0; i<ivn; i++) vs.emplace_back(conv(ivs[i]));
		RenImm::get().draw_vertices(vs);
		RenImm::get().draw_vertices_end(getclr(color));
	}
	void circle(const b2Vec2& center, float32 radius, const b2Color& color, float rot, bool outline)
	{
		const int vn = 8;
		b2Vec2 vs[vn];
		vs[0] = center;
		for (int i=1; i<vn; i++) {
			vs[i] = center;
			double a = M_PI*2 / (vn-2) * (i-1) + rot;
			vs[i].x += cos(a) * radius;
			vs[i].y += sin(a) * radius;
		}
		polygon(vs, vn, color, outline);
	}
	void polygon(const b2Vec2* ivs, int32 ivn, const b2Color& color, bool outline)
	{
		int trin = (ivn - 2);
		if (outline) {
			for (int i=1; i<ivn; i++)
				draw_line(ivs[i], ivs[i-1], color);
			draw_line(ivs[ivn-1], ivs[0], color);
			return;
		}
		
		b2Vec2 vs[24];
		for (int i=0; i<trin; i++) {
			vs[i*3+0] = ivs[0];
			vs[i*3+1] = ivs[i+1];
			vs[i*3+2] = ivs[i+2];
		}
		draw(vs, trin * 3, color);
	}
	
	
	PHW_Draw() {
		// e_shapeBit | e_jointBit | e_aabbBit | e_pairBit | e_centerOfMassBit
		SetFlags( e_shapeBit );
	}
	void DrawPolygon(const b2Vec2* vertices, int32 vertexCount, const b2Color& color)
	{
		polygon(vertices, vertexCount, color, true);
	}
	void DrawSolidPolygon(const b2Vec2* vertices, int32 vertexCount, const b2Color& color)
	{
		polygon(vertices, vertexCount, color, false);
	}
	void DrawCircle(const b2Vec2& center, float32 radius, const b2Color& color)
	{
		circle(center, radius, color, 0, true);
	}
	void DrawSolidCircle(const b2Vec2& center, float32 radius, const b2Vec2& axis, const b2Color& color)
	{
		circle(center, radius, color, atan2(axis.y, axis.x), false);
	}
	void DrawSegment(const b2Vec2& p1, const b2Vec2& p2, const b2Color& color)
	{
		draw_line(p1, p2, color);
	}
	void DrawTransform(const b2Transform& xf) {(void) xf;}
	void DrawPoint(const b2Vec2& p, float32 size, const b2Color& color)
	{
		b2Vec2 vs[4];
		vs[0] = p;
		vs[1] = p; vs[1].x += size;
		vs[2] = p; vs[2].x += size; vs[2].y += size;
		vs[3] = p; vs[3].y += size;
		DrawPolygon(vs, 4, color);
	}
};
class PHW_Lstr : public b2ContactListener
{
public:
	vec2fp avg_point(b2Contact* ct)
	{
		int pc = ct->GetManifold()->pointCount;
		b2WorldManifold wm;
		ct->GetWorldManifold(&wm);
		b2Vec2 p = b2Vec2_zero;
		for (int i=0; i<pc; ++i) p += wm.points[i];
		return conv(p) / pc;
	}
	void report(EC_Logic::ContactEvent::Type type, b2Contact* ct, float imp)
	{
		auto ea = getptr(ct->GetFixtureA()->GetBody())->ent;
		auto eb = getptr(ct->GetFixtureB()->GetBody())->ent;
		auto la = ea->get_log();
		auto lb = ea->get_log();
		if (!la && !lb) return;
		
		EC_Logic::ContactEvent ce;
		ce.type = type;
		ce.imp = imp;
		
		ce.point = avg_point(ct);
		ce.fix_this  = ct->GetFixtureA()->GetUserData();
		ce.fix_other = ct->GetFixtureB()->GetUserData();
		
		if (la) {
			ce.other = eb;
			la->on_event(ce);
		}
		if (lb) {
			std::swap(ce.fix_this, ce.fix_other);
			ce.other = ea;
			lb->on_event(ce);
		}
	}
	void BeginContact(b2Contact* ct) {
		report(EC_Logic::ContactEvent::T_BEGIN, ct, 0.f);
	}
	void EndContact(b2Contact* ct) {
		report(EC_Logic::ContactEvent::T_END, ct, 0.f);
	}
	void PostSolve(b2Contact* ct, const b2ContactImpulse* res) {
		float imp = 0.f;
		int pc = ct->GetManifold()->pointCount;
		for (int i=0; i<pc; ++i) imp += res->normalImpulses[i];
		
		if (!ct->GetFixtureA()->IsSensor() && !ct->GetFixtureB()->IsSensor())
			GamePresenter::get().effect(FE_HIT, {avg_point(ct), 0.f}, imp / 20.f);
		
		report(EC_Logic::ContactEvent::T_RESOLVE, ct, imp);
	}
};



PhysicsWorld::PhysicsWorld(GameCore& core): core(core), world(b2Vec2(0,0))
{
//	world.SetContinuousPhysics(true);
	
	c_lstr.reset( new PHW_Lstr );
	world.SetContactListener( c_lstr.get() );
	
	c_draw.reset( new PHW_Draw );
	world.SetDebugDraw( c_draw.get() );
}
PhysicsWorld::~PhysicsWorld() = default;
void PhysicsWorld::step()
{
	world.Step(core.step_len.seconds(), 8, 3);
}
void PhysicsWorld::raycast_all(std::vector<RaycastResult>& es, vec2fp from, vec2fp to)
{
	class Cb : public b2RayCastCallback {
	public:
		std::vector<RaycastResult>& es;
		float len;
		Cb(std::vector<RaycastResult>& es, float len): es(es), len(len) {}
		float32 ReportFixture(b2Fixture* fix, const b2Vec2& point, const b2Vec2&, float32 frac)
		{
			if (fix->IsSensor()) return 1;
			reserve_more_block(es, 256);
			es.push_back({ {getptr(fix->GetBody())->ent, fix->GetUserData(), frac * len}, conv(point) });
			return 1;
		}
	};
	Cb cb(es, from.dist(to));
	world.RayCast(&cb, conv(from), conv(to));
}
std::optional<PhysicsWorld::RaycastResult> PhysicsWorld::raycast_nearest(vec2fp from, vec2fp to, bool ignore_sensors)
{
	class Cb : public b2RayCastCallback {
	public:
		RaycastResult res;
		bool ign;
		Cb(bool ign): ign(ign) {res.ent = nullptr;}
		float32 ReportFixture(b2Fixture* fix, const b2Vec2& point, const b2Vec2&, float32 frac)
		{
			if (ign && fix->IsSensor()) return 1;
			res.ent = getptr(fix->GetBody())->ent;
			res.fix = fix;
			res.distance = frac;
			res.poi = conv(point);
			return frac;
		}
	};
	Cb cb(ignore_sensors);
	world.RayCast(&cb, conv(from), conv(to));
	
	if (cb.res.ent) {
		cb.res.distance *= from.dist(to);
		return cb.res;
	}
	return {};
}
void PhysicsWorld::circle_cast_all(std::vector<CastResult>& es, vec2fp ctr, float radius)
{
	class Cb : public b2QueryCallback {
	public:
		std::vector<CastResult>& es;
		float rad;
		b2Vec2 ctr;
		Cb(std::vector<CastResult>& es, float rad, b2Vec2 ctr): es(es), rad(rad), ctr(ctr) {}
		bool ReportFixture(b2Fixture* fix)
		{
			if (fix->IsSensor()) return true;
			float d = (fix->GetBody()->GetWorldCenter() - ctr).LengthSquared();
			
			float z = fix->GetShape()->m_radius;
			d -= z*z;
			
			if (d < rad*rad)
			{
				reserve_more_block(es, 256);
				es.push_back({ getptr(fix->GetBody())->ent, fix->GetUserData(), std::sqrt(d) });
			}
			return true;
		}
	};
	Cb cb(es, radius, conv(ctr));
	b2AABB box;
	box.lowerBound = conv(ctr - vec2fp::one(radius));
	box.upperBound = conv(ctr + vec2fp::one(radius));
	world.QueryAABB(&cb, box);
}
void PhysicsWorld::circle_cast_nearest(std::vector<RaycastResult>& es, vec2fp ctr, float radius)
{
	std::vector<CastResult> fc;
	circle_cast_all(fc, ctr, radius);
	
	reserve_more(es, fc.size());
	for (auto& f : fc) {
		auto res = raycast_nearest(ctr, conv(f.ent->get_phy()->body->GetWorldCenter()));
		if (res && res->ent == f.ent && res->fix == f.fix)
			es.push_back({ {f.ent, f.fix, res->distance}, res->poi });
	}
}
