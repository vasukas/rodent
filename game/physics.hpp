#ifndef PHYSICS_HPP
#define PHYSICS_HPP

#include <vector>
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
	float b_radius = 0.f; ///< Approximate radius, calculated from fixtures
	
	EC_Physics(const b2BodyDef& def); ///< Creates body
	~EC_Physics();
	
	void add_circle(b2FixtureDef& fd, float radius, float mass);
	void add_box(b2FixtureDef& fd, vec2fp half_extents, float mass);
	
	void attach_to(EC_Physics& target); ///< Fixed
	void detach(); ///< Detaches self
	
	void calc_radius(); ///< Recalculates radius
	
private:
	std::vector<b2Joint*> js;
	void destroy(b2Joint* j);
};

inline EC_Physics* getptr(b2Body* b) {return static_cast<EC_Physics*>(b->GetUserData());}



class PhysicsWorld
{
private:
	std::unique_ptr<b2Draw> c_draw;
	std::unique_ptr<b2ContactListener> c_lstr;
	
public:
	struct CastResult {
		Entity* ent;
		void* fix; ///< Fixture userdata
		float distance; ///< Distance from start
	};
	struct RaycastResult : CastResult {
		vec2fp poi; ///< Point of impact
	};
	
	GameCore& core;
	b2World world;
	
	
	PhysicsWorld(GameCore& core);
	~PhysicsWorld();
	void step();
	
	
	
	/// Appends result - all object along ray
	void raycast_all(std::vector<RaycastResult>& es, vec2fp from, vec2fp to);
	
	/// Returns nearest object hit
	std::optional<RaycastResult> raycast_nearest(vec2fp from, vec2fp to, bool ignore_sensors = true);
	
	/// Appends result - all objects inside the circle
	void circle_cast_all(std::vector<CastResult>& es, vec2fp ctr, float radius);
	
	/// Appends result - objects inside the circle which are nearest to center
	void circle_cast_nearest(std::vector<RaycastResult>& es, vec2fp ctr, float radius);
};

#endif // PHYSICS_HPP
