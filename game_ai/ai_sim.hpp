#ifndef AI_SIM_HPP
#define AI_SIM_HPP

#include <variant>
#include "vaslib/vas_math.hpp"
#include "vaslib/vas_time.hpp"

class GameCore;



class AI_SimResource
{
public:
	enum Type
	{
		T_ROCK,
		T_LEVELTERM
	};
	
	static constexpr int max_capacity = 65535;
	static constexpr float nonstrict_chance = 0.4;
	
	struct Value
	{
		Type type;
		bool is_producer; // ignored if not resource
		
		int amount;
		int capacity;
	};
	
	struct ResultFinished {};
	struct ResultNotInRange {vec2fp move_target;};
	struct ResultWorking {vec2fp view_target;};
	using WorkResult = std::variant<ResultFinished, ResultNotInRange, ResultWorking>;
	
	struct WorkerReg
	{
		WorkerReg() = default;
		~WorkerReg();
		
		WorkerReg(WorkerReg&&) noexcept;
		void operator= (WorkerReg&&) noexcept;
		
		/// Returns false if resource depleted, doesn't exist anymore or worker is full/empty. 
		/// Must be called at each step
		WorkResult process(Value& val, vec2fp pos);
		
		/// Returns true if really registered
		bool is_reg() const {return res;}
		
	private:
		friend AI_SimResource;
		AI_SimResource* res = {};
		int rate; // per second
		bool was_in_range = false;
		TimeSpan tmo;
		
		WorkerReg(AI_SimResource&, int rate);
		bool step(Value& vres, Value& val, bool check_only);
	};
	
	enum FindType
	{
		FIND_NEAREST_STRICT,
		FIND_NEAREST_RANDOM,
		
		FIND_F_VALUE_MASK = 3, // removes flags
		FIND_F_SAME_ROOM = 4 // flag
	};
	
	
	
	/// Calls reg for nearest suitable resource, if any
	static WorkerReg find(GameCore& core, vec2fp origin, float radius, Type type, int rate, flags_t find_type = FIND_NEAREST_RANDOM);
	
	/// Registers self in AI_Controller
	AI_SimResource(GameCore& core, Value val, vec2fp worker_pos, vec2fp view_tar);
	
	/// Returns null if can't be, otherwise returns new reg. 
	/// Rate (amount per second) is positive for producer-worker and negative for consumer-worker
	WorkerReg reg(int rate);
	
private:
	GameCore& core;
	Value val;
	vec2fp pos, vtar;
	bool is_used = false;
	
	bool can_reg(int rate) const;
};

#endif // AI_SIM_HPP
