#ifndef PHYSICS_HPP
#define PHYSICS_HPP

#include <vector>
#include <Box2D/Box2D.h>
#include "utils/ev_signal.hpp"
#include "entity.hpp"

inline b2Vec2 conv(const vec2fp& p) {return {p.x, p.y};}
inline vec2fp conv(const b2Vec2& p) {return {p.x, p.y};}
inline b2Transform conv(const Transform& t) {return {conv(t.pos), b2Rot(t.rot)};}
inline Transform conv(const b2Transform& t) {return Transform{conv(t.p), t.q.GetAngle()};}



struct ContactEvent
{
	enum Type {
		T_BEGIN,  ///< Beginning of contact
		T_END,    ///< End of contact
		T_RESOLVE ///< Collision resolution
	};
	Type type;
	
	Entity* other; ///< Another entity
	void* fix_this; ///< Fixture usedata for this entity
	void* fix_other; ///< Fixture userdata for another entity
	vec2fp point; ///< Averaged world point of impact
	float imp; ///< Resolution impulse (set only for T_RESOLVE)
};



struct EC_Physics : ECompPhysics
{
	b2Body* body;
	
	ev_signal<ContactEvent> ev_contact;
	
	
	
	EC_Physics(Entity* ent, const b2BodyDef& def); ///< Creates body
	~EC_Physics();
	
	void add_circle(b2FixtureDef& fd, float radius, float mass);
	void add_box(b2FixtureDef& fd, vec2fp half_extents, float mass);
	void add_box(b2FixtureDef& fd, vec2fp half_extents, float mass, Transform tr);
	
	void attach_to(EC_Physics& target); ///< Fixed
	void detach(); ///< Detaches self
	
	Transform get_trans() const override {return conv(body->GetTransform());}
	vec2fp    get_pos() const override {return conv(body->GetWorldCenter());}
	Transform get_vel() const override {return Transform{conv(body->GetLinearVelocity()), body->GetAngularVelocity()};}
	float get_radius()  const override; ///< Approximate radius, calculated from fixtures
	
private:
	std::vector<b2Joint*> js;
	void destroy(b2Joint* j);
	
	mutable std::optional<float> b_radius;
};

inline EC_Physics* getptr(b2Body* b) {return static_cast<EC_Physics*>(b->GetUserData());}



struct EC_VirtualBody : ECompPhysics
{
	Transform pos;
	float radius = 0.5f;
	
	EC_VirtualBody(Entity* ent, Transform pos, std::optional<Transform> vel = {});
	void set_vel(std::optional<Transform> vel);
	void step() override;
	
	Transform get_trans() const override {return pos;}
	vec2fp    get_pos() const override {return pos.pos;}
	Transform get_vel() const override {return vel? *vel : Transform{};}
	float get_radius()  const override {return radius;}
	
private:
	std::optional<Transform> vel;
};



class PhysicsWorld
{
private:
	std::unique_ptr<b2Draw> c_draw;
	std::unique_ptr<b2ContactListener> c_lstr;
	std::vector<std::function<void()>> post_cbs;
	
public:
	struct CastResult {
		Entity* ent;
		void* fix; ///< Fixture userdata
		float distance; ///< Distance from start
	};
	struct RaycastResult : CastResult {
		b2Vec2 poi; ///< Point of impact
	};
	struct PointResult {
		Entity* ent;
		void* fix; ///< Fixture userdata
	};
	
	GameCore& core;
	b2World world;
	
	
	PhysicsWorld(GameCore& core);
	~PhysicsWorld();
	void step();
	
	
	
	/// Appends result - all object along ray
	void raycast_all(std::vector<RaycastResult>& es, b2Vec2 from, b2Vec2 to);
	
	/// Returns nearest object hit
	std::optional<RaycastResult> raycast_nearest(b2Vec2 from, b2Vec2 to, std::function<bool(Entity*, b2Fixture*)> check = {});
	
	/// Appends result - all objects inside the circle
	void circle_cast_all(std::vector<CastResult>& es, b2Vec2 ctr, float radius);
	
	/// Appends result - objects inside the circle which are nearest to center
	void circle_cast_nearest(std::vector<RaycastResult>& es, b2Vec2 ctr, float radius);
	
	/// Returns non-sensor object in which point lays
	std::optional<PointResult> point_cast(b2Vec2 ctr, float radius);
	
	/// Executes function after step (or immediatly, if not inside one)
	void post_step(std::function<void()> f);
};

#endif // PHYSICS_HPP
