#include <SDL2/SDL_events.h>
#include <SDL2/SDL_clipboard.h>
#include "render/control.hpp"
#include "render/ren_imm.hpp"
#include "render/ren_text.hpp"
#include "vaslib/vas_log.hpp"
#include "vaslib/vas_string_utils.hpp"
#include "console.hpp"

#define OFFSET_SIZE (per_line() - 3)



Console::Cmd::Cmd( std::string_view s, AuthLevel auth )
    : args( string_split(s, {" "}) ), auth(auth)
{}
bool Console::Cmd::int_arg  ( size_t i, int&   n )
{
	if (++i == args.size()) return false;
	return string_atoi( args[i], n );
}
bool Console::Cmd::float_arg( size_t i, float& n )
{
	if (++i == args.size()) return false;
	return string_atof( args[i], n );
}



class Console_Impl : public Console
{
public:
	struct Cb
	{
		Handler f;
		std::string name;
		AuthLevel auth;
		bool on;
	};
	std::vector <Cb> hns;
	
	static const int out_size = 1024;
	std::string out_buf [out_size];
	size_t out_ptr = 0;
	
	std::string in_buf;
	size_t in_ptr = 0;
	
	size_t out_shown = 0;
	int out_offset = 0;
	
	
	
	size_t reg( Handler f, const char *name, AuthLevel auth )
	{
		size_t i = hns.size();
		hns.emplace_back() = { f, name, auth, true };
		return i;
	}
	void unreg( size_t i )
	{
		hns[i].f = nullptr;
		hns[i].on = false;
	}
	void pause( size_t i, bool on )
	{
		hns[i].on = on;
	}
	void print( std::string_view str, bool newline = false )
	{
		auto nl = [&]()
		{
			if (out_shown == out_ptr)
				out_shown = (out_shown + 1) % out_size;
			
			out_ptr = (out_ptr + 1) % out_size;
			out_buf[out_ptr].clear();
		};
		
		size_t ptr = 0;
		while (true)
		{
			auto& s = out_buf[out_ptr];
			
			size_t i = ptr;
			for (; i < str.length(); ++i) if (str[i] == '\n') break;
			size_t n = i - ptr;
			
			s.reserve( n );
			s.insert( 0, str.data() + ptr, n );
			
			if (i == str.length())
			{
				if (newline) nl();
				break;
			}
			
			ptr = i + 1;
			nl();
		}
	}
	bool exec( std::string_view str, AuthLevel auth )
	{
		Cmd c( str, auth );
		if (c.args.empty()) return false;
		
		print( FMT_FORMAT( "> exec: {}\n", str ) );
		return exec_low( c );
	}
	void on_event( SDL_Event& ev )
	{
		auto insert = [this]( const char *s )
		{
			size_t n = strlen( s );
			in_buf.insert( in_ptr, s, n );
			in_ptr += n;
		};
		
		if		(ev.type == SDL_KEYDOWN)
		{
			int key = ev.key.keysym.sym;
			int scan = ev.key.keysym.scancode;
			
			if (ev.key.keysym.mod & KMOD_CTRL)
			{
				if		(scan == SDL_SCANCODE_U) in_buf.erase( in_ptr );
				else if (scan == SDL_SCANCODE_C) SDL_SetClipboardText( in_buf.c_str() );
				else if (scan == SDL_SCANCODE_V)
				{
					auto p = SDL_GetClipboardText();
					if (p) insert( p );
					SDL_free( p );
				}
				else if (key == SDLK_LEFT)  out_offset = std::max( 0, out_offset - OFFSET_SIZE );
				else if (key == SDLK_RIGHT) out_offset += OFFSET_SIZE;
				else if (key == SDLK_UP)    out_offset = 0;
			}
			else if (key == SDLK_RETURN)   send_str();
			else if (key == SDLK_END)      out_shown = out_ptr;
			else if (key == SDLK_PAGEUP)   out_shown = (out_shown - per_page()) % out_size;
			else if (key == SDLK_PAGEDOWN) out_shown = (out_shown + per_page()) % out_size;
			else if (key == SDLK_LEFT)   { if (in_ptr)                  --in_ptr; }
			else if (key == SDLK_RIGHT)  { if (in_ptr != in_buf.size()) ++in_ptr; }
			else if (key == SDLK_HOME)     in_ptr = 0;
			else if (key == SDLK_DOWN)   { in_buf.clear(); in_ptr = 0; }
			else if (key == SDLK_BACKSPACE)
			{
				if (!in_buf.empty() && in_ptr)
				{
					--in_ptr;
					in_buf.erase( in_ptr, 1 );
				}
			}
			else if (key == SDLK_INSERT)
			{
				auto p = SDL_GetClipboardText();
				if (p)
				{
					in_buf = p;
					in_ptr = in_buf.size();
				}
				SDL_free( p );
			}
		}
		else if (ev.type == SDL_TEXTINPUT)
		{
			auto& s = ev.text.text;
			if (s[0] == '`' && s[1] == 0) return;
			insert( s );
		}
	}
	void render()
	{
		int ppg = per_page();
		int lnh = RenText::get().line_height( FontIndex::TUI );
		int lwd = RenderControl::get().get_size().x;
		
		RenImm::get().draw_rect({ 0, 0, lwd, (lnh + 1) * ppg + 1 }, 0x60);
		
		TextRenderInfo tri;
		auto strout = [&]( const std::string& str, int offset, float at_y )
		{
			int len = str.length();
			len -= offset;
			if (len <= 0) return;
			
			tri.str_a = str.data() + offset;
			tri.length = len;
			tri.build();
			RenImm::get().draw_text( vec2fp(0, at_y), tri, -1 );
		};
		
		size_t i0 = out_shown - ppg + 1;
		for (int y = 0; y < ppg; ++y)
		{
			size_t i = (i0 + y) % out_size;
			strout( out_buf[i], out_offset, y * lnh );
		}
		
		int in_offset = in_ptr / OFFSET_SIZE;
		if (true)
		{
			int scr_ptr = in_ptr;
			scr_ptr -= in_offset;
			
			int cw = RenText::get().width_mode( FontIndex::TUI );
			RenImm::get().draw_rect({ 0, ppg * lnh, lwd, lnh }, 0x20702080 );
			RenImm::get().draw_rect({ cw * scr_ptr, ppg * lnh, cw, lnh }, 0x80000080 );
		}
		else RenImm::get().draw_rect({ 0, ppg * lnh, lwd, lnh }, 0x40404080 );
		
		strout( in_buf, in_offset, ppg * lnh );
	}
	
	
	
