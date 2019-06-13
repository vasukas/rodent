#include "render/control.hpp"
#include "render/ren_text.hpp"
#include "vaslib/vas_log.hpp"
#include "tui_layer.hpp"



static vec2i get_screen_size()
{
	vec2i cz = RenText::get().mxc_size(FontIndex::TUI);
	return RenderControl::get_size() / cz;
}



void TUI_Layer::Field::set(TUI_Char* s, size_t n)
{
	vec2i at = {};
	for (size_t i=0; i<n; ++i) {
		if (at.x >= r.size().x) {
			for (; i<n; ++i) if (s[i].sym == '\n') break;
			if (i == n) break;
		}
		if (s[i].sym == '\n') {
			at.x = 0;
			if (++at.y == r.size().y) break;
		}
		sur.set(r.lower() + at, s[i]);
		++at.x;
	}
}
void TUI_Layer::Field::set(std::string_view str, size_t highlight)
{
	std::vector<TUI_Char> s;
	s.resize( r.size().x );
	
	for (size_t i = 0; i < str.length(); ++i) {
		auto& c = s[i];
		c.sym = str[i];
		if (i == highlight) std::swap(c.fore, c.back);
	}
	
	set(s.data(), s.size());
}
void TUI_Layer::Field::set_bar(float t, bool show_percent)
{
	(void) show_percent; // TODO
	int n = std::round(std::min(1.f, std::max(0.f, t)) * r.size().x);
	
	std::vector<TUI_Char> s;
	s.resize( r.size().x );
	
	int i=0;
	for (auto& c : s) {
		c.sym = i < n ? SCH_BLOCK_FULL : SCH_CHECKER_LIGHT;
		++i;
	}
	set(s.data(), s.size());
}



static std::vector<TUI_Layer*> stack;
static bool stack_upd = true;
static bool stack_was_empty = true;



TUI_Layer::TUI_Layer(): sur(get_screen_size()) {}
TUI_Layer::~TUI_Layer()
{
	hide();
}
TUI_Layer* TUI_Layer::get_stack_top()
{
	return stack.empty() ? nullptr : stack.back();
}
bool TUI_Layer::render_all(TUI_Surface& dst)
{
	if (stack.empty()) {
		if (!stack_was_empty) {
			stack_was_empty = true;
			dst.clear();
			return true;
		}
		return false;
	}
	stack_was_empty = false;
	
	size_t i = stack.size() - 1;
	for (; i != 0; ++i) if (!stack[i]->transparent) break;
	
	bool any = false;
	for (; i < stack.size(); ++i)
	{
		TUI_Layer* p = stack[i];
		p->render();
		if (p->sur.upd_any || stack_upd) {
			p->sur.upd_any = false;
			dst.set_from({}, &p->sur);
			any = true;
		}
	}
	
	stack_upd = false;
	return any;
}
void TUI_Layer::bring_to_top()
{
	hide();
	stack.push_back(this);
	stack_upd = true;
}
void TUI_Layer::hide()
{
	auto it = std::find(stack.begin(), stack.end(), this);
	if (it != stack.end()) {
		stack.erase(it);
		stack_upd = true;
	}
}
TUI_Layer::Field TUI_Layer::mk_field(Rect r)
{
	if (!r.size().area()) VLOGX("TUI_Layer::mk_field() zero area");
	return Field(sur, r);
}
