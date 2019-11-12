#include "vaslib/vas_cpp_utils.hpp"
#include "game_core.hpp"
#include "physics.hpp"

#include "render/ren_imm.hpp"
#include "vaslib/vas_log.hpp"
#include "client/presenter.hpp"

const float raycast_zero_dist = 0.05; ///< Square of distance at which raycast not performed



bool should_collide(const b2Filter& filterA, const b2Filter& filterB)
{
	if (filterA.groupIndex == filterB.groupIndex && filterA.groupIndex != 0)
	{
		return filterA.groupIndex > 0;
	}

	bool collide = (filterA.maskBits & filterB.categoryBits) != 0 && (filterA.categoryBits & filterB.maskBits) != 0;
	return collide;
}



FixtureInfo::~FixtureInfo()
{
	if (fix) fix->SetUserData(nullptr);
}
void FixtureInfo::destroy_fixture()
{
	if (fix) fix->GetBody()->DestroyFixture(fix);
	fix = nullptr;
	delete this;
}



EC_Physics::EC_Physics(Entity* ent, const b2BodyDef& def)
    : ECompPhysics(ent)
{
	auto& ph = GameCore::get().get_phy();
	body = ph.world.CreateBody(&def);
	body->SetUserData(this);
}
EC_Physics::~EC_Physics()
{
	for (auto f = body->GetFixtureList(); f; f = f->GetNext())
		delete static_cast<FixtureInfo*>(f->GetUserData());
	
	body->GetWorld()->DestroyBody(body);
}
b2Fixture* EC_Physics::add_circle(b2FixtureDef& fd, float radius, float mass, FixtureInfo* info)
{
	b2CircleShape shp;
	shp.m_radius = radius;
	
	fd.density = mass / (M_PI * shp.m_radius * shp.m_radius);
	return add(fd, &shp, info);
}
b2Fixture* EC_Physics::add_box(b2FixtureDef& fd, vec2fp half_extents, float mass, FixtureInfo* info)
{
	b2PolygonShape shp;
	b2Vec2 v = conv(half_extents);
	shp.SetAsBox(v.x, v.y);
	
	fd.density = mass / (v.x * v.y * 4);
	return add(fd, &shp, info);
}
b2Fixture* EC_Physics::add_box(b2FixtureDef& fd, vec2fp half_extents, float mass, Transform tr, FixtureInfo* info)
{
	auto app = [&](vec2fp k){
		return conv (tr.apply (half_extents * k));
	};
	
	b2Vec2 ps[4];
	ps[0] = app({-1, -1});
	ps[1] = app({-1,  1});
	ps[2] = app({ 1,  1});
	ps[3] = app({ 1, -1});
	
	b2PolygonShape shp;
	shp.Set(ps, 4);
	
	fd.density = mass / (half_extents.area() * 4);
	return add(fd, &shp, info);
}
float EC_Physics::get_radius() const
{
	if (!b_radius)
	{
		b_radius = 0.f;
		
		for (auto f = body->GetFixtureList(); f; f = f->GetNext())
		{
			if (f->IsSensor()) continue;
			
			float r = 0.f;
			auto sa = f->GetShape();
			
			if      (sa->GetType() == b2Shape::e_circle) r = sa->m_radius;
			else if (sa->GetType() == b2Shape::e_polygon)
			{
				auto s = static_cast<b2PolygonShape*>(sa);
				for (int i=0; i<s->m_count; ++i)
					r = std::max(r, s->m_vertices[i].LengthSquared());
				r = sqrt(r);
			}
			
			b_radius = std::max(*b_radius, r);
		}
	}
	return *b_radius;
}
void EC_Physics::destroy(b2Fixture* f)
{
	delete static_cast<FixtureInfo*>(f->GetUserData());
	body->DestroyFixture(f);
}
b2Fixture* EC_Physics::add(b2FixtureDef& fd, const b2Shape *shp, FixtureInfo* info)
{
	fd.shape = shp;
	fd.userData = nullptr;
	
	auto f = body->CreateFixture(&fd);
	if (info) setptr(f, info);
	
	b_radius.reset();
	return f;
}



