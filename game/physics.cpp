#include "game_core.hpp"
#include "physics.hpp"

static b2Draw* create_debug_draw();



EC_Physics::EC_Physics(const b2BodyDef& def)
{
	auto& ph = GameCore::get().get_phy();
	
	body = ph.world.CreateBody(&def);
	body->SetUserData(this);
}
EC_Physics::~EC_Physics()
{
	for (auto& j : js) destroy(j);
	ent->core.get_phy().free_body(body);
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



PhysicsWorld::PhysicsWorld(GameCore& core): core(core), world(b2Vec2(0,0))
{
	dbg_draw.reset( create_debug_draw() );
	
//	world.SetContinuousPhysics(true);
	world.SetDebugDraw( dbg_draw.get() );
}
PhysicsWorld::~PhysicsWorld() = default;
void PhysicsWorld::step()
{
	in_step = true;
	world.Step(core.step_len.seconds(), 8, 3);
	in_step = false;
	
	for (auto& b : to_remove) world.DestroyBody(b);
	to_remove.clear();
}
void PhysicsWorld::free_body(b2Body* b)
{
	if (in_step) to_remove.push_back(b);
	else world.DestroyBody(b);
}



#include "render/ren_imm.hpp"

class PhDebugDraw : public b2Draw
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
	
	
	PhDebugDraw() {
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
	void DrawTransform(const b2Transform& xf)
	{
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
b2Draw* create_debug_draw()
{
	return new PhDebugDraw;
}
