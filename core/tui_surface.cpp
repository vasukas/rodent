#include <unordered_map>
#include "render/ren_text.hpp"
#include "tui_surface.hpp"

#define BOXDRAW_CHAR_LIST\
	X(1,1,0,0, 0x2500, 0x2501, 0xc4, '-')\
	X(0,0,1,1, 0x2502, 0x2503, 0xb3, '|')\
	X(1,1,1,1, 0x253c, 0x254b, 0xc5, '+')\
	\
	X(0,1,0,1, 0x256d, 0x250c, 0x250f, 0xda, '/')\
	X(1,0,0,1, 0x256e, 0x2510, 0x2513, 0xbf, '\\')\
	X(0,1,1,0, 0x2570, 0x2514, 0x2517, 0xc0, '\\')\
	X(1,0,1,0, 0x256f, 0x2518, 0x251b, 0xd9, '/')\
	\
	X(0,1,1,1, 0x251c, 0x2523, 0xc3, '|')\
	X(1,0,1,1, 0x2524, 0x252b, 0xb4, '|')\
	X(1,1,0,1, 0x252c, 0x2533, 0xc2, '-')\
	X(1,1,1,0, 0x2534, 0x253b, 0xc1, '-')

#define BOXDRAW_CHAR_FLAGS(L,R,U,D) ((L<<3)|(R<<2)|(U<<1)|(D))

char32_t boxdraw_char(bool left, bool right, bool up, bool down)
{
#define X(L,R,U,D, G, ...) {BOXDRAW_CHAR_FLAGS(L,R,U,D), G},
	static const std::unordered_map<int, char32_t> map = {
	    BOXDRAW_CHAR_LIST
	};
#undef X
	auto it = map.find(BOXDRAW_CHAR_FLAGS(left, right, up, down));
	return it != map.end() ? it->second : ' ';
}

// called from render/ren_text.cpp
std::vector<std::vector<char32_t>> tui_char_get_alts()
{
	std::vector<std::vector<char32_t>> alts;
	
	alts.push_back({ SCH_BULLET, 0x07, '*' });
	alts.push_back({ SCH_CHECKER_LIGHT, 0xb0, ' ' });
	alts.push_back({ SCH_CHECKER_MED,   0xb1, ' ' });
	alts.push_back({ SCH_CHECKER_HEAVY, 0xb2, '#' });
	alts.push_back({ SCH_BLOCK_FULL, 0xdb, '#' });
	
	alts.push_back({ SCH_POINT_RIGHT, 0x10, '>' });
	alts.push_back({ SCH_POINT_LEFT,  0x11, '<' });
	alts.push_back({ SCH_POINT_UP,    0x1e, '^' });
	alts.push_back({ SCH_POINT_DOWN,  0x1f, 'v' });
	
#define X(L,R,U,D, G, ...) alts.push_back({G, __VA_ARGS__});
	BOXDRAW_CHAR_LIST
#undef X
	        
	return alts;
}