EC_VirtualBody::EC_VirtualBody(Entity *ent, Transform pos, std::optional<Transform> vel)
    : ECompPhysics(ent), pos(pos), vel(vel)
{
	if (vel) reg(ECompType::StepPostUtil);
}
void EC_VirtualBody::set_vel(std::optional<Transform> new_vel)
{
	bool had = !!vel;
	vel = new_vel;
	if (had != !!vel)
	{
		if (vel) reg(ECompType::StepPostUtil);
		else   unreg(ECompType::StepPostUtil);
	}
}
void EC_VirtualBody::step()
{
	pos.add(*vel * GameCore::time_mul);
}



class PHW_Draw : public b2Draw
{
public:
	float ln_wid = 0.2f;
	
	uint32_t getclr(const b2Color& color)
	{
		uint32_t c = 0;
		c |= uint8_t(clampf_n(color.r) * 255); c <<= 8;
		c |= uint8_t(clampf_n(color.g) * 255); c <<= 8;
		c |= uint8_t(clampf_n(color.b) * 255); c <<= 8;
		c |= uint8_t(clampf_n(color.a) * 255);
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
		SetFlags( e_shapeBit | e_centerOfMassBit );
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
	void DrawTransform(const b2Transform& xf)
	{
		draw_line(xf.p, xf.p + xf.q.GetXAxis(), b2Color(1, 0, 0));
		draw_line(xf.p, xf.p + xf.q.GetYAxis(), b2Color(0, 0, 1));
	}
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
		if (pc) p *= 1.f / pc;
		return conv(p);
	}
	void report(CollisionEvent::Type type, b2Contact* ct, float imp)
	{
		auto ea = getptr(ct->GetFixtureA()->GetBody());
		auto eb = getptr(ct->GetFixtureB()->GetBody());
		if (!ea->ev_contact.has_conn() && !eb->ev_contact.has_conn()) return;
		
		CollisionEvent ce;
		ce.type = type;
		ce.imp = imp;
		ce.contact = ct;
		
		ce.point = avg_point(ct);
		ce.fix_this  = getptr(ct->GetFixtureA());
		ce.fix_other = getptr(ct->GetFixtureB());
		
		ce.other = eb->ent;
		ea->ev_contact.signal(ce);
			
		std::swap(ce.fix_this, ce.fix_other);
		ce.other = ea->ent;
		eb->ev_contact.signal(ce);
	}
	void BeginContact(b2Contact* ct) {
		report(CollisionEvent::T_BEGIN, ct, 0.f);
	}
	void EndContact(b2Contact* ct) {
		report(CollisionEvent::T_END, ct, 0.f);
	}
	void PostSolve(b2Contact* ct, const b2ContactImpulse* res) {
		float imp = 0.f;
		int pc = ct->GetManifold()->pointCount;
		for (int i=0; i<pc; ++i) imp += res->normalImpulses[i];
		
		if (!ct->GetFixtureA()->IsSensor() && !ct->GetFixtureB()->IsSensor() && imp > 40.f) {
			GamePresenter::get()->effect(FE_HIT, { Transform{avg_point(ct)}, imp / 60.f });
		}
		
		report(CollisionEvent::T_RESOLVE, ct, imp);
	}
};



bool PhysicsWorld::CastFilter::is_ok(b2Fixture& f)
{
	if (ignore_sensors && f.IsSensor()) return false;
	if (ft && !should_collide(*ft, f.GetFilterData())) return false;
	if (check) {
		auto p = getptr(f.GetBody());
		if (!p) throw std::runtime_error("PhysicsWorld::CastFilter::is_ok() non-entity body");
		if (!check(p->ent, &f)) return false;
	}
	return true;
}

