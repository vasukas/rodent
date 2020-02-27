#include "client/presenter.hpp"
#include "game_ai/ai_control.hpp"
#include "utils/noise.hpp"
#include "vaslib/vas_containers.hpp"
#include "vaslib/vas_log.hpp"
#include "game_core.hpp"
#include "level_ctr.hpp"
#include "player_mgr.hpp"
#include "physics.hpp"



class GameCore_Impl : public GameCore
{
public:
	struct Del_Entity { void operator()( Entity* p ) { delete p; } };
	
	std::unique_ptr<LevelControl> lc;
	std::unique_ptr<PhysicsWorld> phy;
	std::unique_ptr<PlayerManager> pmg;
	std::unique_ptr<AI_Controller> aic;
	
	std::array<SparseArray<EComp*>, static_cast<size_t>(ECompType::TOTAL_COUNT)> cs_list;
	SparseArray<Entity*> es_list;
	
	SparseArray<std::unique_ptr <Entity, Del_Entity>> ents;
	std::vector<size_t> e_next1; // stack of indices which would be free on cycle after the next
	std::vector<size_t> e_next2; // stack of indices which would be free on the next cycle
	std::vector<Entity*> e_todel;
	
	uint32_t step_cou = 0;
	TimeSpan step_time_cou = {};
	
	RandomGen rndg;
	bool step_flag = false;
	bool is_freeing_flag = false;
	
	
	
	GameCore_Impl(InitParams pars)
	{
		dbg_ai_attack = true;
		dbg_ai_see_plr = true;
		spawn_drop = false;
		
		ents.block_size = 256;
		
		phy.reset(new PhysicsWorld(*this));
		lc = std::move(pars.lc);
		
		pmg.reset(PlayerManager::create(*this));
		aic.reset(AI_Controller::create(*this));
		
		if (!pars.random_init.empty())
			rndg.load(pars.random_init);
	}
	~GameCore_Impl() {
		is_freeing_flag = true;
	}
	
	AI_Controller& get_aic()    noexcept {return *aic;}
	LevelControl&  get_lc()     noexcept {return *lc ;}
	PhysicsWorld&  get_phy()    noexcept {return *phy;}
	PlayerManager& get_pmg()    noexcept {return *pmg;}
	RandomGen&     get_random() noexcept {return rndg;}
	
	uint32_t      get_step_counter() const noexcept {return step_cou;}
	TimeSpan      get_step_time()    const noexcept {return step_time_cou;}
	bool          is_in_step()       const noexcept {return step_flag;}
	bool          is_freeing()       const noexcept {return is_freeing_flag;}
	
	void step(TimeSpan now)
	{
		++step_cou;
		step_time_cou += step_len;
		
		phy->raycast_count = 0;
		phy->aabb_query_count = 0;
		
		// delete entities
		
		append( ents.raw_free_indices(), e_next2 );
		e_next2 = std::move( e_next1 );
		step_flag = true;
		
		// tick entities
		
		auto step_comp = [this](ECompType type)
		{
			Entity* ent = nullptr;
			try {
				for (auto& c : cs_list[static_cast<size_t>(type)]) {
					ent = &c->ent;
					c->step();
				}
			}
			catch (std::exception& e) {
				THROW_FMTSTR("Failed to step component of type {} on ({}) - {}",
				             enum_name(type), ent? ent->dbg_id() : "null", e.what());
			}
		};
		
		step_comp(ECompType::StepPreUtil);
		step_comp(ECompType::StepLogic);
		
		{	Entity* ent = nullptr;
			try {
				for (auto& e : es_list)
					(ent = e)->step();
			}
			catch (std::exception& e) {
				THROW_FMTSTR("Failed to step entity ({}) - {}",
				             ent? ent->dbg_id() : "null", e.what());
			}
		}
		
		step_comp(ECompType::StepPostUtil);
		
		// tick systems
		
		auto step_sys = [](auto& s, const char *name)
		{
			try {
				s.step();
			}
			catch (std::exception& e) {
				THROW_FMTSTR("Failed to step {} - {}", name, e.what());
			}
		};
		step_sys(*phy, "physics");
		step_sys(*pmg, "plr_mgr");
		step_sys(*aic, "ai_ctr");
		
		if (auto gp = GamePresenter::get()) {
			try {gp->sync(now);}
			catch (std::exception& e) {
				THROW_FMTSTR("Failed to sync presenter - {}", e.what());
			}
		}
		
		// finish
		
		for (auto e : e_todel) delete e;
		e_todel.clear();
		
		step_flag = false;
	}
	Entity* get_ent( EntityIndex ei ) const noexcept
	{
		if (is_freeing_flag) return nullptr;
		size_t i = ei.to_int();
		if (i < ents.size()) {
			auto e = ents[i].get();
			if (e && e->is_ok()) return e;
		}
		return nullptr;
	}
	Entity* valid_ent( EntityIndex& i ) const noexcept
	{
		auto ent = get_ent(i);
		if (!ent) i = {};
		return ent;
	}
	Entity& ent_ref(EntityIndex ei) const
	{
		if (auto e = get_ent(ei)) return *e;
		GAME_THROW("GameCore::ent_ref() failed (eid {})", ei.to_int());
	}
	void foreach(callable_ref<void(Entity&)> f)
	{
		for (auto& e : ents)
			f(*e);
	}
	EntityIndex on_ent_create(Entity* e) noexcept
	{
		size_t i = ents.emplace_new(e);
		return EntityIndex::from_int(i);
	}
	void on_ent_destroy(Entity* e) noexcept
	{
		size_t ix = e->index.to_int();
		ents[ix].release();
		
		reserve_more_block( e_next1, 256 );
		e_next1.push_back( ix );
		
		if (!is_in_step()) delete this;
		else {
			reserve_more_block( e_todel, 128 );
			e_todel.push_back(e);
		}
	}
	size_t reg_ent(Entity* e) noexcept
	{
		return es_list.emplace_new(e);
	}
	void unreg_ent(size_t i) noexcept
	{
		es_list.free_and_reset(i);
	}
	size_t reg_c(ECompType type, EComp* c) noexcept
	{
		return cs_list[static_cast<size_t>(type)].emplace_new(c);
	}
	void unreg_c(ECompType type, size_t i) noexcept
	{
		cs_list[static_cast<size_t>(type)].free_and_reset(i);
	}
};
GameCore* GameCore::create(InitParams pars) {
	return new GameCore_Impl (std::move(pars));
}
