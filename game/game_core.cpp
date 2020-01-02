#include "client/presenter.hpp"
#include "utils/noise.hpp"
#include "vaslib/vas_containers.hpp"
#include "vaslib/vas_log.hpp"
#include "game_core.hpp"
#include "player_mgr.hpp"
#include "physics.hpp"



class GameCore_Impl : public GameCore
{
public:
	struct Del_Entity { void operator()( Entity* p ) { delete p; } };
	
	std::unique_ptr<PhysicsWorld> phy;
	std::unique_ptr<PlayerManager> pmg;
	
	std::array<SparseArray<EComp*>, static_cast<size_t>(ECompType::TOTAL_COUNT)> cs_list;
	SparseArray<Entity*> es_list;
	
	SparseArray<std::unique_ptr <Entity, Del_Entity>> ents;
	std::vector<size_t> e_next1; // stack of indices which would be free on cycle after the next
	std::vector<size_t> e_next2; // stack of indices which would be free on the next cycle
	std::vector<std::unique_ptr <Entity, Del_Entity>> e_todel; // freed at the end of step
	
	uint32_t step_cou = 0;
	TimeSpan step_time_cou = {};
	
	RandomGen rndg;
	bool step_flag = false;
	bool is_freeing_flag = false;
	
	
	
	GameCore_Impl(InitParams pars)
	{
		dbg_ai_attack = true;
		spawn_drop = false;
		
		ents.block_size = 256;
		phy.reset(new PhysicsWorld(*this));
		pmg = std::move(pars.pmg);

		if (!pars.random_init.empty() && !rndg.load(pars.random_init))
			throw std::runtime_error("GameCore:: failed to init random");
	}
	~GameCore_Impl() {
		is_freeing_flag = true;
	}
	
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
					ent = c->ent;
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
		
		try {
			phy->step();
		}
		catch (std::exception& e) {
			THROW_FMTSTR("Failed to step physics - {}", e.what());
		}
		
		if (auto gp = GamePresenter::get()) {
			try {
				gp->sync(now);
			}
			catch (std::exception& e) {
				THROW_FMTSTR("Failed to sync presenter - {}", e.what());
			}
		}
		
		// finish
		
		step_flag = false;
		e_todel.clear();
		
		if (auto gp = GamePresenter::get())
			gp->del_sync();
		
		pmg->step();
	}
	Entity* get_ent( EntityIndex ei ) const noexcept
	{
		size_t i = ei.to_int();
		return i < ents.size() ? ents[i].get() : nullptr;
	}
	Entity* valid_ent( EntityIndex& i ) const noexcept
	{
		auto ent = get_ent(i);
		if (!ent) i = {};
		return ent;
	}
	EntityIndex create_ent(Entity* ent) noexcept
	{
		size_t i = ents.emplace_new(ent);
		return EntityIndex::from_int(i);
	}
	void mark_deleted(Entity* e) noexcept
	{
		if (auto ren = e->get_ren())
			ren->on_destroy_ent();
		
		size_t ix = e->index.to_int();
		ents[ix].release();
		
		reserve_more_block( e_next1, 256 );
		e_next1.push_back( ix );
		
		if (!is_in_step()) delete e;
		else e_todel.emplace_back(e);
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



static GameCore* core;
GameCore& GameCore::get() {return *core;}
GameCore* GameCore::create(InitParams pars) {return core = new GameCore_Impl (std::move(pars));}
GameCore::~GameCore() {core = nullptr;}