PhysicsWorld::PhysicsWorld(GameCore& core)
    : core(core), world(b2Vec2(0,0))
{
	VLOGI("Box2D version: {}.{}.{}", b2_version.major, b2_version.minor, b2_version.revision);
	
	c_lstr.reset( new PHW_Lstr );
	world.SetContactListener( c_lstr.get() );
	
	c_draw.reset( new PHW_Draw );
	world.SetDebugDraw( c_draw.get() );
}
PhysicsWorld::~PhysicsWorld() = default;
void PhysicsWorld::step()
{
	world.Step(core.step_len.seconds(), 8, 3);
	
	for (auto& c : post_cbs) c();
	post_cbs.clear();
}
std::optional<float> PhysicsWorld::los_check(vec2fp from, Entity* target, std::optional<float> width, bool is_bullet)
{
	CastFilter cf{
	[i = target->index](Entity* e, b2Fixture* f) {
		if (e->index == i) return true;
		auto fi = getptr(f);
		return fi && (fi->typeflags & FixtureInfo::TYPEFLAG_OPAQUE);
	}};
	if (is_bullet) {
		cf.ft = b2Filter{};
		cf.ft->categoryBits = EC_Physics::CF_BULLET;
	}
	
	if (!width)
	{
		auto rc = raycast_nearest(conv(from), target->get_phobj().body->GetWorldCenter(), std::move(cf));
		if (rc && rc->ent == target) return rc->distance;
	}
	else
	{
		vec2fp to = target->get_phy().get_pos();
		
		vec2fp t = to;
		t.norm_to(*width);
		t.rot90cw();
		to -= t;
		
		for (int i=0; i<3; ++i) {
			vec2fp pos = to + t * (i / 2.f);
			
			auto rc = raycast_nearest(conv(from), conv(pos), std::move(cf));
			if (rc && rc->ent == target) return rc->distance;
		}
	}
	return {};
}
void PhysicsWorld::raycast_all(std::vector<RaycastResult>& es, b2Vec2 from, b2Vec2 to, CastFilter cf)
{
	if ((from - to).LengthSquared() < raycast_zero_dist) return;
	
	class Cb : public b2RayCastCallback {
	public:
		std::vector<RaycastResult>& es;
		float len;
		CastFilter& cf;
		
		Cb(std::vector<RaycastResult>& es, float len, CastFilter& cf) : es(es), len(len), cf(cf) {}
		float32 ReportFixture(b2Fixture* fix, const b2Vec2& point, const b2Vec2&, float32 frac)
		{
			if (!cf.is_ok(*fix)) return -1;
			
			reserve_more_block(es, 256);
			es.push_back({ {getptr(fix->GetBody())->ent, getptr(fix), frac * len}, point });
			return 1;
		}
	};
	Cb cb(es, (from - to).Length(), cf);
	world.RayCast(&cb, from, to);
	++ raycast_count;
}
std::optional<PhysicsWorld::RaycastResult> PhysicsWorld::raycast_nearest(b2Vec2 from, b2Vec2 to, CastFilter cf)
{
	if ((from - to).LengthSquared() < raycast_zero_dist) return {};
	
	class Cb : public b2RayCastCallback {
	public:
		std::optional<RaycastResult> res;
		CastFilter& cf;
		
		Cb(CastFilter& cf) : cf(cf) {}
		float32 ReportFixture(b2Fixture* fix, const b2Vec2& point, const b2Vec2&, float32 frac)
		{
			if (!cf.is_ok(*fix)) return -1;
			res = RaycastResult
			{
				getptr(fix->GetBody())->ent,
				getptr(fix),
				frac,
			    point
			};
			return frac;
		}
	};
	Cb cb(cf);
	world.RayCast(&cb, from, to);
	++ raycast_count;
	
	if (cb.res) cb.res->distance *= (from - to).Length();
	return cb.res;
}
void PhysicsWorld::circle_cast_all(std::vector<CastResult>& es, b2Vec2 ctr, float radius, CastFilter cf)
{
	class Cb : public b2QueryCallback {
	public:
		std::vector<CastResult>& es;
		float rad;
		b2Vec2 ctr;
		CastFilter& cf;
		
		Cb(std::vector<CastResult>& es, float rad, b2Vec2 ctr, CastFilter& cf) : es(es), rad(rad), ctr(ctr), cf(cf) {}
		bool ReportFixture(b2Fixture* fix)
		{
			if (!cf.is_ok(*fix)) return true;
			float d = (fix->GetBody()->GetWorldCenter() - ctr).LengthSquared();
			
			float z = getptr(fix->GetBody())->get_radius();
			d -= z*z;
			
			if (d < rad*rad)
			{
				reserve_more_block(es, 256);
				es.push_back({ getptr(fix->GetBody())->ent, getptr(fix), d > 0 ? std::sqrt(d) : 0 });
			}
			return true;
		}
	};
	Cb cb(es, radius, ctr, cf);
	b2AABB box;
	box.lowerBound = ctr - b2Vec2(radius, radius);
	box.upperBound = ctr + b2Vec2(radius, radius);
	world.QueryAABB(&cb, box);
	++ aabb_query_count;
}
void PhysicsWorld::circle_cast_nearest(std::vector<RaycastResult>& es, b2Vec2 ctr, float radius, CastFilter cf)
{
	std::vector<CastResult> fc;
	circle_cast_all(fc, ctr, radius, cf);
	
	reserve_more(es, fc.size());
	for (auto& f : fc)
	{
		auto& eb = f.ent->get_phobj();
		auto p = eb.body->GetWorldCenter();
		float dist = 0;
		
		bool ok = (ctr - p).LengthSquared() <= eb.get_radius() * eb.get_radius() + raycast_zero_dist;
		if (!ok) {
			auto res = raycast_nearest(ctr, p, cf);
			ok = res && res->ent == f.ent && res->fix == f.fix;
			if (ok) {
				p = res->poi;
				dist = res->distance;
			}
		}
		if (ok) es.push_back({ {f.ent, f.fix, dist}, p });
	}
}
std::optional<PhysicsWorld::PointResult> PhysicsWorld::point_cast(b2Vec2 ctr, float radius, CastFilter cf)
{
	class Cb : public b2QueryCallback {
	public:
		std::optional<PointResult> res;
		b2Vec2 ctr;
		CastFilter& cf;
		
		Cb(b2Vec2 ctr, CastFilter& cf): ctr(ctr), cf(cf) {}
		bool ReportFixture(b2Fixture* fix)
		{
			if (!cf.is_ok(*fix)) return true;
			if (!fix->TestPoint(ctr)) return true;
			
			res.emplace();
			res->ent = getptr(fix->GetBody())->ent;
			res->fix = getptr(fix);
			return false;
		}
	};
	Cb cb(ctr, cf);
	b2AABB box;
	box.lowerBound = ctr - b2Vec2(radius, radius);
	box.upperBound = ctr + b2Vec2(radius, radius);
	world.QueryAABB(&cb, box);
	++ aabb_query_count;
	return cb.res;
}
void PhysicsWorld::area_cast(std::vector<PointResult>& es, Rectfp area, CastFilter cf)
{
	class Cb : public b2QueryCallback {
	public:
		std::vector<PointResult>& es;
		CastFilter& cf;
		
		Cb(std::vector<PointResult>& es, CastFilter& cf): es(es), cf(cf) {}
		bool ReportFixture(b2Fixture* fix)
		{
			if (cf.is_ok(*fix))
			{
				reserve_more_block(es, 256);
				es.push_back({ getptr(fix->GetBody())->ent, getptr(fix) });
			}
			return true;
		}
	};
	Cb cb(es, cf);
	b2AABB box;
	box.lowerBound = conv(area.lower());
	box.upperBound = conv(area.upper());
	world.QueryAABB(&cb, box);
	++ aabb_query_count;
}
void PhysicsWorld::post_step(std::function<void()> f)
{
	if (!world.IsLocked()) f();
	else {
		reserve_more_block(post_cbs, 32);
		post_cbs.emplace_back( std::move(f) );
	}
}
