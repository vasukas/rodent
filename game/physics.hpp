#ifndef PHYSICS_HPP
#define PHYSICS_HPP

#include "Box2D/Box2D.h"
#include "entity.hpp"

inline b2Vec2 conv(const vec2fp& p) {return {p.x, p.y};}
inline vec2fp conv(const b2Vec2& p) {return {p.x, p.y};}
inline b2Transform conv(const Transform& t) {return {conv(t.pos), b2Rot(t.rot)};}
inline Transform conv(const b2Transform& t) {return {conv(t.p), t.q.GetAngle()};}



struct EC_Physics
{
	Entity* ent;
	b2Body* body;
	
	EC_Physics(const b2BodyDef& def);
	~EC_Physics();
	
	void add_circle(b2FixtureDef& fd, float radius, float mass);
	void add_box(b2FixtureDef& fd, vec2fp half_extents, float mass);
};



class PhysicsWorld
{
public:
	GameCore& core;
	b2World world;
	
	PhysicsWorld(GameCore& core);
	~PhysicsWorld();
	void step();
	
	void free_body(b2Body* b);
	
private:
	bool in_step = false;
	std::vector<b2Body*> to_remove;
};

#endif // PHYSICS_HPP
