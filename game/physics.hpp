#ifndef PHYSICS_HPP
#define PHYSICS_HPP

#include <vector>
#include <box2d/box2d.h>
#include "utils/ev_signal.hpp"
#include "entity.hpp"



inline b2Vec2 conv(const vec2fp& p) {return {p.x, p.y};}
inline vec2fp conv(const b2Vec2& p) {return {p.x, p.y};}

/// Copied from b2WorldCallbacks.cpp
bool should_collide(const b2Filter& filterA, const b2Filter& filterB);



/// Copied by b2Fixture
struct FixtureInfo final
{
	enum
	{
		TYPEFLAG_WALL = 1, ///< Just static wall
		TYPEFLAG_OPAQUE = 2, ///< Can't be seen through
		TYPEFLAG_INTERACTIVE = 4 ///< Entity is EInteractive
	};
	
	flags_t typeflags = 0;
	std::optional<size_t> armor_index = {};
	b2Fixture* fix = {}; ///< Always set
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
	
	b2Fixture* fix_phy; ///< Actual fixture for this entity
};



inline b2BodyDef bodydef(vec2fp at, bool is_dynamic, float angle = 0)
{
	b2BodyDef bd;
	bd.type = is_dynamic ? b2_dynamicBody : b2_staticBody;
	bd.position = conv(at);
	bd.angle = angle;
	return bd;
}
inline b2FixtureDef fixtdef(float friction, float restitution)
{
	b2FixtureDef fd;
	fd.friction = friction;
	fd.restitution = restitution;
	return fd;
}
inline b2FixtureDef fixtsensor()
{
	b2FixtureDef fd;
	fd.isSensor = true;
	return fd;
}



struct FixtureCreate
{
	b2FixtureDef fd; ///< Shape and userdata are ignored
	std::shared_ptr<b2Shape> shp;
	std::optional<FixtureInfo> info;
	
	static FixtureCreate empty() {return {};}
	static FixtureCreate circle(b2FixtureDef fd, float radius, float mass, std::optional<FixtureInfo> info = {});
	static FixtureCreate box(b2FixtureDef fd, vec2fp half_extents, float mass, std::optional<FixtureInfo> info = {});
	static FixtureCreate box(b2FixtureDef fd, vec2fp half_extents, Transform offset, float mass, std::optional<FixtureInfo> info = {});
	
private:
	FixtureCreate() = default;
};



struct EC_Physics : EC_Position
{
	enum : decltype(b2Filter::categoryBits)
	{
		CF_ALL = std::numeric_limits<decltype(b2Filter::categoryBits)>::max(),
		CF_DEFAULT = 1,
		CF_BULLET  = 2
	};
	
	/// May be null only if was created inside world step
	b2Body& body;
	
	/// Note: handlers must use PhysicsWorld::post_step() if they add new bodies
	ev_signal<CollisionEvent> ev_contact;
	
	
	
	EC_Physics(Entity& ent, const b2BodyDef& def); ///< Creates body
	~EC_Physics();
	
	b2Fixture& add(FixtureCreate& c);
	b2Fixture& add(FixtureCreate&& c);
	
	b2Fixture& add(b2FixtureDef& fd, const b2Shape& shp, std::optional<FixtureInfo> info);
	void destroy(b2Fixture* f);
	
	vec2fp    get_pos()    const override {return conv(body.GetWorldCenter());}
	vec2fp    get_vel()    const override {return conv(body.GetLinearVelocity());}
	float     get_radius() const override; ///< Approximate radius, calculated from fixtures
	float get_real_angle() const override {return body.GetAngle();}
	
	bool is_material() const; ///< Iterates all fixtures and returns true if at least one is not sensor
	
private:
	mutable std::optional<float> b_radius;
};

/// Returns current info or nullptr
FixtureInfo* get_info(b2Fixture& f);

/// Replaces current info
void set_info(b2Fixture& f, std::optional<FixtureInfo> info);



struct SwitchableFixture
{
	SwitchableFixture(FixtureCreate fc); ///< Disabled on init
	~SwitchableFixture();
	
