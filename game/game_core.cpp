#include "utils/noise.hpp"
#include "vaslib/vas_containers.hpp"
#include "vaslib/vas_log.hpp"
#include "game_core.hpp"
#include "physics.hpp"
#include "presenter.hpp"

const TimeSpan GameCore::step_len = TimeSpan::seconds(1./30);



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
	
	uint32_t step_cou = 1;
	RandomGen rndg;
	bool step_flag = false;
	
	
	
	GameCore_Impl(const InitParams& pars)
	{
		ents.fi_expand = 256;
		phy.reset(new PhysicsWorld(*this));
		rndg.gen.seed(pars.random_seed);
	}
	~GameCore_Impl() = default;
	
	PhysicsWorld& get_phy()                noexcept {return *phy;}
	uint32_t      get_step_counter() const noexcept {return step_cou;}
	bool          is_in_step()       const noexcept {return step_flag;}
	
	void step()
	{
		// delete entities
		
		append( ents.raw_free_indices(), e_next2 );
		e_next2 = std::move( e_next1 );
		step_flag = true;
		
		// tick systems
		
		auto step_comp = [this](ECompType type)
		{
			for (auto& c : cs_list[static_cast<size_t>(type)])
				c->step();
		};
		step_comp(ECompType::StepLogic);
		step_comp(ECompType::StepPostUtil);
		
		phy->step();
		GamePresenter::get().submit();
		
		// finish
		
		step_flag = false;
		e_todel.clear();
	}
	Entity* create_ent() noexcept
	{
		size_t i = ents.new_index();
		auto e = new Entity(i + 1);
		ents[i].reset( e );
		return e;
	}
	Entity* get_ent( EntityIndex i ) const noexcept
	{
		--i;
		return i < ents.size() ? ents[i].get() : nullptr;
	}
	std::vector<EComp*>& get_comp_list(ECompType type) noexcept
	{
		return cs_list[static_cast<size_t>(type)].raw_values();
	}
	RandomGen& get_random() noexcept
	{
		return rndg;
	}
	void mark_deleted( Entity* e ) noexcept
	{
		if (is_in_step() && e->was_destroyed) return;
		
		ents[e->index - 1].release();
		
		reserve_more_block( e_next1, 256 );
		e_next1.push_back( e->index - 1 );
		
		if (!is_in_step()) delete e;
		else e_todel.emplace_back(e);
	}
	size_t reg_c(ECompType type, EComp* c) noexcept
	{
		return cs_list[static_cast<size_t>(type)].insert_new(c);
	}
	void unreg_c(ECompType type, size_t i) noexcept
	{
		cs_list[static_cast<size_t>(type)][i] = nullptr;
	}
};



static GameCore* core;
GameCore& GameCore::get() {return *core;}
GameCore* GameCore::create( const InitParams& pars ) {return core = new GameCore_Impl(pars);}
GameCore::~GameCore() {core = nullptr;}
