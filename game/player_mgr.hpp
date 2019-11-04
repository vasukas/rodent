#ifndef PLAYER_MGR_HPP
#define PLAYER_MGR_HPP

#include "entity.hpp"

class PlayerController;

class PlayerManager
{
public:
	bool god_mode = false;
	
	static PlayerManager* create(std::shared_ptr<PlayerController> pc_ctr);
	virtual ~PlayerManager() = default;
	
	virtual Entity* get_ent() const = 0; ///< May return null
	virtual bool is_player(Entity* ent) const = 0;
	
	virtual void render(TimeSpan passed) = 0; ///< May be called not at each cycle
	virtual void update_godmode() = 0; ///< Call if changed 'god_mode'
	
	/// Current AI activation and deactivation rects
	virtual std::pair<Rect, Rect> get_ai_rects() = 0;
	
protected:
	friend class GameCore_Impl;
	virtual void step() = 0;
};

#endif // PLAYER_MGR_HPP