	bool is_enabled() const {return fix;}
	void set_enabled(Entity& ent, bool on);
	
	b2Fixture* get_fixture() {return fix;}
	FixtureCreate& get_fc() {return fc;}
	
private:
	FixtureCreate fc;
	b2Fixture* fix = {};
};



struct EC_VirtualBody : EC_Position
{
	Transform pos;
	float radius = 0.5f;
	
	EC_VirtualBody(Entity& ent, Transform pos, std::optional<Transform> vel = {});
	void set_vel(std::optional<Transform> new_vel);
	Transform get_vel_tr() const {return vel ? *vel : Transform{};}
	
	vec2fp    get_pos()    const override {return pos.pos;}
	vec2fp    get_vel()    const override {return vel ? vel->pos : vec2fp{};}
	float     get_radius() const override {return radius;}
	float get_real_angle() const override {return pos.rot;}
	
private:
	std::optional<Transform> vel;
	void step() override;
};



class PhysicsWorld
{
	std::unique_ptr<b2Draw> c_draw;
	std::unique_ptr<b2ContactListener> c_lstr;
	
	struct Event {
		EntityIndex ia, ib;
		CollisionEvent ce;
		b2Fixture* fb;
	};
	std::vector<Event> col_evs;
	friend class PHW_Lstr;
	
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
		std::function<bool(Entity&, b2Fixture&)> check;
		std::optional<b2Filter> ft;
		bool ignore_sensors;
		
		CastFilter(
		        std::function<bool(Entity&, b2Fixture&)> check = {},
		        std::optional<b2Filter> ft = {},
		        bool ignore_sensors = true)
			:
		    check(std::move(check)), ft(std::move(ft)), ignore_sensors(ignore_sensors)
		{}
		bool is_ok(b2Fixture& f);
	};
	
	using QueryCb    = callable_ref<void(Entity&, b2Fixture&)>;
	using QueryCbRet = callable_ref<bool(Entity&, b2Fixture&)>; ///< If returns false, query stops (unless it's wide)
	using OptQueryCbRet = opt_callable_ref<bool(Entity&, b2Fixture&)>;
	
	GameCore& core;
	b2World world;
	
	// debug info
	size_t raycast_count = 0;
	size_t aabb_query_count = 0;
	
	
	
	PhysicsWorld(GameCore& core);
	~PhysicsWorld();
	
	void step();
	
	
	
	/// Returns distance if entity is directly visible
	std::optional<float> los_check(vec2fp from, Entity& target, std::optional<float> width = {}, bool is_bullet = false);
	
	/// Appends result - all object along ray
	void raycast_all(std::vector<RaycastResult>& es, b2Vec2 from, b2Vec2 to, CastFilter cf = {});
	
	/// Returns nearest object hit (central ray is preffered)
	std::optional<RaycastResult> raycast_nearest(b2Vec2 from, b2Vec2 to, CastFilter cf = {}, std::optional<float> width = {});
	
	/// Calls function for all objects inside circle
	void query_circle_all(b2Vec2 ctr, float radius, QueryCbRet narrow, OptQueryCbRet wide = nullptr);
	
	/// Calls function for all objects inside circle
	void query_circle_all(b2Vec2 ctr, float radius, QueryCb narrow, OptQueryCbRet wide = nullptr);
	
	/// Appends result - all objects inside circle
	void circle_cast_all(std::vector<CastResult>& es, b2Vec2 ctr, float radius, CastFilter cf = {});
	
	/// Appends result - objects inside circle which are nearest to center
	void circle_cast_nearest(std::vector<RaycastResult>& es, b2Vec2 ctr, float radius, CastFilter cf = {});
	
	/// Returns non-sensor object in which point lays
	std::optional<PointResult> point_cast(b2Vec2 ctr, float radius, CastFilter cf = {});
	
	/// Calls function for all objects inside rectangle
	void query_aabb(Rectfp area, QueryCbRet f);
	
	/// Calls function for all objects inside rectangle
	void query_aabb(Rectfp area, QueryCb f);
};

#endif // PHYSICS_HPP
