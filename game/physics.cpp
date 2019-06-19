#include "game_core.hpp"
#include "physics.hpp"

EC_Physics::EC_Physics(const b2BodyDef& def)
{
	body = GameCore::get().get_phy().world.CreateBody(&def);
}
EC_Physics::~EC_Physics()
{
	ent->get_core().get_phy().free_body(body);
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



PhysicsWorld::PhysicsWorld(GameCore& core): core(core), world(b2Vec2(0,0))
{
	world.SetAllowSleeping(true);
//	world.SetContinuousPhysics(true);
//	world.SetDebugDraw(ph_debugdraw);
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
