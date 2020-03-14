#ifndef EC_RENDER_HPP
#define EC_RENDER_HPP

#include "client/resbase.hpp"
#include "game/entity.hpp"
#include "render/ren_particles.hpp"



struct EC_RenderPos : EDynComp
{
	static constexpr int interp_depth = 3;
	bool disable_culling = false; ///< If true, never culled
	bool immediate_rotation = false; ///< If true, rotation isn't interpolated (only for some components)
	
	const Transform& get_cur() const {return pos;}
	const Transform& newest() const {return fn ? fs[fn-1].second : pos;}
	bool is_visible_now() const {return in_vport;}
	
	EC_RenderPos(Entity& ent);
	~EC_RenderPos();
	void parts(PGG_Pointer gen, const ParticleBatchPars& pars);
	
private:
	friend class GamePresenter_Impl;
	
	Transform pos;
	bool in_vport = true; 
	
	std::array<std::pair<TimeSpan, Transform>, interp_depth> fs;
	int fn = 0;
	
	vec2fp vel = {};
};



struct EC_RenderComp : EDynComp
{
protected:
	EC_RenderComp(Entity& ent);
	~EC_RenderComp();
	
	friend class GamePresenter_Impl;
	virtual void render(const EC_RenderPos& p, TimeSpan passed) = 0;
	virtual bool allow_immediate_rotation() {return false;}
	virtual void on_vport_enter() {} ///< Called when stops being culled
};



struct EC_RenderModel : EC_RenderComp
{
	enum DeathType {
		DEATH_NONE,
		DEATH_PARTICLES,
		DEATH_AND_EXPLOSION
	};
	
	ModelType model;
	FColor clr;
	DeathType death;
	
	EC_RenderModel(Entity& ent, ModelType model = MODEL_ERROR, FColor clr = FColor(1,1,1,1), DeathType death = DEATH_PARTICLES)
		: EC_RenderComp(ent), model(model), clr(clr), death(death) {}
	~EC_RenderModel();
	
	void parts(ModelEffect effect, const ParticleBatchPars& pars);
	
private:
	void render(const EC_RenderPos& p, TimeSpan passed);
	bool allow_immediate_rotation() {return true;}
};



struct EC_RenderEquip : EC_RenderComp
{
	enum AttachType
	{
		ATT_WEAPON,
		ATT_SHIELD,
		
		ATT_TOTAL_COUNT_INTERNAL ///< Do not use
	};
	
	EC_RenderEquip(Entity& ent): EC_RenderComp(ent) {}
	void attach(AttachType type, Transform at, ModelType model, FColor clr);
	void detach(AttachType type) {attach(type, {}, MODEL_NONE, {});}
	
private:
	struct Attach
	{
		ModelType model = MODEL_NONE;
		Transform at;
		FColor clr;
	};
	std::array<Attach, ATT_TOTAL_COUNT_INTERNAL> atts;
	
	void render(const EC_RenderPos& p, TimeSpan passed);
	bool allow_immediate_rotation() {return true;}
};



struct EC_ParticleEmitter : EC_RenderComp
{
	struct Emit
	{
		TimeSpan period;
		TimeSpan tmo; ///< Timeout to next period
		int left; ///< Times left
		
		ParticleGroupGenerator* gen;
		ParticleBatchPars pars;
		Transform tr;
		
		Emit() = default;
		Emit(PGG_Pointer gen, const ParticleBatchPars& pars, TimeSpan period, TimeSpan total_time);
	};
	
	struct Channel : SubresRef<Channel, EC_ParticleEmitter>
	{
		TimeSpan period; ///< Timeout value
		TimeSpan after; ///< Time after which can emit
		
		void once(PGG_Pointer gen, const ParticleBatchPars& pars, std::optional<TimeSpan> new_period = {});
		bool is_playing() const;
		
		void play(PGG_Pointer gen, const ParticleBatchPars& pars, TimeSpan period, TimeSpan total_time); // ignores timeout
		void stop(bool force = false); ///< If force is true, also resets timeout
		
	private:
		friend EC_ParticleEmitter;
		std::optional<Emit> em;
		EC_ParticleEmitter* pe() const {return get_root();}
	};
	
	
	
	EC_ParticleEmitter(Entity& ent): EC_RenderComp(ent), chs(this) {}
	
	/// Emits particles once per period for specified time
	void effect(PGG_Pointer gen, const ParticleBatchPars& pars, TimeSpan period, TimeSpan total_time);
	
	/// Creates new emitter
	std::unique_ptr<Channel> new_channel();
	
private:
	std::vector<Emit> ems;
	SubresRoot<Channel, EC_ParticleEmitter> chs;
	
	void render(const EC_RenderPos& p, TimeSpan passed);
};



struct EC_LaserDesigRay : EC_RenderComp
{
	FColor clr;
	bool enabled = false;
	
	void set_target(vec2fp new_tar);
	void find_target(vec2fp dir); ///< Performs raycast
	
	EC_LaserDesigRay(Entity& ent, FColor clr = FColor(1, 0, 0, 0.6))
	    : EC_RenderComp(ent), clr(clr) {}
	
private:
	vec2fp tar_ray = {};
	std::optional<std::pair<vec2fp, vec2fp>> tar_next;
	float tar_next_t;
	
	void render(const EC_RenderPos& p, TimeSpan passed);
	void on_vport_enter() {tar_next.reset();}
};



struct EC_Uberray : EC_RenderComp
{
	FColor clr = FColor(1, 0.3, 0.2);
	TimeSpan left_max = TimeSpan::seconds(0.5);
	
	EC_Uberray(Entity& ent): EC_RenderComp(ent) {}
	void trigger(vec2fp target);
	
private:
	TimeSpan left;
	vec2fp b_last = {};
	
	void render(const EC_RenderPos& p, TimeSpan passed);
	void on_vport_enter() {left = {};}
};



struct EC_RenderDoor : EC_RenderComp
{
	static constexpr float min_off = 0.2; // meters; length in 'closed' state
	
	EC_RenderDoor(Entity& ent, vec2fp fix_he, bool is_x_ext, FColor l_clr)
	    : EC_RenderComp(ent), fix_he(fix_he), is_x_ext(is_x_ext), l_clr(l_clr) {}
	
	void set_state(float t) {t_ext = t;}
	
private:
	vec2fp fix_he;
	bool is_x_ext;
	FColor l_clr; // alpha is ignored
	float t_ext = 1;
	
	void render(const EC_RenderPos& p, TimeSpan passed);
};



struct EC_RenderCustomDebug : EC_RenderComp
{
	std::function<void(Transform pos, TimeSpan passed)> f;
	EC_RenderCustomDebug(Entity& ent, std::function<void(Transform pos, TimeSpan passed)> f)
		: EC_RenderComp(ent), f(std::move(f)) {}
	
private:
	void render(const EC_RenderPos& p, TimeSpan passed) {
		if (f) f(p.get_cur(), passed);
	}
};

#endif // EC_RENDER_HPP
