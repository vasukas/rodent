#include <SDL2/SDL_scancode.h>
#include "render/control.hpp"
#include "render/ren_imm.hpp"
#include "render/ren_text.hpp"
#include "vaslib/vas_log.hpp"
#include "dbg_menu.hpp"



class DbgMenu_Impl : public DbgMenu
{
public:
	std::vector<Section> ss;
	size_t sel = (size_t) -1;
	
	std::vector<char> keys;
	std::string out_str;
	
	int b_cou;
	bool is_sel;
	
	int b_index = -1;
	TimeSpan b_left;
	
	DbgMenuGroup c_grp = DBGMEN_ETC;
	
	
	
	bool has_key(char c) {return std::find(keys.begin(), keys.end(), c) != keys.end();}
	RAII_Guard reg(Section sec)
	{
		auto it = std::find_if(ss.begin(), ss.end(), [&](auto&& v){return v.name == sec.name;});
		if (it != ss.end())
			LOG_THROW_X("DbgMenu::reg() name already taken: {}", sec.name);
		
		ss.emplace_back( std::move(sec) );
		return RAII_Guard([this, i = ss.size() - 1] {ss[i].proc = {}; });
	}
	void label(std::string_view s)
	{
		out_str += s;
		out_str += '\n';
	}
	bool button(std::string_view s, char hotkey)
	{
		bool ok = false;
		if (is_sel && has_key(hotkey))
		{
			ok = true;
			b_index = b_cou;
			b_left = TimeSpan::seconds(1.);
		}
		
		out_str += '[';
		out_str += (b_index == b_cou) ? '@' : (hotkey? hotkey : ' ');
		out_str += ' ';
		out_str += s;
		out_str += "]\n";
		
		++b_cou;
		return ok;
	}
	void render(TimeSpan passed, bool has_input)
	{
		b_cou = 0;
		if (b_left >= TimeSpan::ms(0)) {
			b_left -= passed;
			if (b_left < TimeSpan::ms(0))
				b_index = -1;
		}
		
		if      (has_key('\033')) sel = (size_t) -1;
		else if (has_key('1')) c_grp = DBGMEN_ETC;
		else if (has_key('2')) c_grp = DBGMEN_RENDER;
		
		out_str += "=== ";
		out_str += (c_grp == DBGMEN_ETC) ? '@' : '1';
		out_str += " other === ";
		out_str += (c_grp == DBGMEN_RENDER) ? '@' : '2';
		out_str += " render === ";
		if (!has_input) out_str += "INPUT CAPTURE DISABLED";
		out_str += "\n\n";
		
		for (size_t i=0; i<ss.size(); ++i)
		{
			auto& s = ss[i];
			if (!s.proc || s.group != c_grp) continue;
			
			if (s.hotkey && has_key(s.hotkey)) sel = i;
			is_sel = (i == sel) || (!s.hotkey && sel == (size_t) -1);
			
			out_str += "=== ";
			out_str += is_sel? '@' : (s.hotkey? s.hotkey : ' ');
			out_str += ' ';
			out_str += s.name;
			out_str += " ===\n";
			
			s.proc();
			out_str += '\n';
		}
		
		vec2i sz = RenderControl::get_size();
		RenImm::get().draw_rect({0,0,sz.x,sz.y}, 0xc0);
		RenImm::get().draw_text({}, out_str, -1, false, 1.f, FontIndex::Debug);
		
		keys.clear();
		out_str.clear();
	}
	void on_key(int scan)
	{
		char c = 0;
		if      (scan <= SDL_SCANCODE_Z && scan >= SDL_SCANCODE_A) c = 'a' + (scan - SDL_SCANCODE_A);
		else if (scan <= SDL_SCANCODE_9 && scan >= SDL_SCANCODE_1) c = '1' + (scan - SDL_SCANCODE_1);
		else if (scan == SDL_SCANCODE_ESCAPE) c = '\033';
		if (c) keys.push_back(c);
	}
};
bool DbgMenu::checkbox(bool& flag, std::string_view s, char hotkey)
{
	bool ok = button(std::string(s) + (flag? ": +" : ": -"), hotkey);
	if (ok) flag = !flag;
	return ok;
}
DbgMenu& DbgMenu::get() {
	static DbgMenu* rni = new DbgMenu_Impl;
	return *rni;
}
