#include <queue>
#include "vaslib/vas_types.hpp"
#include "path_search.hpp"

#define USE_DIAG 1

class APS_Astar : public PathSearch
{
public:
	static constexpr bool use_diag = USE_DIAG;
	
	/// Fixed-point, to handle diags
	using PathCost = uint_fast32_t;

	///
	using NodeIndex = uint_fast16_t;
	
	struct Node
	{
		bool is_pass;
		uint_fast8_t closed; // counter
#if USE_DIAG
		uint_fast8_t dir_mask;
#endif
		NodeIndex prev; // ID of parent node
	};
	struct QueueNode
	{
		NodeIndex index;
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
	
	const size_t path_cost_bits = 16; ///< number of bits in fractional part 
	const PathCost dirs_diff = 1 << path_cost_bits; ///< same as 1.0 (always)
	const PathCost dirs_diag = std::sqrt(2) * dirs_diff;
	
	
	
	PathCost calc_dist(const vec2i& pt, size_t ix)
	{
		int dy = std::abs(pt.y - int(ix / f_size.x));
		int dx = std::abs(pt.x - int(ix % f_size.x));
		
		auto mm = std::minmax(dx, dy);
		return dirs_diff * (mm.second - mm.first) + dirs_diag * mm.first;
	}
	
	void update(vec2i size, std::vector<uint8_t> cost_grid) override
	{
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
	Result rebuild_path(size_t i, size_t len)
	{
		Result r;
		r.ps.reserve(len);
		
		do {
			r.ps.emplace_back( i % f_size.x, i / f_size.x );
			i = f_ns[i].prev;
		}
		while (i != NodeIndex(-1));
		
		size_t n = r.ps.size() - 1;
		for (size_t i=0; i < r.ps.size() /2; ++i)
			std::swap(r.ps[i], r.ps[n - i]);
			
		return r;
	}
	std::pair<NodeIndex, PathCost> find_path_internal(vec2i p_src, vec2i p_dst, const Args& args)
	{
		TimeSpan t0 = TimeSpan::current();
		++debug_request_count;
		
		++closed_cou;
		
		size_t i_src = p_src.y * f_size.x + p_src.x;
		size_t i_dst = p_dst.y * f_size.x + p_dst.x;
		PathCost maxlen = (args.max_length + 2) << path_cost_bits;
		PathCost evadecost = (args.evade_cost) << path_cost_bits;
		
		auto hval = [&](size_t ix) -> PathCost {
			const PathCost hval_corr = dirs_diff /2;
			PathCost c = calc_dist(p_dst, ix);
			return c < dirs_diff ? c : c - hval_corr;
		};
		
		open_q = decltype(open_q)();
		open_q.push({ static_cast<NodeIndex>(i_src), 0, hval(i_src) });
		
		f_ns[i_src].closed = closed_cou;
		f_ns[i_src].prev = NodeIndex(-1);
		
		while (!open_q.empty())
		{
			auto qn = open_q.top();
			open_q.pop();
			
			if (qn.index == i_dst) {
				debug_time += TimeSpan::current() - t0;
				return {i_dst, (qn.cost >> path_cost_bits) + 2};
			}
			
			if (qn.cost > maxlen)
				continue;
			
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
				if (args.evade && calc_dist(*args.evade, qn.index) <= (unsigned) args.evade_radius) c += evadecost;
				open_q.push({ static_cast<NodeIndex>(n_ix), c, c + hval(n_ix) });
			}
		}
		
		debug_time += TimeSpan::current() - t0;
		return {(NodeIndex) -1, 0};
	}
	Result find_path(Args args) override
	{
		auto res = find_path_internal(args.src, args.dst, args);
		if (res.first == (NodeIndex) -1) return {};
		return rebuild_path(res.first, res.second);
	}
	size_t find_length(Args args) override
	{
		auto res = find_path_internal(args.src, args.dst, args);
		if (res.first == (NodeIndex) -1) return size_t_inval;
		return res.second;
	}
};
PathSearch* PathSearch::create() {
	return new APS_Astar;
}
