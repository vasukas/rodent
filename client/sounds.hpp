#ifndef SOUND2_HPP
#define SOUND2_HPP

#include "game/entity.hpp"

class b2ChainShape;
class GameCore;
class Entity;



#define SOUND_ID_X_LIST\
	X(SND_NONE)\
	X(SND_VOLUME_TEST_QUIET)\
	X(SND_VOLUME_TEST_LOUD)\
	\
	X(SND_WPN_MINIGUN)\
	X(SND_WPN_SHOTGUN)\
	X(SND_WPN_MINIGUN_TURRET)\
	\
	X(SND_WPN_ROCKET)\
	\
	X(SND_WPN_BOLTER_CHARGE)\
	X(SND_WPN_BOLTER_DISCHARGE)\
	X(SND_WPN_BOLTER_FAIL)\
	X(SND_WPN_BOLTER_ELECTRO)\
	\
	X(SND_WPN_FIRE_SHOOT)\
	X(SND_WPN_FIRE_LOOP)\
	X(SND_WPN_FOAM_SHOOT)\
	X(SND_WPN_FOAM_AMBIENT)\
	\
	X(SND_WPN_RIFLE)\
	X(SND_WPN_GRENADE_SHOT)\
	\
	X(SND_WPN_UBER_RAY)\
	X(SND_WPN_UBER_LAUNCH)\
	X(SND_WPN_UBER_AMBIENT)\
	\
	X(SND_WPN_BARRAGE)\
	X(SND_WPN_SMG)\
	\
	X(SND_ENV_LIGHTNING)\
	X(SND_ENV_BULLET_HIT)\
	X(SND_ENV_BIG_SHIELD_HIT)\
	X(SND_ENV_EXPLOSION)\
	X(SND_ENV_LARGE_EXPLOSION)\
	X(SND_ENV_BOT_EXPLOSION)\
	X(SND_ENV_RAM)\
	X(SND_ENV_WALLRAM)\
	X(SND_ENV_DRILLING)\
	X(SND_ENV_UNLOADING)\
	\
	X(SND_AUTOLOCK_PING)\
	X(SND_BOT_LOCK_FIX)\
	X(SND_OBJ_DISPENSER)\
	X(SND_OBJ_DISPENSER_EMPTY)\
	X(SND_OBJ_DISPENSER_WAIT)\
	X(SND_OBJ_MINIDOCK)\
	X(SND_OBJ_SPAWN)\
	X(SND_OBJ_TELEPORT_ACTIVATE)\
	X(SND_OBJ_TELEPORT)\
	X(SND_OBJ_DRILL_FIZZLE)\
	X(SND_OBJ_TERMINAL_FAIL)\
	X(SND_OBJ_TERMINAL_OK)\
	X(SND_OBJ_DOOR_OPEN)\
	X(SND_OBJ_DOOR_CLOSE)\
	\
	X(SND_OBJAMB_FINALTERM_WORK)\
	X(SND_OBJAMB_SCITERM)\
	X(SND_OBJAMB_CRYOPOD)\
	X(SND_OBJAMB_CONVEYOR)\
	\
	X(SND_UI_NO_AMMO)\
	X(SND_UI_SHIELD_READY)\
	X(SND_UI_PICKUP)\
	X(SND_UI_PICKUP_POWER)\
	\
	X(SND_TOTAL_COUNT_INTERNAL)

enum SoundId
{
#define X(A) A,
SOUND_ID_X_LIST
#undef X
};
const char *get_name(SoundId id);



struct SoundPlayParams
{
	SoundId id;
	std::optional<vec2fp> pos = {};
	EntityIndex target = {};
	std::optional<float> t = {};
	TimeSpan loop_period = {};
	float volume = 1; ///< Limited at [0; 1]
	
	SoundPlayParams() = default;
	SoundPlayParams& _pos(vec2fp pos);
	SoundPlayParams& _target(EntityIndex target);
	SoundPlayParams& _t(float t);
	SoundPlayParams& _period(TimeSpan t);
	SoundPlayParams& _volume(float t);
};



/// Controls looping sounds
struct SoundObj
{
	struct Id {
		using T = uint16_t;
		T i = std::numeric_limits<T>::max();
		Id() = default;
		explicit Id(int i): i(i) {}
		explicit operator bool() const {return i != std::numeric_limits<T>::max();}
	};
	
	SoundObj() = default;
	SoundObj(SoundObj&&) noexcept;
	SoundObj& operator=(SoundObj&&) noexcept;
	~SoundObj() {stop();}
	
	void update(const SoundPlayParams& pars);
	void update(Entity& ent, SoundPlayParams pars); ///< Autosets pos and index
	void stop();
	
private:
	friend class SoundEngine_Impl;
	Id id = {};
};



class SoundEngine
{
public:
	bool debug_draw = false;
	
	static bool init();
	static SoundEngine* get();
	virtual ~SoundEngine();
	
	static int check_unused_sounds();
	
	static void once(const SoundPlayParams& pars) {
		if (auto p = get()) p->play({}, pars, false);}
	static void once(SoundId id, std::optional<vec2fp> pos) {
		if (auto p = get()) p->play({}, {id, pos}, false);}

	virtual void geom_static_add(const b2ChainShape& shp) = 0;
	virtual void geom_static_clear() = 0;
	
	virtual float get_master_vol() = 0;
	virtual void set_master_vol(float vol) = 0;
	
	virtual float get_sfx_vol() = 0;
	virtual void set_sfx_vol(float vol) = 0;
	
	virtual float get_music_vol() = 0;
	virtual void set_music_vol(float vol) = 0;
	
	/// Blocks if called immediatly again. 
	/// Use nullptr to disable music. 
	/// Disables automatic control, resets UI pause (for music only).
	virtual void music(const char *name, int subtrack_index = -1, bool loop = true, bool = true) = 0;
	
	/// Returns true if music currently playing
	virtual bool has_music() = 0;
	
	enum MusControl {
		MUSC_NO_AUTO, ///< No automatic control (keeps last track playing)
		MUSC_AMBIENT,
		MUSC_LIGHT,
		MUSC_HEAVY,
		MUSC_EPIC
	};
	virtual void music_control(MusControl state) = 0; ///< Update dynamic music control state
	
	virtual void sync(GameCore& core, vec2fp lstr_pos) = 0; ///< Must be called when game logic thread is locked
	virtual void set_pause(bool is_paused) = 0; ///< Stops music and all non-UI sounds
	
protected:
	friend SoundObj;
	virtual SoundObj::Id play(SoundObj::Id obj_id, const SoundPlayParams& pars, bool continious) = 0;
	virtual void stop(SoundObj::Id id, bool = true) = 0;
};

#endif // SOUND2_HPP
