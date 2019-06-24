#include <random>
#include "vaslib/vas_cpp_utils.hpp"
#include "game_core.hpp"
#include "logic.hpp"
#include "physics.hpp"
#include "presenter.hpp"

const TimeSpan GameCore::step_len = TimeSpan::seconds(1./30);

class GameCore_Impl : public GameCore
{
public:
	struct Del_Entity { void operator()( Entity* p ) { delete p; } };
	
	std::unique_ptr<PhysicsWorld> phy;
	
	std::vector<std::unique_ptr <Entity, Del_Entity>> ents;
	std::vector<size_t> e_free; // stack of free indices
	std::vector<size_t> e_next1; // stack of indices which would be free on cycle after the next
	std::vector<size_t> e_next2; // stack of indices which would be free on the next cycle
	std::vector<std::unique_ptr <Entity, Del_Entity>> e_todel; // freed at the end of step
	
	uint32_t step_cou = 1;
	std::mt19937 rndg;
	bool step_flag = false;
	
	
	
	GameCore_Impl(const InitParams& pars)
	{
		phy.reset(new PhysicsWorld(*this));
		rndg.seed(pars.random_seed);
	}
	~GameCore_Impl() = default;
	
	PhysicsWorld& get_phy()                noexcept {return *phy;}
	uint32_t      get_step_counter() const noexcept {return step_cou;}
	bool          is_in_step()       const noexcept {return step_flag;}
	
	void step()
	{
		// delete entities
		
		e_free.insert( e_free.end(), e_next2.begin(), e_next2.end() );
		e_next2 = std::move( e_next1 );
		step_flag = true;
		
		// tick systems
		
		for (auto& e : ents) if (e) if (auto c = e->get_log()) c->step();
		
		phy->step();
		
		GamePresenter::get().submit();
		
		// finish
		
		step_flag = false;
		e_todel.clear();
	}
	Entity* create_ent() noexcept
	{
		if (e_free.empty())
		{
			size_t n = ents.size();
			size_t cap = n + 256;
			ents.resize( cap );
			
			e_free.reserve( e_free.size() + 256 );
			for (; n < cap; ++n) e_free.push_back( n );
		}
		
		size_t i = e_free.back();
		e_free.pop_back();
		
		auto e = new Entity( *this, i + 1 );
		ents[i].reset( e );
		return e;
	}
	Entity* get_ent( EntityIndex i ) const noexcept
	{
		--i;
		return i < ents.size() ? ents[i].get() : nullptr;
	}
	uint32_t get_random() noexcept
	{
		return rndg();
	}
	void mark_deleted( Entity* e ) noexcept
	{
		if (is_in_step()) {
			auto it = std::find_if( e_todel.begin(), e_todel.end(), [&](auto&& v){return v.get() == e;} );
			if (it != e_todel.end()) return;
		}
		
		ents[e->index - 1].release();
		
		reserve_more_block( e_next1, 256 );
		e_next1.push_back( e->index );
		
		if (!is_in_step()) delete e;
		else e_todel.emplace_back(e);
	}
};



static GameCore* core;
GameCore& GameCore::get() {return *core;}
GameCore* GameCore::create( const InitParams& pars ) {return core = new GameCore_Impl(pars);}
GameCore::~GameCore() {core = nullptr;}
