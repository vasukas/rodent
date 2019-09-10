#ifndef GAME_PRESENTER_HPP
#define GAME_PRESENTER_HPP

#include <memory>
#include <vector>
#include "game/entity.hpp"
#include "render/particles.hpp"
#include "utils/color_manip.hpp"
#include "vaslib/vas_time.hpp"
#include "resbase.hpp"

struct LevelTerrain;
struct PresCommand;



struct ECompRender : EComp
{
	enum AttachType
	{
		ATT_WEAPON,
		ATT_TOTAL_COUNT ///< Do not use
	};
	
	ECompRender(Entity* ent);
	virtual ~ECompRender() = default;
	
	void parts(ModelType model, ModelEffect effect, const ParticleGroupGenerator::BatchPars& pars);
	void parts(FreeEffect effect, const ParticleGroupGenerator::BatchPars& pars);
	void attach(AttachType type, Transform at, ModelType model, FColor clr);
	void detach(AttachType type) {attach(type, {}, MODEL_NONE, {});}
	
	const Transform& get_pos() const {return _pos;} ///< Current rendering position
	
protected:
	void send(PresCommand& c);
	virtual void proc(const PresCommand& c); ///< Applies non-standard command to component
	virtual void sync() {} ///< Performs additional synchronization
	virtual void on_destroy() {} ///< Called once before destroying entity
	
private:
	friend class GamePresenter_Impl;
	Transform _pos, _vel;
	size_t _comp_id = size_t_inval;
	bool _is_ok = true;
	
	friend class GameCore_Impl;
	void on_destroy_ent();
};



struct EC_RenderSimple : ECompRender
{
	ModelType model;
	FColor clr;
	
	EC_RenderSimple(Entity* ent, ModelType model = MODEL_ERROR, FColor clr = FColor(1,1,1,1));
	void on_destroy() override;
	void step() override;
};



struct EC_RenderBot : ECompRender
{
	struct Attach
	{
		ModelType model = MODEL_NONE;
		Transform at;
		FColor clr;
	};
	
	std::array<Attach, ATT_TOTAL_COUNT> atts;
	ModelType model;
	FColor clr;
	
	float rot = 0.f; ///< override
	
	EC_RenderBot(Entity* ent, ModelType model, FColor clr);
	void on_destroy() override;
	void step() override;
	void proc(const PresCommand& c) override;
};



struct PresCommand
{
	enum Type
	{
		T_ERROR,
		
		// object
		T_CREATE,
		T_DEL, // ix0 comp_id
		T_OBJPARTS, // ix0 model, ix1 effect, pars
		T_FREEPARTS, // ix0 effect, pars
		
		// general
		T_EFFECT, // ix0 effect, pars
		
		// object non-standard
		T_ATTACH // ix0 type, ix1 model, pos, clr
	};
	
	Type type = T_ERROR;
	ECompRender* ptr = nullptr;
	
	size_t ix0, ix1;
	Transform pos;
	float power;
	FColor clr;
	
	// uses: pos, power, clr
	void set(const ParticleGroupGenerator::BatchPars& pars);
	void get(ParticleGroupGenerator::BatchPars& pars);
};



class GamePresenter
{
	// Intended to run in separate thread with external sync
public:
	struct InitParams
	{
		std::shared_ptr<LevelTerrain> lvl; ///< Must be non-null
	};
	
	static void init(const InitParams& pars); ///< Creates singleton (must be called from render thread)
	static GamePresenter* get(); ///< Returns singleton
	virtual ~GamePresenter();
	
	virtual void sync() = 0; ///< Synchronizes with GameCore (must be called from logic thread)
	virtual void add_cmd(const PresCommand& c) = 0;
	
	virtual void render(TimeSpan passed) = 0; ///< Renders everything (must be called from render thread)
	virtual TimeSpan get_passed() = 0; ///< Returns last passed time (for components)
	
	void effect(FreeEffect effect, const ParticleGroupGenerator::BatchPars& pars);
};

#endif // GAME_PRESENTER_HPP
