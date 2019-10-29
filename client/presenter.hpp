#ifndef GAME_PRESENTER_HPP
#define GAME_PRESENTER_HPP

#include <memory>
#include <variant>
#include <vector>
#include "game/entity.hpp"
#include "render/particles.hpp"
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
	enum AttachType
	{
		ATT_WEAPON,
		ATT_SHIELD,
		ATT_TOTAL_COUNT ///< Do not use
	};
	
	ECompRender(Entity* ent);
	virtual ~ECompRender() = default;
	
	void parts(ModelType model, ModelEffect effect, const ParticleBatchPars& pars);
	void parts(FreeEffect effect, const ParticleBatchPars& pars);
	void attach(AttachType type, Transform at, ModelType model, FColor clr);
	void detach(AttachType type) {attach(type, {}, MODEL_NONE, {});}
	
	const Transform& get_pos() const {return _pos;} ///< Current rendering position
	virtual void set_face(float angle) {(void) angle;} ///< Sets facing direction
	
protected:
	void send(PresCommand c);
	virtual void proc(PresCommand c); ///< Applies non-standard command to component
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
	void set_face(float angle) override {rot_tar = angle;}
	
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
	
	static void init(const InitParams& pars); ///< Creates singleton (must be called from render thread)
	static GamePresenter* get(); ///< Returns singleton
	virtual ~GamePresenter();
	
	virtual void sync() = 0; ///< Synchronizes with GameCore (must be called from logic thread)
	virtual void add_cmd(PresCommand c) = 0;
	
	virtual void render(TimeSpan passed) = 0; ///< Renders everything (must be called from render thread)
	virtual TimeSpan get_passed() = 0; ///< Returns last passed time (for components)
	
	void effect(FreeEffect effect, const ParticleBatchPars& pars);
	
	// Displayed only for one logic step
	void dbg_line(vec2fp a, vec2fp b, uint32_t clr, float wid = 0.2f);
	void dbg_rect(Rectfp area, uint32_t clr);
	void dbg_rect(vec2fp ctr, uint32_t clr, float rad = 0.5f);
	void dbg_text(vec2fp at, std::string str);
	
	/// Temporary effect, must return false when should be destroyed
	void add_effect(std::function<bool(TimeSpan passed)> eff);
	void add_float_text(FloatText text);
};

#endif // GAME_PRESENTER_HPP
