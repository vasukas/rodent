#ifndef WEAPON_ALL_HPP
#define WEAPON_ALL_HPP

#include "client/ec_render.hpp"
#include "game/physics.hpp"
#include "game/weapon.hpp"



struct StdProjectile : EComp
{
	enum Type
	{
		T_BULLET, ///< Only direct hit
		T_AOE, ///< Explosion
	};
	struct Params
	{
		DamageQuant dq = {DamageType::Kinetic, 0};
		Type type = T_BULLET;
		
		float rad = 1.f; ///< Area-of-effect radius
		float rad_min = 0.3f; ///< Minimal radius in damage calculation
		bool rad_full = false; ///< If true, radius ignored in damage calculation; otherwise linear fall-off is used
		float aoe_max_k = 0.65; ///< Maximum damage modifier for AoE
		float aoe_min_k = 0.2; ///< Minimal damage modifier for AoE
		
		float imp = 0.f; ///< Physical impulse applied to target
		bool trail = false; ///< If true, leaves particle trail
		
		float size = GameConst::hsz_proj; ///< Projectile radius
		bool friendly_fire = false;
	};
	
	/// Note: 'src' currently ignored
	static PhysicsWorld::CastFilter make_cf(EntityIndex src);
	
	/// Velocity can be zero. Hit must contain only valid pos, everything else can be null/zero
	static void explode(GameCore& core, size_t src_team, EntityIndex src_eid,
	                    b2Vec2 self_vel, PhysicsWorld::RaycastResult hit, const Params& pars);
	
	
	
	StdProjectile(Entity& ent, const Params& pars, EntityIndex src, std::optional<vec2fp> target);
	vec2fp hit_location = {}; ///< Set before destroy
	
private:
	Params pars;
	EntityIndex src;
	std::optional<vec2fp> target;
	
	void step() override;
};



class ProjectileEntity : public Entity
{
public:
	ProjectileEntity(GameCore& core, vec2fp pos, vec2fp vel, std::optional<vec2fp> target,
	                 Entity* src, const StdProjectile::Params& pars, ModelType model, FColor clr);
	~ProjectileEntity();
	
private:
	EC_VirtualBody phy;
	StdProjectile proj;
	size_t team;
	
	EC_Position& ref_pc() override {return phy;}
	size_t get_team() const override {return team;}
};



class ElectroCharge : public Entity
{
public:
	static constexpr float radius = 10;
	static constexpr float angle_lim = -0.2;
	static constexpr size_t max_tars = 2;
	static constexpr float damage = 20;
	
	struct SrcParams
	{
		size_t team;
		EntityIndex src_eid;
		int gener = 0; // generation
		std::vector <Entity*> prev = {};
		
		bool ignore(Entity* ent);
	};
	
	static bool generate(GameCore& core, vec2fp pos, SrcParams src, std::optional<vec2fp> dir_lim);
	
private:
	static constexpr int last_generation = 2; // 3 gens
	static constexpr TimeSpan left_init = TimeSpan::seconds(0.3);
	TimeSpan left = left_init;
	EC_VirtualBody phy;
	SrcParams src;
	std::optional<vec2fp> dir_lim;
	
	ElectroCharge(GameCore& core, vec2fp pos, SrcParams src, std::optional<vec2fp> dir_lim);
	EC_Position& ref_pc() override {return phy;}
	void step() override;
};



class FoamProjectile : public Entity
{
public:
	static bool can_create(GameCore& core, vec2fp pos, EntityIndex src_i);
	FoamProjectile(GameCore& core, vec2fp pos, vec2fp vel, size_t team, EntityIndex src_i, bool is_first);
	~FoamProjectile();
	
private:
	EVS_SUBSCR;
	EC_Physics phy;
	EC_Health hlc;
	size_t team;
	
	static constexpr TimeSpan frozen_left = TimeSpan::seconds(0.1);
	TimeSpan left;
	bool frozen = false;
	float min_spd; // squared
	
