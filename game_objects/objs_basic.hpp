#ifndef OBJS_BASIC_HPP
#define OBJS_BASIC_HPP

#include "client/presenter.hpp"
#include "game/physics.hpp"
#include "game/weapon.hpp"



class EWall final : public Entity
{
	EC_Physics phy;
	EC_RenderSimple ren;
	
public:
	EWall(const std::vector<std::vector<vec2fp>>& walls);
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
};



class EPhyBox final : public Entity
{
	EC_Physics phy;
	EC_RenderSimple ren;
	EC_Health hlc;
	
public:
	EPhyBox(vec2fp at);
	std::string ui_descr() const override {return "Box";}
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
	EC_Health*    get_hlc() override {return &hlc;}
};



/// Works only on player
class EPickable final : public Entity
{
public:
	struct AmmoPack
	{
		AmmoType type;
		int amount;
	};
	
	struct ArmorShard
	{
		int amount;
	};
	
	struct Func
	{
		/// Returns true if pickable should be destroyed
		std::function<bool(Entity&)> f;
		ModelType model;
		FColor clr = FColor(1, 1, 1);
		std::string ui_name = {};
	};
	
	using Value = std::variant<AmmoPack, ArmorShard, Func>;
	
	
	
	static AmmoPack rnd_ammo();
	static AmmoPack std_ammo(AmmoType type);
	static void death_drop(vec2fp pos, float value);
	
	EPickable(vec2fp pos, Value val);
	std::string ui_descr() const override;
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
	
private:
	EC_Physics phy;
	EC_RenderSimple ren;
	Value val;
	
	EVS_SUBSCR;
	void on_cnt(const CollisionEvent& ce);
};



class EDoor final : public Entity
{
	struct RenDoor : ECompRender {
		RenDoor(Entity* ent);
		void step() override;
	};
	
	struct FI_Sensor : FixtureInfo {};
	
	enum State {
		ST_OPEN,
		ST_CLOSED,
		ST_TO_OPEN,
		ST_TO_CLOSE
	};
	
	// logic
	static constexpr float sens_width = 1; // relative to cell size
	static constexpr TimeSpan wait_time = TimeSpan::seconds(0.3); // before opening
	static constexpr TimeSpan anim_time = TimeSpan::seconds(0.5); // opening/closing time
	static constexpr TimeSpan keep_time = TimeSpan::seconds(7); // kept open
	static constexpr TimeSpan keep_time_plr = TimeSpan::seconds(1); // kept open, plr_only
	// graphics
	static constexpr float width = 0.5; // relative to cell size
	static constexpr float offset = 0.1; // relative to cell size
	static constexpr float min_off = 0.2; // meters; length in 'closed' state
	
	EC_Physics phy;
	RenDoor ren;
	bool plr_only;
	
	TimeSpan tm_left;
	State state = ST_CLOSED;
	size_t num_cnt = 0;
	
	bool is_x_ext;
	vec2fp fix_he;
	b2Fixture* fix = nullptr;
	
	EVS_SUBSCR;
	void on_cnt(const CollisionEvent& ce);
	void open();
	
	void step() override; // only while open
	void upd_fix();
	
public:
	EDoor(vec2i TL_origin, vec2i door_ext, vec2i room_dir, bool plr_only = false);
	std::string ui_descr() const override {return "Door";}
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
};



class EInteractive : public Entity
{
public:
	virtual std::pair<bool, std::string> use_string() = 0; ///< Returns if can be used and description
	virtual void use(Entity* by) = 0; ///< Currently assumed it's always player. Called outside of physics step
};



class EFinalTerminal final : public EInteractive
{
	EC_Physics phy;
	EC_RenderSimple ren;
	
	void step() override;
	
public:
	bool enabled = false;
	bool is_activated = false;
	TimeSpan timer_end = {};
	
	EFinalTerminal(vec2fp at);
	std::string ui_descr() const override {return "Control terminal";}
	
	std::pair<bool, std::string> use_string() override;
	void use(Entity* by) override;
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
};



class EDispenser final : public EInteractive
{
	EC_Physics phy;
	EC_RenderSimple ren;
	
	TimeSpan usable_after;
	vec2fp gen_at;
	bool increased;
	
	size_t left;
	
public:
	EDispenser(vec2fp at, float rot, bool increased_amount);
	std::string ui_descr() const override {return "Dispenser";}
	
	std::pair<bool, std::string> use_string() override;
	void use(Entity* by) override;
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
};



class EMinidock final : public Entity
{
	EC_Physics phy;
	EC_RenderSimple ren;
	
	Entity* plr = nullptr;
	TimeSpan usable_after;
	float charge = 1;
	
	EVS_SUBSCR;
	void on_cnt(const CollisionEvent& ev);
	void step() override;
	
public:
	EMinidock(vec2fp at, float rot);
	std::string ui_descr() const override {return "Minidoc";}
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
	
	Entity* get_target() const {return plr;}
};



class EDecor final : public Entity
{
	EC_Physics phy;
	EC_RenderSimple ren;
	const char *ui_name;
	
public:
	/// 'ui_name' is NOT copied
	EDecor(const char *ui_name, Rect at, float rot, ModelType model, FColor clr = FColor(0.7, 0.7, 0.7));
	std::string ui_descr() const override {return ui_name;}
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
};



class EDecorGhost final : public Entity
{
	EC_VirtualBody phy;
	EC_RenderSimple ren;
	
public:
	EDecorGhost(Transform at, ModelType model, FColor clr = FColor(0.3, 0.3, 0.3));
	
	ECompPhysics& get_phy() override {return  phy;}
	ECompRender*  get_ren() override {return &ren;}
};

#endif // OBJS_BASIC_HPP
