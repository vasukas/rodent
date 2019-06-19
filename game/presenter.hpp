#ifndef GAME_PRESENTER_HPP
#define GAME_PRESENTER_HPP

#include <memory>
#include <vector>
#include "render/particles.hpp"
#include "utils/color_manip.hpp"
#include "vaslib/vas_math.hpp"
#include "vaslib/vas_time.hpp"
#include "vaslib/vas_types.hpp"

class  Entity;
class  GameCore;
struct PresCommand;



struct EC_Render
{
	Entity* ent;
	
	EC_Render(Entity* ent, size_t sprite_id);
	~EC_Render();
	void parts(size_t id, float power = 1.f, Transform rel = {});
	
private:
	void send(PresCommand& c);
};



struct PresCommand
{
	enum Type
	{
		T_CREATE, ///< new object; [sprite index]
		T_DEL, ///< delete object with post-effect; [pos]
		T_OBJPARTS, ///< generate particles onto obj [preset index, pos (relative), power]
		
		T_FREEPARTS ///< generate particles [(obj ignored), preset index, pos, power]
	};
	Type type;
	size_t obj; ///< non-zero EntityIndex
	
	size_t index;
	Transform pos;
	float power;
};

struct PresObject
{
	size_t id; ///< RenInstanced id
	FColor clr = FColor(1, 1, 1, 1);
	std::vector<std::shared_ptr<ParticleGroupGenerator>> ps; ///< [0] must be null or contain death gen
};

/// Runs in separate thread
class GamePresenter
{
public:
	static GamePresenter& get(); ///< Returns singleton (inits if needed)
	virtual ~GamePresenter();
	
	virtual void render(TimeSpan passed) = 0; ///< Renders everything
	
	virtual void add_cmd(const PresCommand& cmd) = 0; ///< Adds command to queue
	virtual void submit() = 0; ///< Applies new commands asynchronously
	
	virtual size_t add_preset(std::shared_ptr<ParticleGroupGenerator> p) = 0; ///< Adds new preset, returns internal index
	virtual size_t add_preset(const PresObject& p) = 0; ///< Adds new preset, returns internal index
	
	virtual void effect(size_t preset_id, Transform at) = 0; ///< Plays particle effect
};

#endif // GAME_PRESENTER_HPP
