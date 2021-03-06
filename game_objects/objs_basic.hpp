#ifndef OBJS_BASIC_HPP
#define OBJS_BASIC_HPP

#include <variant>
#include "client/ec_render.hpp"
#include "client/sounds.hpp"
#include "game/physics.hpp"
#include "game/weapon.hpp"



class EWall final : public Entity
{
	EC_Physics phy;
	
public:
	EWall(GameCore& core, const std::vector<std::vector<vec2fp>>& walls);
	EC_Position& ref_pc() override {return phy;}
};



class EPhyBox final : public Entity
{
	EC_Physics phy;
	EC_Health hlc;
	
public:
	EPhyBox(GameCore& core, vec2fp at);
	
	EC_Position& ref_pc()  override {return phy;}
	EC_Health*   get_hlc() override {return &hlc;}
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
	
	struct SecurityKey {};
	
	using Value = std::variant<AmmoPack, ArmorShard, SecurityKey>;
	
	
	
	static AmmoPack rnd_ammo(GameCore& core);
	static AmmoPack std_ammo(AmmoType type);
	
	static void create_death_drop(GameCore& core, vec2fp pos, float item_value_k);
	
	EPickable(GameCore& core, vec2fp pos, Value val);
	EC_Position& ref_pc() override {return phy;}
	
private:
	EVS_SUBSCR;
	EC_Physics phy;
	Value val;
	
	void on_cnt(const CollisionEvent& ce);
};



class EDoor final : public Entity
{
	// logic
	static constexpr float sens_width = 1; // relative to cell size
	static constexpr TimeSpan wait_time = TimeSpan::seconds(0.6); // before (re-)opening
	static constexpr TimeSpan anim_time = TimeSpan::seconds(0.5); // opening/closing time
	static constexpr TimeSpan keep_time = TimeSpan::seconds(7); // kept open
	static constexpr TimeSpan keep_time_plr = TimeSpan::seconds(1); // kept open, plr_only
	// graphics
	static constexpr float width = 0.5; // relative to cell size
	static constexpr float offset = 0.1; // relative to cell size
	
	
	EC_Physics phy;
	SwitchableFixture fix;
	
	bool plr_only;
	
	enum State {
		ST_OPEN,
		ST_CLOSED,
		ST_TO_OPEN,
		ST_TO_CLOSE
	};
	State state = ST_CLOSED;
	TimeSpan tm_left;
	size_t num_cnt = 0;
	
	
	EVS_SUBSCR;
	void on_cnt(const CollisionEvent& ce);
	void open();
	
	void step() override; // only while open
	
public:
	struct Init
	{
		b2BodyDef def; // static
		vec2fp fix_he;
		vec2fp sens_he;
		bool is_x_ext;
		bool plr_only;
	};
	
	EDoor(GameCore& core, vec2i TL_origin, vec2i door_ext, vec2i room_dir, bool plr_only = false);
	EDoor(GameCore& core, Init init);
	EC_Position& ref_pc() override {return phy;}
};



class EInteractive : public Entity
{
public:
	EInteractive(GameCore& core): Entity(core) {}
	virtual std::pair<bool, std::string> use_string() = 0; ///< Returns if can be used and description
	virtual void use(Entity* by) = 0; ///< Currently assumed it's always player. Called outside of physics step
};



class EFinalTerminal final : public EInteractive
{
	EC_Physics phy;
	SoundObj snd;
	void step() override;
	
public:
	EFinalTerminal(GameCore& core, vec2fp at);
	EC_Position& ref_pc() override {return phy;}
	
	std::pair<bool, std::string> use_string() override;
	void use(Entity* by) override;
};



class EDispenser final : public EInteractive
{
	EC_Physics phy;
	
	TimeSpan usable_after;
	vec2fp gen_at;
	bool increased;
	
	size_t left;
	
public:	
	EDispenser(GameCore& core, vec2fp at, float rot, bool increased_amount);
	EC_Position& ref_pc() override {return phy;}
	
