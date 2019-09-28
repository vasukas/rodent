#ifndef WEAPON_ALL_HPP
#define WEAPON_ALL_HPP

#include "client/presenter.hpp"
#include "physics.hpp"
#include "weapon.hpp"



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
	};
	
	static PhysicsWorld::CastFilter make_cf(EntityIndex src);
	static void explode(size_t src_team, EntityIndex src_eid, b2Vec2 self_vel, PhysicsWorld::RaycastResult hit, const Params& pars);
	
	StdProjectile(Entity* ent, const Params& pars, EntityIndex src, std::optional<vec2fp> target);
	
private:
	Params pars;
	EntityIndex src;
	std::optional<vec2fp> target;
	
	void step() override;
};



class ProjectileEntity : public Entity
{
public:
	ProjectileEntity(vec2fp pos, vec2fp vel, std::optional<vec2fp> target, Entity* src, const StdProjectile::Params& pars, ModelType model, FColor clr)
	    :
	    phy(this, Transform{pos, vel.angle()}, Transform{vel}),
	    ren(this, model, clr),
	    proj(this, pars, src? src->index : EntityIndex{}, target),
	    team(src? src->get_team() : TEAM_ENVIRON)
	{}
	
private:
	EC_VirtualBody phy;
	EC_RenderSimple ren;
	StdProjectile proj;
	size_t team;
	
	ECompPhysics& get_phy() override {return phy;}
	ECompRender* get_ren()  override {return &ren;}
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
		std::vector <Entity*> prev = {};
		
		bool ignore(Entity* ent);
	};
	struct RenComp : ECompRender
	{
		struct Line {
			vec2fp a, b;
			float r;
		};
		std::vector<Line> ls;
		float t = 1, tps;
		
		RenComp(Entity* ent, std::vector<Line> ls, TimeSpan lt);
		void step() override;
	};
	
	static bool generate(vec2fp pos, SrcParams src, std::optional<vec2fp> dir_lim, bool is_first);
	
private:
	TimeSpan left = TimeSpan::seconds(0.3);
	EC_VirtualBody phy;
	RenComp ren;
	SrcParams src;
	std::optional<vec2fp> dir_lim;
	
	static std::vector<RenComp::Line> gen_ls(vec2fp a, vec2fp b, bool add);
	
	ElectroCharge(vec2fp pos, SrcParams src, std::vector<RenComp::Line> ls, std::optional<vec2fp> dir_lim)
	    : phy(this, Transform{pos}), ren(this, std::move(ls), left), src(src), dir_lim(dir_lim)
	{reg();}
	ECompPhysics& get_phy() override {return phy;}
	ECompRender* get_ren() override {return &ren;}
	void step() override;
};



class FoamProjectile : public Entity
{
public:
	static bool can_create(vec2fp pos, EntityIndex src_i);
	FoamProjectile(vec2fp pos, vec2fp vel, size_t team, EntityIndex src_i, bool is_first);
	
private:
	EVS_SUBSCR;
	EC_Physics phy;
	EC_RenderSimple ren;
	EC_Health hlc;
	size_t team;
	
	TimeSpan left;
	bool frozen = false;
	float min_spd; // squared
	bool is_first;
	EntityIndex src_i;
	
	ECompPhysics& get_phy() override {return phy;}
	ECompRender*  get_ren() override {return &ren;}
	EC_Health*    get_hlc() override {return &hlc;}
	size_t get_team() const override {return team;}
	
	void step() override;
	void on_event(const CollisionEvent& ev);
	void freeze();
};



class GrenadeProjectile : public Entity
{
public:
	GrenadeProjectile(vec2fp pos, vec2fp dir, size_t team);
	
private:
	EVS_SUBSCR;
	EC_Physics phy;
	EC_RenderSimple ren;
	EC_Health hlc;
	size_t team;
	
	TimeSpan left;
	float clr_t = 0;
	
	ECompPhysics& get_phy() override {return phy;}
	ECompRender*  get_ren() override {return &ren;}
	EC_Health*    get_hlc() override {return &hlc;}
	size_t get_team() const override {return team;}
	
	void step() override;
	void on_event(const CollisionEvent& ev);
	void explode();
};



class WpnMinigun : public Weapon
{
public:
	WpnMinigun();
	
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



class WpnElectro : public Weapon
{
public:
	WpnElectro();
	
private:
	float charge_lvl = 0;
	TimeSpan charge_tmo;
	
	std::optional<ShootResult> shoot(ShootParams pars) override;
};



class WpnFoam : public Weapon
{
public:
	WpnFoam();
	
private:
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

#endif // WEAPON_ALL_HPP
