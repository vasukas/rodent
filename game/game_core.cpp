#include "client/presenter.hpp"
#include "utils/noise.hpp"
#include "vaslib/vas_containers.hpp"
#include "vaslib/vas_log.hpp"
#include "game_core.hpp"
#include "physics.hpp"



class GameCore_Impl : public GameCore
{
public:
	struct Del_Entity { void operator()( Entity* p ) { delete p; } };
	
	std::unique_ptr<PhysicsWorld> phy;
	std::array<SparseArray<EComp*>, static_cast<size_t>(ECompType::TOTAL_COUNT)> cs_list;
	
	SparseArray<std::unique_ptr <Entity, Del_Entity>> ents;
	std::vector<size_t> e_next1; // stack of indices which would be free on cycle after the next
	std::vector<size_t> e_next2; // stack of indices which would be free on the next cycle
	std::vector<std::unique_ptr <Entity, Del_Entity>> e_todel; // freed at the end of step
	
	uint32_t step_cou = 0;
	RandomGen rndg;
	bool step_flag = false;
	
	
	
	GameCore_Impl(const InitParams& pars)
	{
		ents.block_size = 256;
		phy.reset(new PhysicsWorld(*this));
		rndg.gen.seed(pars.random_seed);
	}
	~GameCore_Impl() = default;
	
	PhysicsWorld& get_phy()    noexcept {return *phy;}
	RandomGen&    get_random() noexcept {return rndg;}
	
	uint32_t      get_step_counter() const noexcept {return step_cou;}
	bool          is_in_step()       const noexcept {return step_flag;}
	
	void step()
	{
		++step_cou;
		
		// delete entities
		
		append( ents.raw_free_indices(), e_next2 );
		e_next2 = std::move( e_next1 );
		step_flag = true;
		
		// tick systems
		
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
				             static_cast<int>(type), ent? ent->dbg_id() : "null", e.what());
			}
		};
		step_comp(ECompType::StepLogic);
		step_comp(ECompType::StepPostUtil);
		
		try {
			phy->step();
		}
		catch (std::exception& e) {
			THROW_FMTSTR("Failed to step physics - {}", e.what());
		}
		
		if (auto gp = GamePresenter::get()) {
			try {
				gp->sync();
			}
			catch (std::exception& e) {
				THROW_FMTSTR("Failed to sync presenter - {}", e.what());
			}
		}
		
		// finish
		
		step_flag = false;
		e_todel.clear();
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
	size_t reg_c(ECompType type, EComp* c) noexcept
	{
		return cs_list[static_cast<size_t>(type)].emplace_new(c);
	}
	void unreg_c(ECompType type, size_t i) noexcept
	{
		cs_list[static_cast<size_t>(type)][i] = nullptr;
		cs_list[static_cast<size_t>(type)].free_index(i);
	}
};



static GameCore* core;
GameCore& GameCore::get() {return *core;}
GameCore* GameCore::create( const InitParams& pars ) {return core = new GameCore_Impl(pars);}
GameCore::~GameCore() {core = nullptr;}
