#include <mutex>
#include <queue>
#include <thread>
#include "vaslib/vas_containers.hpp"
#include "vaslib/vas_types.hpp"
#include "path_search.hpp"

#define USE_DIAG 1

class APS_Astar : public AsyncPathSearch
{
public:
	static constexpr bool use_diag = USE_DIAG;
	
	struct Slot
	{
		enum State
		{
			ST_NONE,
			ST_WAITING,
			ST_COMPUTE,
			ST_RESULT,
			ST_COMPUTE_RESET
		};

		State state = ST_NONE;
		vec2i pa, pb;
		Result res;
		
		operator bool() const {return state != ST_NONE;}
	};
	
	bool thr_term = false;
	std::mutex mux;
	SparseArray<Slot> ss;
	std::thread thr;
	
	/// Fixed-point, to handle diags
	using PathCost = uint_fast32_t;
	
	struct Node
	{
		bool is_pass;
		uint_fast8_t closed; // counter
		uint_fast16_t prev; // ID of parent node
#if USE_DIAG
		uint_fast8_t dir_mask;
#endif
	};
	struct QueueNode
	{
		uint_fast16_t index;
		PathCost cost; // g-value
		PathCost weight; // f = g + h
		
		bool operator > (const QueueNode& n) const {return weight > n.weight;}
	};
	
	vec2i f_size = {};
	std::vector<Node> f_ns;
	
	struct DirOff
	{
		ssize_t offset;
		PathCost diff;
	};
	std::array <DirOff, use_diag? 8 : 4> dirs; // neighbour offset + cost diff
	
	std::priority_queue <QueueNode, std::vector<QueueNode>, std::greater<QueueNode>> open_q;
	uint_fast8_t closed_cou = 0;
	
	const PathCost dirs_diff = 0x10000; // also same as 1.0
	const PathCost dirs_diag = std::sqrt(2) * dirs_diff;
	
	
	