	std::pair<bool, std::string> use_string() override;
	void use(Entity* by) override;
};



class ETeleport final : public EInteractive
{
	EC_Physics phy;
	bool activated = false;
	
	EVS_SUBSCR;
	void on_cnt(const CollisionEvent& ev);
	
public:
	ETeleport(GameCore& core, vec2fp at);
	EC_Position& ref_pc() override {return phy;}
	
	std::pair<bool, std::string> use_string() override;
	void use(Entity* by) override;
	
	void activate(bool menu);
	void teleport_player();
};



class EMinidock final : public Entity
{
	EC_Physics phy;
	
	Entity* plr = nullptr;
	TimeSpan usable_after;
	float charge = 1;
	
	EVS_SUBSCR;
	void on_cnt(const CollisionEvent& ev);
	void step() override;
	
public:
	EMinidock(GameCore& core, vec2fp at, float rot);
	EC_Position& ref_pc() override {return phy;}
	
	Entity* get_target() const {return plr;}
};



class EStorageBox final : public Entity
{
	EC_Physics phy;
	EC_Health  hlc;
public:
	EStorageBox(GameCore& core, vec2fp at);
	~EStorageBox();
	EC_Position& ref_pc() override {return phy;}
	EC_Health* get_hlc() override {return &hlc;}
};



class EMiningDrill final : public EInteractive
{
	EC_Physics phy;
	EC_Equipment eqp;
	TimeSpan left;
	int stage = 0;
	void step() override;
public:
	EMiningDrill(GameCore& core, vec2fp at, float rot);
	EC_Position&  ref_pc()  override {return phy;}
	EC_Equipment* get_eqp() override {return &eqp;}
	bool is_creature() override {return false;}
	
	std::pair<bool, std::string> use_string() override {return {true, {}};}
	void use(Entity* by) override;
};



class EDecor : public Entity
{
protected:
	EC_Physics phy;
public:
	SoundObj snd; ///< Not used internally
	/// 'ui_name' is NOT copied
	EDecor(GameCore& core, const char *ui_name, Rect at, float rot, ModelType model, FColor clr = FColor(0.7, 0.7, 0.7));
	EC_Position& ref_pc() override {return phy;}
};



class EDecorDestructible : public EDecor
{
protected:
	EC_Health  hlc;
public:
	/// 'ui_name' is NOT copied
	EDecorDestructible(GameCore& core, const char *ui_name, int hp_amount,
	                   Rect at, float rot, ModelType model, FColor clr = FColor(0.7, 0.7, 0.7));
	EC_Health* get_hlc() override {return &hlc;}
};



class EAssembler final : public EDecorDestructible
{
public:
	EAssembler(GameCore& core, vec2i at, float rot);
	~EAssembler();
	size_t get_team() const override {return TEAM_BOTS;}
};



class EDecorGhost final : public Entity
{
	EC_VirtualBody phy;
public:
	EDecorGhost(GameCore& core, Transform at, ModelType model, FColor clr = FColor(0.3, 0.3, 0.3));
	EC_Position& ref_pc() override {return phy;}
};



class ETutorialDummy final : public Entity
{
	EVS_SUBSCR;
	EC_Physics phy;
	EC_Health  hlc;
	void on_dmg(const DamageQuant& q);
public:
	ETutorialDummy(GameCore& core, vec2fp pos);
	EC_Position& ref_pc()  override {return phy;}
	EC_Health*   get_hlc() override {return &hlc;}
	size_t get_team() const override {return TEAM_BOTS;}
};



class ERespawnFunc final : public Entity
{
	EC_VirtualBody phy;
	EntityIndex child;
	TimeSpan tmo;
	void step() override;
	
public:
	std::function<Entity*()> f;
	TimeSpan period = TimeSpan::seconds(8);
	
	ERespawnFunc(GameCore& core, vec2fp pos, std::function<Entity*()> f, TimeSpan initial_delay = {});
	EC_Position& ref_pc() override {return phy;}
};

#endif // OBJS_BASIC_HPP
