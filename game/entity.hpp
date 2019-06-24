#ifndef ENTITY_HPP
#define ENTITY_HPP

#include <memory>
#include "vaslib/vas_math.hpp"

class  GameCore;
struct EC_Logic;
struct EC_Physics;
struct EC_Render;
class  Entity;

/// Unique entity index, always non-zero for existing entity
typedef uint32_t EntityIndex;



/// Game object
class Entity final
{
public:
	const EntityIndex index;
	GameCore& core;
	
	std::string dbg_name = {};
	
	
	void destroy(); ///< Deletes entity immediatly or at the end of the step. Index garanteed to be not used in next step

	Transform get_pos() const; ///< Returns center position
	float     get_dir() const; ///< Returns face direction
	
	EC_Logic   *get_log() const {return c_log.get();}
	EC_Physics *get_phy() const {return c_phy.get();}
	EC_Render  *get_ren() const {return c_ren.get();}
	
	void cnew(EC_Logic   *c);
	void cnew(EC_Physics *c);
	void cnew(EC_Render  *c);
	
private:
	friend class GameCore_Impl;
	
	std::unique_ptr<EC_Logic>   c_log;
	std::unique_ptr<EC_Physics> c_phy;
	std::unique_ptr<EC_Render>  c_ren;
	
	Entity( GameCore&, EntityIndex );
	~Entity();
};

#endif // ENTITY_HPP