	APS_Astar()
	    : thr([this]{thr_func();})
	{}
	~APS_Astar()
	{
		thr_term = true;
		if (thr.joinable())
			thr.join();
	}
	void update(vec2i size, std::vector<uint8_t> cost_grid, int) override
	{
		std::unique_lock lock(mux);
		f_size = size;
		f_ns.resize (cost_grid.size());
		
		for (size_t i=0; i < cost_grid.size(); ++i)
		{
			f_ns[i].is_pass = (cost_grid[i] != 0);
			f_ns[i].closed = 0;
#if USE_DIAG
			f_ns[i].dir_mask = 0;
#endif
		}
		closed_cou = 0;
		
#if USE_DIAG
		auto cell = [&](int x, int y) -> auto& {return f_ns[y * f_size.x + x];};
		
		for (int y=1; y < size.y - 1; ++y)
		for (int x=1; x < size.x - 1; ++x)
		{
			auto& dm = cell(x, y).dir_mask;
			if (!cell(x, y-1).is_pass) dm |= (1 << 0) | (1 << 2);
			if (!cell(x, y+1).is_pass) dm |= (1 << 5) | (1 << 7);
			if (!cell(x-1, y).is_pass) dm |= (1 << 0) | (1 << 5);
			if (!cell(x+1, y).is_pass) dm |= (1 << 2) | (1 << 7);
		}
#endif
			
		ssize_t pt = f_size.x;
#if USE_DIAG
		dirs = {{
			{-pt -1, dirs_diag}, {-pt, dirs_diff}, {-pt +1, dirs_diag}, // 0 1 2
			{    -1, dirs_diff},                   {    +1, dirs_diff}, // 3   4
			{ pt -1, dirs_diag}, { pt, dirs_diff}, { pt +1, dirs_diag}  // 5 6 7
		}};
#else
		dirs = {{
							 {-pt, dirs_diff},
			{-1, dirs_diff},                   {1, dirs_diff},
							 { pt, dirs_diff}
		}};
#endif
	}
	size_t add_task(vec2i from, vec2i to) override
	{
		std::unique_lock lock(mux);
		size_t i = ss.new_index();
		
		auto& s = ss[i];
		s.pa = from;
		s.pb = to;
		s.state = Slot::ST_WAITING;
		
		return i;
	}
	void rem_task(size_t index) override
	{
		std::unique_lock lock(mux);
		auto& s = ss[index];
		
		if (s.state == Slot::ST_COMPUTE)
			s.state = Slot::ST_COMPUTE_RESET;
		else {
			s.state = Slot::ST_NONE;
			ss.free_index(index);
		}
	}
	std::optional<Result> get_task(size_t index) override
	{
		std::unique_lock lock(mux);
		auto& s = ss[index];
		
		if (s.state == Slot::ST_RESULT)
		{
			auto res = std::move(s.res);
			s.state = Slot::ST_NONE;
			ss.free_index(index);
			return res;
		}
		return {};
	}
	void thr_func()
	{
		while (!thr_term)
		{
			if (!ss.existing_count())
				sleep(sleep_time);
			
			// find waiting slot
			
			std::unique_lock lock(mux);
			size_t index = size_t_inval;
			
			auto it_end = ss.end();
			for (auto it = ss.begin(); it != it_end; ++it)
			{
				if (it->state == Slot::ST_WAITING)
				{
					index = it.index();
					break;
				}
			}
			
			if (index == size_t_inval)
				continue;
			
			// prepare
			
			Slot* s = &ss[index];
			vec2i p_src = s->pa;
			vec2i p_dst = s->pb;
			s->state = Slot::ST_COMPUTE;
			
			// non-blocking calc
			
			lock.unlock();
			auto res = calc_path(p_src, p_dst);
			lock.lock();
			
			// set result
			
			s = &ss[index];
			if (s->state == Slot::ST_COMPUTE_RESET)
			{
				s->state = Slot::ST_NONE;
				ss.free_index(index);
				continue;
			}
			
			s->res = std::move(res);
			s->state = Slot::ST_RESULT;
		}
	}
	Result rebuild_path(size_t i, size_t len)
	{
		Result r;
		r.ps.reserve(len);
		
		do {
			r.ps.emplace_back( i % f_size.x, i / f_size.x );
			i = f_ns[i].prev;
		}
		while (f_ns[i].prev != size_t_inval);
		
		size_t n = r.ps.size() - 1;
		for (size_t i=0; i < r.ps.size() /2; ++i)
			std::swap(r.ps[i], r.ps[n - i]);
			
		return r;
	}
	Result calc_path(vec2i p_src, vec2i p_dst)
	{
		++closed_cou;
		
		size_t i_src = p_src.y * f_size.x + p_src.x;
		size_t i_dst = p_dst.y * f_size.x + p_dst.x;
		
		auto hval = [&](size_t ix) -> PathCost
		{
			int dy = p_dst.y - (ix / f_size.x);
			int dx = p_dst.x - (ix % f_size.x);
			return std::round( std::sqrt(dx*dx + dy*dy) * dirs_diff );
		};
		
		open_q = decltype(open_q)();
		open_q.push({ i_src, 0, hval(i_src) });
		
		f_ns[i_src].closed = closed_cou;
		f_ns[i_src].prev = size_t_inval;
		
		while (!open_q.empty())
		{
			auto qn = open_q.top();
			open_q.pop();
			
			if (qn.index == i_dst)
				return rebuild_path(i_dst, qn.cost + 2);
			
#if USE_DIAG
			int dir_bit = 1;
#endif
			for (auto& d : dirs)
			{
#if USE_DIAG
				bool no_dir = f_ns[qn.index].dir_mask & dir_bit;
				dir_bit <<= 1;
				if (no_dir) continue;
#endif
				
				size_t n_ix = qn.index + d.offset;
				auto& n = f_ns[n_ix];
				if (!n.is_pass || n.closed == closed_cou) continue;
				
				n.closed = closed_cou;
				n.prev = qn.index;
				
				PathCost c = qn.cost + d.diff;
				open_q.push({ n_ix, c, c + hval(n_ix) });
			}
		}
		return {};
	}
};
AsyncPathSearch* AsyncPathSearch::create_default() {
	return new APS_Astar;
}
