#ifndef WEAPON_HPP
#define WEAPON_HPP

#include "client/resbase.hpp"
#include "vaslib/vas_time.hpp"
#include "damage.hpp"

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
		float angle_limit = deg_to_rad(30); ///< Max angle between target and facing direction (DISABLED)
		
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
	
	virtual std::optional<UI_Info> get_ui_info() {return {};} ///< Should return always, even if no info available
	std::optional<TimeSpan> get_reload_timeout() const {if (rof_left.is_positive()) return rof_left; return {};}
	
	struct DirectionResult
	{
		vec2fp origin; ///< Calculated bullet origin
		vec2fp dir; ///< Normalized
	};
	/// If 'ignore_target' is true, always returns (as it ignores angle limitation)
	std::optional<DirectionResult> get_direction(const ShootParams& pars, bool ignore_target = false);
	
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
	
	int hand = 0; // 1 right, 0 center, -1 left
	
	/// If true, ammo not consumed for any weapon
	bool infinite_ammo = true;
	
	/// If set, error messages sent to it
	WeaponMsgReport* msgrep = nullptr;
	
	
	
	EC_Equipment(Entity* ent);
	
	/// Sets with which weapon shoot at the end of the step
	void shoot(vec2fp target, bool main, bool alt);
	
	bool set_wpn(size_t index); ///< Returns false if can't be set
	size_t wpn_index();
	
	Weapon& get_wpn(); ///< Returns current weapon
	void add_wpn(Weapon* wpn); ///< Assumes ownership
	auto& raw_wpns() {return wpns;} ///< Do NOT erase anything
	
	Ammo& get_ammo(AmmoType type) {return ammos[static_cast<size_t>(type)];}
	bool has_ammo(Weapon& w, std::optional<int> amount = {}); ///< For use by Weapon
	
private:
	std::vector<std::unique_ptr<Weapon>> wpns; // no nulls
	std::array<Ammo, static_cast<size_t>(AmmoType::TOTAL_COUNT)> ammos;
	size_t wpn_cur = size_t_inval;
	std::optional<size_t> last_req; ///< change request
	std::optional<size_t> w_prev;
	Weapon::ShootParams pars = {};
	
	bool shoot_internal(Weapon& wpn, Weapon::ShootParams pars);
	bool shoot_check(Weapon& wpn); ///< Checks based on default values
	void step() override;
};

#endif // WEAPON_HPP