	size_t per_page()
	{
		int scr_h = RenderControl::get().get_size().y;
		return (scr_h / 2) / RenText::get().line_height( FontIndex::TUI );
	}
	int per_line()
	{
		int scr_w = RenderControl::get().get_size().x;
		return scr_w / RenText::get().width_mode( FontIndex::TUI );
	}
	void send_str()
	{
		Cmd c( in_buf, AUTH_LOCAL );
		
		in_ptr = 0;
		if (c.args.empty())
		{
			in_buf.clear();
			return;
		}
		
		print( FMT_FORMAT( "> cmd: {}\n", in_buf ) );
		in_buf.clear();
		
		exec_low( c );
	}
	bool exec_low( Cmd& c )
	{
		bool auth_err = false;
		for (auto& h : hns)
		{
			if (!h.on || !h.f) continue;
			if (h.auth > c.auth)
			{
				auth_err = true;
				continue;
			}
			auto ret = h.f( c );
			
			if (ret == H_OK) return true;
			if (ret == H_AUTH)
			{
				print( "> Insufficient auth level\n" );
				return false;
			}
			if (ret == H_ERR)
			{
				print( FMT_FORMAT( "> Invalid command for {}\n", h.name ) );
				return false;
			}
		}
		
		if (auth_err)
		{
			print( "> Insufficient auth level\n" );
			return false;
		}
		print( "> Unknown command\n" );
		return false;
	}
};
const char *Console::get_msg(HandleRet ret)
{
	switch (ret)
	{
	case H_OK: return "> Successful";
	case H_UNK: return "> Unknown command";
	case H_ERR: return "> Execution error";
	case H_AUTH: return "> Insufficient auth level";
	}
	return "> Unknown return code";
}
Console& Console::get()
{
	static Console* con = new Console_Impl;
	return *con;
}



ConsoleHandler::ConsoleHandler()
{
	i = std::string::npos;
}
ConsoleHandler::ConsoleHandler(const char *name, Console::AuthLevel auth, Console::Handler h)
{
	i = Console::get().reg([this](auto& c){ return func? func(c) : Console::H_UNK; }, name, auth);
	func = h;
}
ConsoleHandler::ConsoleHandler(ConsoleHandler&& ch)
{
	i = ch.i;
	func = std::move(ch.func);
}
ConsoleHandler& ConsoleHandler::operator=(ConsoleHandler&& ch)
{
	std::swap(i, ch.i);
	func = std::move(ch.func);
	return *this;
}
ConsoleHandler::~ConsoleHandler()
{
	if (i != std::string::npos)
		Console::get().unreg(i);
}
