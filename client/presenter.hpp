#ifndef GAME_PRESENTER_HPP
#define GAME_PRESENTER_HPP

#include <memory>
#include <variant>
#include <vector>
#include "game/entity.hpp"
#include "render/ren_particles.hpp"
#include "utils/color_manip.hpp"
#include "vaslib/vas_time.hpp"
#include "resbase.hpp"

struct LevelTerrain;

struct PresCmdCreate;
struct PresCmdDelete;
struct PresCmdObjEffect;
struct PresCmdEffect;
struct PresCmdDbgRect;
struct PresCmdDbgLine;
struct PresCmdDbgText;
struct PresCmdEffectFunc;
struct PresCmdAttach;

struct FloatText
{
	vec2fp at;
	std::string str;
	uint32_t color = 0xffffffff;
	float size = 1;
	TimeSpan show_len = TimeSpan::seconds(1.0);
	TimeSpan fade_len = TimeSpan::seconds(1.5);
};

using PresCommand = std::variant
<
	PresCmdCreate,
	PresCmdDelete,
	PresCmdObjEffect,
	PresCmdEffect,
	PresCmdDbgRect,
	PresCmdDbgLine,
	PresCmdDbgText,
	PresCmdEffectFunc,
	FloatText,
	PresCmdAttach
>;



struct ECompRender : EComp
{
	// MUST be inited after physics component (unless null ent is supplied)
	// WARNING: step() and sync() may not be called at each step
	
	enum AttachType
	{
		ATT_WEAPON,
		ATT_SHIELD,
		ATT_TOTAL_COUNT ///< Do not use
	};
	
	bool disable_culling = false; ///< If true, never culled
	
	ECompRender(Entity* ent);
	virtual ~ECompRender() = default;
	
	void parts(ModelType model, ModelEffect effect, const ParticleBatchPars& pars);
	void parts(FreeEffect effect, const ParticleBatchPars& pars);
	void attach(AttachType type, Transform at, ModelType model, FColor clr);
	void detach(AttachType type) {attach(type, {}, MODEL_NONE, {});}
	
	/// Current rendering position. 
	/// Must not be used outside of rendering components (non-atomic)
	const Transform& get_pos() const {return _pos;}
	
protected:
	void send(PresCommand c);
	virtual void proc(PresCommand c); ///< Applies non-standard command to component
	virtual void sync() {} ///< Performs additional synchronization
	virtual void on_destroy() {} ///< Called once before destroying entity
	
private:
	friend class GamePresenter_Impl;
	bool _is_ok = true;
	bool _in_vport = false; // is shown
	Transform _pos;
	size_t _comp_id = size_t_inval;
	
	struct PosQueue {
		std::array<std::pair<TimeSpan, Transform>, 3> fs;
		int fn = 0;
	};
	union {
		PosQueue _q_pos;
		Transform _vel;
	};
	
	friend class GameCore_Impl;
	void on_destroy_ent();
};



struct EC_RenderSimple : ECompRender
{
	ModelType model;
	FColor clr;
	
	EC_RenderSimple(Entity* ent, ModelType model = MODEL_ERROR, FColor clr = FColor(1,1,1,1));
	
private:
	void on_destroy() override;
	void step() override;
};



struct EC_RenderBot : ECompRender
{
	ModelType model;
	FColor clr;
	
	float rot = 0; ///< override current
	float rot_tar = 0; ///< override target
	
	EC_RenderBot(Entity* ent, ModelType model, FColor clr);
	
private:
	struct Attach
	{
		ModelType model = MODEL_NONE;
		Transform at;
		FColor clr;
	};
	std::array<Attach, ATT_TOTAL_COUNT> atts;
	
	void on_destroy() override;
	void step() override;
	void proc(PresCommand c) override;
	void sync() override {rot_tar = ent->get_face_rot();}
};



// internal
struct PresCmdCreate {
	ECompRender* ptr;
};
struct PresCmdDelete {
	ECompRender* ptr;
	size_t index;
};
struct PresCmdObjEffect {
	ECompRender* ptr;
	ModelType model;
	ModelEffect eff;
	ParticleBatchPars pars;
};
struct PresCmdEffect {
	ECompRender* ptr; // may be null
	FreeEffect eff;
	ParticleBatchPars pars;
};
struct PresCmdDbgRect {
	Rectfp dst;
	uint32_t clr;
};
struct PresCmdDbgLine {
	vec2fp a, b;
	uint32_t clr;
	float wid;
};
struct PresCmdDbgText {
	vec2fp at;
	std::string str;
	uint32_t clr;
};
struct PresCmdEffectFunc {
	std::function<bool(TimeSpan)> eff;
};

// non-standard
struct PresCmdAttach {
	ECompRender* ptr;
	ECompRender::AttachType type;
	ModelType model;
	FColor clr;
	Transform pos; // relative
};



// Intended to run in separate thread with external sync
class GamePresenter
{
public:
	struct InitParams
	{
		std::shared_ptr<LevelTerrain> lvl; ///< Must be non-null
	};
	
	static GamePresenter* init(const InitParams& pars); ///< Creates singleton (must be called from render thread)
	static GamePresenter* get(); ///< Returns singleton
	virtual ~GamePresenter();
	
	virtual void sync(TimeSpan now) = 0; ///< Synchronizes with GameCore (must be called from logic thread)
	virtual void del_sync() = 0; ///< Processes only delete messages and nothing else
	virtual void add_cmd(PresCommand c) = 0;
	
	virtual void render(TimeSpan now, TimeSpan passed) = 0; ///< Renders everything (must be called from render thread)
	virtual TimeSpan get_passed() = 0; ///< Returns last passed time (for components)
	virtual Rectfp get_vport() = 0; ///< Returns world frustum rect
	
	void effect(FreeEffect effect, const ParticleBatchPars& pars);
	
	// Displayed only for one logic step
	void dbg_line(vec2fp a, vec2fp b, uint32_t clr, float wid = 0.2f);
	void dbg_rect(Rectfp area, uint32_t clr);
	void dbg_rect(vec2fp ctr, uint32_t clr, float rad = 0.5f);
	void dbg_text(vec2fp at, std::string str, uint32_t clr = 0xffffffff);
	
	/// Temporary effect, must return false when should be destroyed
	void add_effect(std::function<bool(TimeSpan passed)> eff);
	void add_float_text(FloatText text);
	
	/// Performs debug screenshot on following rendering step
	virtual void dbg_screenshot() = 0;
};

#endif // GAME_PRESENTER_HPP
