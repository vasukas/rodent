#ifndef WEAPON_HPP
#define WEAPON_HPP

#include "client/resbase.hpp"
#include "vaslib/vas_time.hpp"
#include "damage.hpp"



enum class WeaponIndex
{
	Bat,
	Handgun,
	Bolter,
	Grenade,
	Minigun,
	Rocket,
	Electro
};

ModelType ammo_model(WeaponIndex wpn);



class Weapon
{
public:
	static Weapon* create_std(WeaponIndex i);
	virtual ~Weapon() = default;
	
	/// Must never fail
	virtual void shoot(Entity* ent, vec2fp target) = 0;
	
	/// Average, meters per second
	virtual float get_bullet_speed() const = 0;
	
	/// Returns true if can shoot right now
	bool is_ready();
	
	
	
	struct RenInfo
	{
		std::string name;
		ModelType model;
	};
	
	struct ModRof
	{
		TimeSpan delay, wait;
		
		ModRof(TimeSpan delay): delay(delay) {}
		bool ok() const {return !wait.is_positive();}
		void shoot() {wait = delay;}
	};
	
	struct ModAmmo
	{
		WeaponIndex type;
		float per_shot, max, cur;
		
		ModAmmo(WeaponIndex type, float per_shot = 1, float max = 100, float cur = 0):
		    type(type), per_shot(per_shot), max(max), cur(cur) {}
		
		bool ok() const {return cur != 0.f;}
		void shoot();
		bool add(float amount); ///< Returns false if already at max
	};
	
	struct ModOverheat
	{
		float shots_per_second = 1;
		float thr_off = 0.5; ///< Overheating disabled when lower
		float v_incr = 0.3; ///< Overheat per second of shooting/normal cooling
		float v_cool = 0.5; ///< Overheat decrease per second of cooldown
		
		float value = 0.f;
		bool flag = false; ///< Is overheated
		
		bool ok() const {return !flag;}
		void shoot();
		void cool();
	};
	
	virtual RenInfo get_reninfo() const = 0;
	
	// Note: must be stepped outside
	virtual ModRof* get_rof() {return nullptr;}
	virtual ModAmmo* get_ammo() {return nullptr;}
	virtual ModOverheat* get_heat() {return nullptr;}
	
protected:
	/// Checks internal conditions, called only by is_ready()
	virtual bool is_ready_internal() {return true;}
};



struct EC_Equipment : EComp
{
	std::vector<std::unique_ptr<Weapon>> wpns;
	int hand = 1; // 1 right, 0 center, -1 left
	
	/// If true, ammo not consumed for any weapon
	bool infinite_ammo = false;
	
	
	
	EC_Equipment(Entity* ent);
	~EC_Equipment();
	void step() override;
	
	/// Returns false if not possible atm
	bool shoot(vec2fp target);
	
	bool set_wpn(size_t index); ///< Returns false if can't
	size_t wpn_index() {return wpn_cur;} ///< size_t_inval if none
	
	Weapon* wpn_ptr(); ///< Returns current weapon (or nullptr if none)
	Weapon& get_wpn(); ///< Returns current weapon (throws if none)
	
private:
	size_t wpn_cur = size_t_inval;
	std::optional<size_t> last_req;
	bool has_shot = false;
};

#endif // WEAPON_HPP
