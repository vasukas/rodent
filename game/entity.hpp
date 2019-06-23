#ifndef ENTITY_HPP
#define ENTITY_HPP

#include <memory>
#include "vaslib/vas_math.hpp"

class  GameCore;
struct EC_Physics;
struct EC_Render;
class  Entity;

/// Unique entity index, always non-zero for existing entity
typedef uint32_t EntityIndex;



/// Internal behaviour
struct EC_Logic
{
	Entity* ent = nullptr;
	
	virtual ~EC_Logic() = default;
	virtual void tick() = 0; ///< Called at each game step
};



/// Game object
class Entity final
{
public:
	const EntityIndex index;
	GameCore& core;
	
	std::string dbg_name = {};
	std::unique_ptr<EC_Logic> logic; // temp
	
	
	void destroy(); ///< Deletes entity immediatly or at the end of the step. Index garanteed to be not used in next step

	Transform get_pos() const; ///< Returns center position
	float     get_dir() const; ///< Returns face direction
	
	EC_Physics *get_phy() const {return c_phy.get();}
	EC_Render  *get_ren() const {return c_ren.get();}
	
	void cnew(EC_Physics *c);
	void cnew(EC_Render  *c);
	
private:
	friend class GameCore_Impl;
	
	std::unique_ptr<EC_Physics> c_phy;
	std::unique_ptr<EC_Render>  c_ren;
	
	Entity( GameCore&, EntityIndex );
	~Entity();
};

#endif // ENTITY_HPP
