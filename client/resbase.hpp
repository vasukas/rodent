#ifndef RESBASE_HPP
#define RESBASE_HPP

#include <string>
#include <vector>
#include "vaslib/vas_math.hpp"

struct ParticleGroupGenerator;
struct TextureReg;



enum ModelType
{
	MODEL_LEVEL_GRID,
	MODEL_LEVEL_STATIC,
	
	MODEL_ERROR, ///< Model-not-found model
	MODEL_NONE, ///< Displays nothing
	MODEL_WINRAR,
	
	MODEL_PC_RAT, ///< Player character
	MODEL_PC_SHLD, ///< Projected shield
	
	MODEL_BOX_SMALL,
	MODEL_DRONE,
	MODEL_WORKER,
	MODEL_CAMPER,
	MODEL_HUNTER,
	
	MODEL_ARMOR,
	MODEL_TERMINAL_KEY,
	
	MODEL_DISPENSER,
	MODEL_TERMINAL,
	MODEL_MINIDOCK,
	MODEL_TERMINAL_FIN,
	
	MODEL_DOCKPAD,
	MODEL_TELEPAD,
	MODEL_ASSEMBLER,
	
	MODEL_MINEDRILL,
	MODEL_STORAGE,
	MODEL_CONVEYOR,
	MODEL_STORAGE_BOX,
	MODEL_STORAGE_BOX_OPEN,
	MODEL_HUMANPOD,
	MODEL_SCIENCE_BOX,
	
	MODEL_BAT,
	MODEL_HANDGUN,
	MODEL_BOLTER,
	MODEL_GRENADE,
	MODEL_MINIGUN,
	MODEL_ROCKET,
	MODEL_ELECTRO,
	
	MODEL_HANDGUN_AMMO,
	MODEL_BOLTER_AMMO,
	MODEL_GRENADE_AMMO,
	MODEL_MINIGUN_AMMO,
	MODEL_ROCKET_AMMO,
	MODEL_ELECTRO_AMMO,
	
	MODEL_HANDGUN_PROJ,
	MODEL_BOLTER_PROJ,
	MODEL_GRENADE_PROJ,
	MODEL_MINIGUN_PROJ,
	MODEL_ROCKET_PROJ,
	
	MODEL_BOLTER_PROJ_ALT,
	MODEL_GRENADE_PROJ_ALT,
	MODEL_MINIGUN_PROJ_ALT,
	MODEL_ELECTRO_PROJ_ALT,
	
	MODEL_SPHERE,
	MODEL_TOTAL_COUNT_INTERNAL ///< Do not use
};

enum ModelEffect
{
	ME_DEATH,
	ME_AURA,
	
	ME_TOTAL_COUNT_INTERNAL ///< Do not use
};

enum FreeEffect
{
	FE_EXPLOSION,
	FE_HIT,
	FE_HIT_SHIELD,
	FE_WPN_EXPLOSION,
	FE_SHOT_DUST,
	FE_SPEED_DUST,
	FE_SPAWN,
	FE_WPN_CHARGE,
	FE_CIRCLE_AURA,
	
	FE_TOTAL_COUNT_INTERNAL ///< Do not use
};



class ResBase
{
public:
	static ResBase& get(); ///< Returns singleton
	virtual ~ResBase() = default;
	
	virtual ParticleGroupGenerator* get_eff(ModelType type, ModelEffect eff) = 0;	
	virtual ParticleGroupGenerator* get_eff(FreeEffect eff) = 0;
	
	virtual vec2fp get_size(ModelType type) = 0;
	virtual TextureReg get_image(ModelType type) = 0; ///< May change
	
protected:
	friend class GamePresenter_Impl;
	virtual void init_ren() = 0;
};

#endif // RESBASE_HPP