	bool is_first;
	EntityIndex src_i;
	vec2fp vel_initial;
	
	EC_Position&  ref_pc()  override {return phy;}
	EC_Health*    get_hlc() override {return &hlc;}
	size_t get_team() const override {return team;}
	
	void step() override;
	void on_event(const CollisionEvent& ev);
	void freeze(bool is_normal = true);
};



class GrenadeProjectile : public Entity
{
public:
	GrenadeProjectile(GameCore& core, vec2fp pos, vec2fp dir, EntityIndex src_eid);
	
private:
	EVS_SUBSCR;
	EC_Physics phy;
	EC_Health hlc;
	
	EntityIndex src_eid;
	TimeSpan ignore_tmo;
	TimeSpan left;
	float clr_t = 0;
	
	EC_Position&  ref_pc()  override {return phy;}
	EC_Health*    get_hlc() override {return &hlc;}
	size_t get_team() const override {return TEAM_ENVIRON;}
	
	void step() override;
	void on_event(const CollisionEvent& ev);
	void explode();
};



class ElectroBall : public Entity
{
public:
	ElectroBall(GameCore& core, vec2fp pos, vec2fp dir);
	
private:
	EVS_SUBSCR;
	EC_Physics phy;
	
	EntityIndex target_id;
	TimeSpan tmo_target;
	TimeSpan tmo_ignore = TimeSpan::seconds(1.5);
	
	int explode_left = 10;
	TimeSpan tmo_explode;
	//
	size_t explode_cntc = 0;
	bool explode_close = false;
	
	static constexpr float expl_radius = 3;
	static constexpr float speed_min = 7;
	static constexpr float speed_max = 40;
	
	EC_Position& ref_pc() override {return phy;}
	size_t get_team() const override {return TEAM_ENVIRON;}
	
	void on_cnt(const CollisionEvent& ev);
	void step() override;
};



class WpnMinigun : public Weapon
{
public:
	WpnMinigun();
	
private:
	StdProjectile::Params pp, p2;
	std::optional<ShootResult> shoot(ShootParams pars) override;
};



/// Old minigun
class WpnMinigunTurret : public Weapon
{
public:
	WpnMinigunTurret();
	
private:
	StdProjectile::Params pp;
	std::optional<ShootResult> shoot(ShootParams pars) override;
};



class WpnRocket : public Weapon
{
public:
	WpnRocket();
	
private:
	StdProjectile::Params pp;
	std::optional<ShootResult> shoot(ShootParams pars) override;
};



struct WpnElectro_Pars;

class WpnElectro : public Weapon
{
public:
	static constexpr int shoot_through = 2; // each next weaker by 2
	
	enum Type
	{
		T_PLAYER,
		T_ONESHOT,
		T_WORKER,
		T_CAMPER,
		
		T_TOTAL_COUNT_INTERNAL
	};
	WpnElectro(Type type);
	
private:
	const WpnElectro_Pars& wpr;
	bool ai_alt = false;
	float charge_lvl = 0;
	TimeSpan charge_tmo;
	
	std::optional<ShootResult> shoot(ShootParams pars) override;
	std::optional<UI_Info> get_ui_info() override;
};



class WpnFoam : public Weapon
{
public:
	WpnFoam();
	
private:
	int ammo_skip_count = 0;
	std::optional<ShootResult> shoot(ShootParams pars) override;
};



class WpnRifle : public Weapon
{
public:
	WpnRifle();
	
private:
	StdProjectile::Params pp;
	std::optional<ShootResult> shoot(ShootParams pars) override;
};



class WpnRifleBot : public Weapon
{
public:
	WpnRifleBot();
	
private:
	StdProjectile::Params pp;
	std::optional<ShootResult> shoot(ShootParams pars) override;
};



class WpnUber : public Weapon
{
public:
	WpnUber();
	~WpnUber();
	
private:
	int ammo_skip_count = 0;
	std::optional<ShootResult> shoot(ShootParams pars) override;
};

#endif // WEAPON_ALL_HPP
