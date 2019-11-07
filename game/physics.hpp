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

/// Copied from b2WorldCallbacks.cpp
bool should_collide(const b2Filter& filterA, const b2Filter& filterB);



/// Owned by b2Fixture (if deleted through EC_Physics)
struct FixtureInfo
{
	enum
	{
		TYPEFLAG_WALL = 1, ///< Just static wall
		TYPEFLAG_OPAQUE = 2, ///< Can't be seen through
		TYPEFLAG_INTERACTIVE = 4 ///< Entity is EInteractive
	};
	
	b2Fixture* fix = nullptr;
	flags_t typeflags = 0;
	
	FixtureInfo() = default;
	FixtureInfo(flags_t typeflags): typeflags(typeflags) {}
	virtual ~FixtureInfo();
	void destroy_fixture(); ///< Also destroys self
	
	/// Returns index of armored area on entity
	virtual std::optional<size_t> get_armor_index() {return {};}
};



/// Means this is armored part of entity
struct FixtureArmor : FixtureInfo
{
	size_t index;
	FixtureArmor(size_t index): index(index) {}
	virtual std::optional<size_t> get_armor_index() {return index;}
};



struct CollisionEvent
{
	enum Type {
		T_BEGIN,  ///< Beginning of contact
		T_END,    ///< End of contact
		T_RESOLVE ///< Collision resolution
	};
	Type type;
	
	Entity* other; ///< Another entity
	FixtureInfo* fix_this; ///< Fixture usedata for this entity
	FixtureInfo* fix_other; ///< Fixture userdata for another entity
	vec2fp point; ///< Averaged world point of impact
	float imp; ///< Resolution impulse (set only for T_RESOLVE)
	
	b2Contact* contact; ///< Actual contact
};



struct EC_Physics : ECompPhysics
{
	enum : decltype(b2Filter::categoryBits)
	{
		CF_ALL = std::numeric_limits<decltype(b2Filter::categoryBits)>::max(),
		CF_DEFAULT = 1,
		CF_BULLET  = 2
	};
	
	b2Body* body;
	ev_signal<CollisionEvent> ev_contact;
	
	
	EC_Physics(Entity* ent, const b2BodyDef& def); ///< Creates body
	~EC_Physics();
	
	b2Fixture* add_circle(b2FixtureDef& fd, float radius, float mass, FixtureInfo* info = nullptr);
	b2Fixture* add_box(b2FixtureDef& fd, vec2fp half_extents, float mass, FixtureInfo* info = nullptr);
	b2Fixture* add_box(b2FixtureDef& fd, vec2fp half_extents, float mass, Transform tr, FixtureInfo* info = nullptr);
	
	Transform get_trans() const override {return conv(body->GetTransform());}
	vec2fp    get_pos() const override {return conv(body->GetWorldCenter());}
	Transform get_vel() const override {return Transform{conv(body->GetLinearVelocity()), body->GetAngularVelocity()};}
	float get_radius()  const override; ///< Approximate radius, calculated from fixtures
	
	void destroy(b2Fixture* f);
	
private:
	b2Fixture* add(b2FixtureDef& fd, const b2Shape* shp, FixtureInfo* info);
	mutable std::optional<float> b_radius;
};

inline EC_Physics*  getptr(b2Body* b)    {return static_cast<EC_Physics *>(b->GetUserData());}
inline FixtureInfo* getptr(b2Fixture* f) {return static_cast<FixtureInfo*>(f->GetUserData());}

inline void setptr(b2Fixture* f, FixtureInfo* info) {
	f->SetUserData(info);
	info->fix = f;
}



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
	std::unique_ptr<b2Draw> c_draw;
	std::unique_ptr<b2ContactListener> c_lstr;
	std::vector<std::function<void()>> post_cbs;
	
public:
	struct PointResult
	{
		Entity* ent;
		FixtureInfo* fix; ///< Fixture userdata
	};
	struct CastResult : PointResult
	{
		float distance; ///< Distance from start
	};
	struct RaycastResult : CastResult
	{
		b2Vec2 poi; ///< Point of impact
	};
	
	struct CastFilter
	{
		std::function<bool(Entity*, b2Fixture*)> check;
		std::optional<b2Filter> ft;
		bool ignore_sensors;
		
		CastFilter(
		        std::function<bool(Entity*, b2Fixture*)> check = {},
		        std::optional<b2Filter> ft = {},
		        bool ignore_sensors = true)
			:
		    check(std::move(check)), ft(std::move(ft)), ignore_sensors(ignore_sensors)
		{}
		bool is_ok(b2Fixture& f);
	};
	
	GameCore& core;
	b2World world;
	
	size_t raycast_count = 0;
	size_t aabb_query_count = 0;
	
	
	PhysicsWorld(GameCore& core);
	~PhysicsWorld();
	void step();
	
	
	
	/// Returns distance if entity is directly visible
	std::optional<float> los_check(vec2fp from, Entity* target, std::optional<float> width = {}, bool is_bullet = false);
	
	/// Appends result - all object along ray
	void raycast_all(std::vector<RaycastResult>& es, b2Vec2 from, b2Vec2 to, CastFilter cf = {});
	
	/// Returns nearest object hit
	std::optional<RaycastResult> raycast_nearest(b2Vec2 from, b2Vec2 to, CastFilter cf = {});
	
	/// Appends result - all objects inside the circle
	void circle_cast_all(std::vector<CastResult>& es, b2Vec2 ctr, float radius, CastFilter cf = {});
	
	/// Appends result - objects inside the circle which are nearest to center
	void circle_cast_nearest(std::vector<RaycastResult>& es, b2Vec2 ctr, float radius, CastFilter cf = {});
	
	/// Returns non-sensor object in which point lays
	std::optional<PointResult> point_cast(b2Vec2 ctr, float radius, CastFilter cf = {});
	
	/// Appends result - all objects inside rectangle
	void area_cast(std::vector<PointResult>& es, Rectfp area, CastFilter cf = {});
	
	/// Executes function after step (or immediatly, if not inside one)
	void post_step(std::function<void()> f);
};

#endif // PHYSICS_HPP