TUI_Surface::TUI_Surface(vec2i size, TUI_Char with)
{
	resize_clear(size, with);
}
void TUI_Surface::clear(TUI_Char with)
{
	for (auto& c : cs) c = with;
	upd_any = true;
}
void TUI_Surface::resize_clear(vec2i new_size, TUI_Char with)
{
	size = new_size;
	cs.resize( size.area() );
	clear(with);
}
void TUI_Surface::resize_keep(vec2i new_size)
{
	std::vector<TUI_Char> n_cs;
	n_cs.resize( new_size.area() );
	
	vec2i mz = min(size, new_size);
	for (int y=0; y<mz.y; ++y)
	for (int x=0; x<mz.x; ++x)
		n_cs[y * new_size.x + x] = cs[y * size.x + x];
	
	cs = std::move(n_cs);
	size = new_size;
	
	upd_any = true;
}
void TUI_Surface::set_from(vec2i off, TUI_Surface* src, vec2i src_pos, vec2i size)
{
	if (!src) return;
	for (int y=0; y<size.y; ++y)
	for (int x=0; x<size.x; ++x)
	{
		vec2i sp = src_pos + vec2i{x,y};
		if (!src->in_bounds(sp)) continue;
		auto& c = src->cs[src->pti(sp)];
		if (c.sym) set(off + vec2i{x,y}, c);
	}
}
void TUI_Surface::set_from(vec2i off, TUI_Surface* src)
{
	if (src) set_from(off, src, {}, src->size);
}
bool TUI_Surface::in_bounds(vec2i pos) const
{
	return pos.x < size.x && pos.x >= 0 &&
	       pos.y < size.y && pos.y >= 0;
}
size_t TUI_Surface::pti(vec2i pos) const
{
	return pos.y * size.x + pos.x;
}
const TUI_Char& TUI_Surface::get(vec2i pos) const
{
	static TUI_Char inv;
	return in_bounds(pos) ? cs[pti(pos)] : inv;
}
void TUI_Surface::set(vec2i pos, TUI_Char ch)
{
	if (!in_bounds(pos)) return;
	cs[pti(pos)] = ch;
	upd_any = true;
}
void TUI_Surface::change(vec2i pos, TUI_Char ch)
{
	if (!in_bounds(pos)) return;
	auto& c = cs[pti(pos)];
	if (ch.sym != static_cast<char32_t>(-1)) c.sym = ch.sym;
	if (ch.fore != -1) c.fore = ch.fore;
	if (ch.back != -1) c.back = ch.back;
	if (ch.alpha >= 0.f) c.alpha = ch.alpha;
	upd_any = true;
}
void TUI_Surface::set_rect(Rect r, TUI_Char ch)
{
	for (int y = r.lower().y; y <= r.upper().y; ++y)
	for (int x = r.lower().y; x <= r.upper().x; ++x)
		set({x, y}, ch);
}
void TUI_Surface::change_rect(Rect r, TUI_Char ch)
{
	for (int y = r.lower().y; y <= r.upper().y; ++y)
	for (int x = r.lower().y; x <= r.upper().x; ++x)
		change({x, y}, ch);
}



void TUI_BoxdrawHelper::init(TUI_Surface& s)
{
	sur = &s;
	clear();
}
void TUI_BoxdrawHelper::box(Rect r)
{
	hline(r.lower().x, r.upper().x, r.lower().y);
	hline(r.lower().x, r.upper().x, r.upper().y);
	vline(r.lower().y, r.upper().y, r.lower().x);
	vline(r.lower().y, r.upper().y, r.upper().x);
}
void TUI_BoxdrawHelper::hline(int x1, int x2, int y)
{
	for (int x=x1; x<=x2; ++x) set({x, y});
}
void TUI_BoxdrawHelper::vline(int y1, int y2, int x)
{
	for (int y=y1; y<=y2; ++y) set({x, y});
}
void TUI_BoxdrawHelper::set(vec2i p)
{
	if (sur->in_bounds(p))
		flags[sur->pti(p)] = true;
}
void TUI_BoxdrawHelper::submit()
{
	std::vector<int> cs;
	vec2i sz = sur->size;
	cs.resize( sz.area() );
	
	for (int y=0; y<sz.y; ++y)
	for (int x=0; x<sz.x; ++x)
	{
		size_t i = y * sz.x + x;
		if (!flags[i]) continue;
		
		int v[4] = {};
		if (x && flags[i - 1]) v[0] = 1;
		if (x != sz.x-1 && flags[i + 1]) v[1] = 1;
		if (y && flags[i - sz.x]) v[2] = 1;
		if (y != sz.y-1 && flags[i + sz.x]) v[3] = 1;
		cs[i] = boxdraw_char(v[0], v[1], v[2], v[3]);
	}
	
	TUI_Char ch = TUI_Char::none();
	for (size_t i=0; i<cs.size(); ++i) {
		if (!cs[i]) continue;
		ch.sym = cs[i];
		sur->change(vec2i(i % sz.x, i / sz.x), ch);
	}
}
void TUI_BoxdrawHelper::clear()
{
	flags.clear();
	flags.resize( sur->size.area() );
}
