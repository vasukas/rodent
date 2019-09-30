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
	
	TOTAL_COUNT
};

ModelType ammo_model(AmmoType type);



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
		std::optional<float> def_ammo; ///< Default ammo consumption per shot
		std::optional<float> def_heat; ///< Default heat increase PER SECOND
		
		float bullet_speed = 1.f; ///< Average, meters per second. For AI prediction only
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
		std::optional<float> ammo = {}; ///< Ammo spent
		std::optional<TimeSpan> delay = {}; ///< Time before next shot
		std::optional<float> heat = {}; ///< Heat increase (ignored if oveheat nor present)
	};
	
	/// If returns nothing, no shot was made. 
	/// Called if one or more of fire button flags are set
	virtual std::optional<ShootResult> shoot(ShootParams pars) = 0;
	
	/// Checks internal conditions. 
	/// Default checks default ammo
	virtual bool is_ready();
	
	virtual std::optional<UI_Info> get_ui_info() {return {};}
	std::optional<TimeSpan> get_reload_timeout() const {if (rof_left.is_positive()) return rof_left; return {};}
	
private:
	friend EC_Equipment;
	TimeSpan rof_left;
};



struct EC_Equipment : EComp
{
	struct Ammo
	{
		float value = 0, max = 1;
		
		bool add(float amount); ///< Or subtract. Returns false if already at max
		bool has(float amount) {return value >= amount;}
		bool has(Weapon& w) {return w.info->def_ammo ? has(*w.info->def_ammo) : true;}
	};
	
	int hand = 0; // 1 right, 0 center, -1 left
	
	/// If true, ammo not consumed for any weapon
	bool infinite_ammo = true;
	
	
	
	EC_Equipment(Entity* ent);
	
	/// Uses previous button states, obtained by this functions
	void try_shoot(vec2fp target, bool main, bool alt);
	
	/// Returns false if not possible atm
	bool shoot(Weapon::ShootParams pars);
	
	bool set_wpn(std::optional<size_t> index); ///< Returns false if can't
	std::optional<size_t> wpn_index() {return wpn_cur;}
	
	Weapon* wpn_ptr(); ///< Returns current weapon (or nullptr if none)
	Weapon& get_wpn(); ///< Returns current weapon (throws if none)
	
	void add_wpn(Weapon* wpn); ///< Assumes ownership
	Ammo& get_ammo(AmmoType type) {return ammos[static_cast<size_t>(type)];}
	
	bool has_ammo(Weapon& w, std::optional<float> amount = {}); ///< For use by Weapon
	auto& raw_wpns() {return wpns;}
	
private:
	std::vector<std::unique_ptr<Weapon>> wpns; // no nulls
	std::array<Ammo, static_cast<size_t>(AmmoType::TOTAL_COUNT)> ammos;
	std::optional<size_t> wpn_cur;
	std::optional<size_t> last_req; ///< change request
	bool has_shot = false;
	bool prev_main = false;
	bool prev_alt = false;
	
	void step() override;
};

#endif // WEAPON_HPP
