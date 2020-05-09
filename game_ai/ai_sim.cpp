#include "game/game_core.hpp"
#include "game/level_ctr.hpp"
#include "utils/noise.hpp"
#include "ai_control.hpp"
#include "ai_sim.hpp"

static const float work_range = std::sqrt(AI_Const::move_slowprecise_dist_squ) + 0.01; 



AI_SimResource::WorkerReg::WorkerReg(AI_SimResource& res, int rate)
	: res(&res), rate(rate)
{
	res.is_used = true;
}
AI_SimResource::WorkerReg::~WorkerReg()
{
	if (res) res->is_used = false;
}
AI_SimResource::WorkerReg::WorkerReg(WorkerReg&& r) noexcept
{
	*this = std::move(r);
}
void AI_SimResource::WorkerReg::operator= (WorkerReg&& r) noexcept
{
	std::swap(res,  r.res);
	std::swap(rate, r.rate);
	std::swap(tmo,  r.tmo);
	std::swap(was_in_range, r.was_in_range);
}
bool AI_SimResource::WorkerReg::step(Value& vres, Value& val, bool check_only)
{
	if (tmo.is_positive()) {
		tmo -= GameCore::step_len;
		return true;
	}
	tmo = TimeSpan::seconds(1);
	
	if (rate < 0)
	{
		int dt = std::min( val.capacity - val.amount, -rate );
		dt = std::min( dt, vres.amount );
		if (dt <= 0) return false;
		
		if (check_only) return true;
		vres.amount -= dt;
		val.amount += dt;
	}
	else
	{
		int dt = std::min( val.amount, rate );
		dt = std::min( dt, vres.capacity - vres.amount );
		if (dt <= 0) return false;
		
		if (check_only) return true;
		vres.amount += dt;
		val.amount -= dt;
	}
	return true;
}
AI_SimResource::WorkResult AI_SimResource::WorkerReg::process(Value& val, vec2fp pos)
{
	float r = work_range;
	bool in_range = (res->pos.dist_squ(pos) < r * r);
	
	if (!in_range) {
		was_in_range = false;
		return ResultNotInRange{ res->pos };
	}
	
	if (!was_in_range) {
		was_in_range = true;
		tmo = TimeSpan::seconds(0.5);
	}
	
	if (!step(res->val, val, false)) return ResultFinished{};
	return ResultWorking{ res->vtar };
}



AI_SimResource::WorkerReg AI_SimResource::find(GameCore& core, vec2fp origin, float radius, Type type, int rate, flags_t find_type)
{
	const LevelCtrRoom* room = nullptr;
	if (find_type & FIND_F_SAME_ROOM)
		room = core.get_lc().get_room(origin);
	
	std::vector<std::pair<AI_SimResource*, float>> rs;
	rs.reserve(64);
	
	core.get_aic().find_resource( Rectfp::from_center(origin, vec2fp::one(radius)),
	[&](AI_SimResource& p)
	{
		if (room && core.get_lc().get_room(p.pos) != room) return;
		if (p.val.type != type || !p.can_reg(rate)) return;
		rs.emplace_back( &p, p.pos.dist_squ(origin) );
	});
	
	if (rs.empty())
		return {};
	
	bool strict_nearest = (find_type & FIND_F_VALUE_MASK) == FIND_NEAREST_STRICT;
	
	size_t s = 0;
	for (size_t i = 1; i < rs.size(); ++i)
	{
		if (rs[i].second < rs[s].second && (strict_nearest || core.get_random().range_n() < nonstrict_chance))
			s = i;
	}
	return rs[s].first->reg(rate);
}
AI_SimResource::AI_SimResource(GameCore& core, Value val, vec2fp pos, vec2fp vtar)
	: core(core), val(val), pos(pos), vtar(vtar)
{
	core.get_aic().ref_resource(Rectfp::from_center(pos, vec2fp::one(work_range)), this);
}
AI_SimResource::WorkerReg AI_SimResource::reg(int rate)
{
	if (!can_reg(rate)) return {};
	return WorkerReg(*this, rate);
}
bool AI_SimResource::can_reg(int rate) const
{
	if (!rate) throw std::runtime_error("AI_SimResource::can_reg() zero rate");
	return !is_used && (
		rate > 0
		? !val.is_producer && val.amount < val.capacity
		:  val.is_producer && val.amount > 0 );
}
