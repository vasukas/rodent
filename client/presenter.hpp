#ifndef GAME_PRESENTER_HPP
#define GAME_PRESENTER_HPP

#include <variant>
#include "game/entity.hpp"
#include "render/ren_particles.hpp"
#include "resbase.hpp"

struct EC_RenderPos;
struct EC_RenderComp;
struct LevelTerrain;

struct PresCmdParticles {
	EntityIndex eid;
	ParticleGroupGenerator* gen;
	ParticleBatchPars pars;
};

struct FloatText
{
	vec2fp at;
	std::string str;
	uint32_t color = 0xffffffff;
	float size = 1;
	TimeSpan show_len = TimeSpan::seconds(1.0);
	TimeSpan fade_len = TimeSpan::seconds(1.5);
	float spread_strength = 1;
};

using PresCommand = std::variant
<
	PresCmdParticles
>;



struct GameRenderEffect
{
	virtual ~GameRenderEffect() = default;
	virtual void init() {} ///< Called in rendering thread
	virtual bool render(TimeSpan passed) = 0; ///< Return false to delete
	
private:
	friend class GamePresenter_Impl;
	bool is_first = true;
};



// Intended to run in separate thread with external sync
class GamePresenter
{
public:
	struct InitParams
	{
		GameCore* core; ///< Must be non-null
		const LevelTerrain* lvl; ///< Must be non-null. Saved for reinit
	};
	
	bool playback_hack = false; ///< Disables interpolation
	
	static GamePresenter* init(const InitParams& pars); ///< Creates singleton
	static GamePresenter* get(); ///< Returns singleton
	virtual ~GamePresenter();
	
	virtual void sync(TimeSpan now) = 0; ///< Synchronizes with GameCore (must be called from logic thread)
	virtual void add_cmd(PresCommand c) = 0;
	
	virtual void render(TimeSpan now, TimeSpan passed) = 0; ///< Renders everything (must be called from render thread)
	virtual TimeSpan get_passed() = 0; ///< Returns last passed time (for components)
	virtual Rectfp get_vport() = 0; ///< Returns world frustum rect
	
	void effect(PGG_Pointer ppg, const ParticleBatchPars& pars);
	
	// Displayed only for one logic step
	virtual void dbg_line(vec2fp a, vec2fp b, uint32_t clr, float wid = 0.2f) = 0;
	virtual void dbg_rect(Rectfp area, uint32_t clr) = 0;
	virtual void dbg_rect(vec2fp ctr, uint32_t clr, float rad = 0.5f) = 0;
	virtual void dbg_text(vec2fp at, std::string str, uint32_t clr = 0xffffffff) = 0;
	
	virtual void add_effect(std::unique_ptr<GameRenderEffect> eff) = 0;
	virtual void add_float_text(FloatText text) = 0;
	
	/// Performs debug screenshot on following rendering step
	virtual void dbg_screenshot() = 0;
	
	/// Must be called from rendering thread
	virtual void reinit_resources(const LevelTerrain& lvl) = 0;
	
protected:
	friend EC_RenderPos;
	friend EC_RenderComp;
	
	virtual void on_add(EC_RenderPos& c) = 0;
	virtual void on_rem(EC_RenderPos& c) = 0;
	virtual void on_add(EC_RenderComp& c) = 0;
	virtual void on_rem(EC_RenderComp& c) = 0;
};

#endif // GAME_PRESENTER_HPP
