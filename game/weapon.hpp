#ifndef WEAPON_HPP
#define WEAPON_HPP

#include "client/resbase.hpp"
#include "client/sounds.hpp"
#include "vaslib/vas_time.hpp"
#include "damage.hpp"

struct EC_Equipment;

enum class AmmoType
{
	None,
	Bullet,
	Rocket,
	Energy,
	FoamCell,
	
	TOTAL_COUNT
};

ModelType ammo_model(AmmoType type);
const char *ammo_name(AmmoType type);



class Weapon
{
public:
	struct Info
	{
		std::string name;
		ModelType model = MODEL_NONE;
		std::optional<int> hand; ///< Overrides equip setting
		
		AmmoType ammo = AmmoType::None;
		std::optional<TimeSpan> def_delay; ///< Default delay after shot
		std::optional<int> def_ammo; ///< Default ammo consumption per shot
		std::optional<float> def_heat; ///< Default heat increase PER SECOND
		
		float bullet_speed = 1.f; ///< Average, meters per second. For AI prediction only
		vec2fp bullet_offset = {}; ///< For projectile generation, from weapon origin
		float angle_limit = deg_to_rad(30); ///< Max angle between target and facing direction
		
		void set_origin_from_model(); ///< Sets bullet_offset
	};
	
	struct Overheat
	{
		float thr_off = 0.5; ///< Overheating disabled when lower
		float v_decr = 0.3; ///< Overheat decrease when not overheated (per second)
		float v_cool = 0.5; ///< Overheat decrease per second of cooldown (per second)
		
		float value = 0.f;
		bool flag = false; ///< Is overheated
		
		bool is_ok() const {return !flag;}
		void shoot(float amount);
		void cool();
	};
	
	struct UI_Info
	{
		std::optional<float> charge_t; ///< Current shot charge level, [0, 1]
	};
	
	const Info* const info;
	std::optional<Overheat> overheat; ///< Controlled by equip
	
	EC_Equipment* equip = nullptr; ///< By which owned, never null
	
	
	
	Weapon(const Info* info) : info(info) {}
	virtual ~Weapon() = default;
	
	
	
	struct ShootParams
	{
		vec2fp target; ///< World coords
		bool main, main_was; ///< Main fire button state (current and previous)
		bool alt, alt_was; ///< Alt fire button state (current and previous)
		bool is_ok; ///< Set if weapon can shoot (check only if previous button states are used)
	};
	
	struct ShootResult
	{
		std::optional<int> ammo = {}; ///< Ammo spent
		std::optional<TimeSpan> delay = {}; ///< Time before next shot
		std::optional<float> heat = {}; ///< Heat increase (ignored if oveheat nor present)
	};
	
	/// If returns nothing, no shot was made. 
	/// Called if one or more of fire button flags are set
	virtual std::optional<ShootResult> shoot(ShootParams pars) = 0;

	/// Should return true if not shooting, but somehow preparing to. Called if shoot() fails
	virtual bool is_preparing() {return false;}
	
	virtual std::optional<UI_Info> get_ui_info() {return {};} ///< Should return always, even if no info available
	std::optional<TimeSpan> get_reload_timeout() const {if (rof_left.is_positive()) return rof_left; return {};}
	
	struct DirectionResult
	{
		vec2fp origin; ///< Calculated bullet origin
		vec2fp dir; ///< Normalized
	};
	enum DirectionType
	{
		DIRTYPE_TARGET,
		DIRTYPE_IGNORE_ANGLE, ///< Ignore angle limitation (always succeeds)
		DIRTYPE_FORWARD_FAIL ///< Use target if in limits, otherwise returns forward direction (always succeeds)
	};
	std::optional<DirectionResult> get_direction(const ShootParams& pars, DirectionType dtype = DIRTYPE_TARGET);
	
	void sound(SoundId id, std::optional<TimeSpan> period = {}, std::optional<float> t = {});
	
private:
	friend EC_Equipment;
	TimeSpan rof_left;
};



struct WeaponMsgReport
{
	enum JustError
	{
		ERR_SELECT_OVERHEAT,
		ERR_SELECT_NOAMMO,
		ERR_NO_TARGET
	};
	
	virtual void jerr(JustError err) = 0;
	virtual void no_ammo(int required) = 0;
	virtual void reset() {}
	virtual ~WeaponMsgReport() = default;
};



struct EC_Equipment : EComp
{
	struct Ammo
	{
		int value = 0, max = 1;
		
		float add(int amount); ///< Or subtract. Returns actual amount added/subtracted
		bool has(int amount) {return value >= amount;}
		bool has(Weapon& w) {return w.info->def_ammo ? has(*w.info->def_ammo) : true;}
	};
	
	std::optional<int> hand = 0; // 1 right, 0 center, -1 left. If not set, weapons not shown
	
	/// If true, ammo not consumed for any weapon
	bool infinite_ammo = true;
	
	/// If true, never overheats
	bool no_overheat = false;
	
	/// If set, error messages sent to it
	WeaponMsgReport* msgrep = nullptr;
	
	/// For continiously shooting weapons
	SoundObj snd_loop;
	TimeSpan snd_keep_until;
	
	
	
	EC_Equipment(Entity& ent);
	
	/// Sets with which weapon shoot at the end of the step
	void shoot(vec2fp target, bool main, bool alt);
	
	/// Returns true if shot on previous step
	bool did_shot() {return did_shot_flag;}
	
	bool set_wpn(size_t index, bool even_if_no_ammo = false); ///< Returns false if can't be set
	size_t wpn_index();
	
	Weapon& get_wpn(); ///< Returns current weapon
	void add_wpn(std::unique_ptr<Weapon> wpn);
	void replace_wpn(size_t index, std::unique_ptr<Weapon> new_wpn); ///< Meant for debug
	auto& raw_wpns() {return wpns;} ///< Do NOT erase anything
	
	Ammo& get_ammo(AmmoType type) {return ammos[static_cast<size_t>(type)];}
	bool has_ammo(Weapon& w, std::optional<int> amount = {}); ///< For use by Weapon
	
	vec2fp get_attachment_offset();
	
private:
	std::vector<std::unique_ptr<Weapon>> wpns; // no nulls
	std::array<Ammo, static_cast<size_t>(AmmoType::TOTAL_COUNT)> ammos;
	size_t wpn_cur = size_t_inval;
	std::optional<size_t> last_req; ///< change request
	std::optional<size_t> w_prev;
	Weapon::ShootParams pars = {};
	bool did_shot_flag = false;
	
	bool shoot_internal(Weapon& wpn, Weapon::ShootParams pars);
	int shoot_check(Weapon& wpn); ///< Checks based on default values
	
	friend class GameCore_Impl;
	void step();
};

#endif // WEAPON_HPP
