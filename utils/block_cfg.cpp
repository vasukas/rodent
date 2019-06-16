#include <functional>
#include "vaslib/vas_log.hpp"
#include "block_cfg.hpp"

bool bc_parse( BC_Block& top, std::string_view str_v, int spaces_per_tab )
{
	const char *str = str_v.data();
	size_t left = str_v.length();
	BC_Block* cur = &top;
	
	for (size_t line_c = 1; left != 0; )
	{
		int tab_cou = 0;
		int space_cou = 0;
		
		for (size_t i = 0 ;; ++i)
		{
			if (i == left) return true;
			if (str[i] == '\t')
			{
				++tab_cou;
				space_cou = 0;
			}
			else if (str[i] == ' ')
			{
				if (++space_cou == spaces_per_tab)
				{
					++tab_cou;
					space_cou = 0;
				}
			}
			else
			{
				if (space_cou) ++tab_cou;
				str += i;
				left -= i;
				break;
			}
		}
		
		BC_Line ln;
		ln.args.reserve( 8 );
		
		bool arg = false;
		bool quoted = false;
		bool esc_enc = false;
		
		for (size_t i = 0 ;; ++i)
		{
			if (i == left || str[i] == '\n' || (str[i] == '#' && !quoted))
			{
				if (quoted || esc_enc)
				{
					VLOGE( "bc_parse() newline before end of string at #{}", line_c );
					return false;
				}
				
				str += i;
				left -= i;
				break;
			}
			else if (str[i] == ' ' && !quoted)
			{
				if (esc_enc)
				{
					VLOGE( "bc_parse() broken escape sequence at #{}", line_c );
					return false;
				}
				arg = false;
			}
			else
			{
				if (!esc_enc)
				{
					if		(str[i] == '\\') { esc_enc = true;   continue; }
					else if (str[i] == '"')  { quoted = !quoted; continue; }
				}
				else
				{
					esc_enc = false;
					if (str[i] != '\\' && str[i] != '"')
						VLOGW( "bc_parse() unknown escape sequence at #{}", line_c );
				}
				
				if (!arg)
				{
					arg = true;
					ln.args.emplace_back().reserve( 64 );
				}
				ln.args.back().push_back( str[i] );
			}
		}
		
		size_t line_new = line_c + 1;
		
		if (left && str[0] == '#')
		{
			if (left > 1 && str[1] == '{')
			{
				int level = 1;
				int ln_passed = 0;
				
				for (size_t i = 2 ;; ++i)
				{
					if (i == left)
					{
						str += i;
						left -= i;
						break;
					}
					if (left - i > 1)
					{
						if		(str[i] == '#' && str[i+1] == '{') ++level;
						else if (str[i] == '}' && str[i+1] == '#')
						{
							if (--level == 0)
							{
								str += i;
								left -= i;
								break;
							}
						}
					}
					if (str[i] == '\n')
						++ln_passed;
				}
				
				if (level)
					VLOGW( "bc_parse() multi-line comment not closed, beginning at #{}", line_c );
				
				line_new += ln_passed;
			}
			
			for (size_t i = 0 ;; ++i)
			{
				if (i == left || str[i] == '\n')
				{
					str += i;
					left -= i;
					break;
				}
			}
		}
		
		ln.line = line_c;
		line_c = line_new;
		if (left) { ++str; --left; }
		if (ln.args.empty()) continue;
		
		if		(tab_cou > cur->tab_level)
		{
			if (cur->ls.empty())
			{
				VLOGE( "bc_parse() invalid indentation at #{}", ln.line );
				return false;
			}
			
			cur = &cur->ls.back().block;
			cur->tab_level = tab_cou;
		}
		else if (tab_cou < cur->tab_level)
		{
			std::function< BC_Block*(BC_Block&,int) > find =
			[ &find ]( BC_Block& top, int tab_cou ) -> BC_Block*
			{
				if (tab_cou == top.tab_level) return &top;
				if (tab_cou <  top.tab_level) return nullptr;
				if (top.ls.empty()) return nullptr;
				return find( top.ls.back().block, tab_cou );
			};
			
			cur = find( top, tab_cou );
			if (!cur)
			{
				VLOGE( "bc_parse() invalid indentation at #{}", ln.line );
				return false;
			}
		}
		
		cur->ls.emplace_back() = std::move( ln );
	}
	return true;
}
size_t bc_dump( const BC_Block& top, std::string& str, int spaces_per_tab, size_t first_line )
{
	for (auto& b : top.ls)
	{
		size_t skip = b.line - first_line;
		str.insert( str.end(), skip, '\n' );
		
		if (top.tab_level > 0)
		{
			if (!spaces_per_tab)
				str.insert( str.end(), top.tab_level, '\t' );
			else
				str.insert( str.end(), top.tab_level * spaces_per_tab, ' ' );
		}
		
		for (auto& arg : b.args)
		{
			bool quote = false;
			for (auto& c : arg)
			{
				if (c == ' ')
				{
					quote = true;
					break;
				}
			}
			
			if (quote) str.push_back( '"' );
			for (auto& c : arg)
			{
				if (c == '"' || c == '\\') str.push_back('\\');
				str.push_back(c);
			}
			if (quote) str.push_back('"');
			
			str.push_back(' ');
		}
		str.back() = '\n';
		first_line = b.line + 1;
		
		if (!b.block.ls.empty())
			first_line = bc_dump( b.block, str, spaces_per_tab, first_line );
	}
	return first_line;
}



#include "vaslib/vas_string_utils.hpp"

enum
{
	BCAT_FIXED,
	BCAT_OPTPREF,
	BCAT_INT,
	BCAT_FLOAT,
	BCAT_STR
};

BC_Cmd::BC_Cmd( bool optional, bool single, std::string name, std::function <bool()> cb )
	: opt(optional), single(single), name(std::move(name)), cb(std::move(cb))
{
}
void BC_Cmd::add( BC_Cmd c )
{
	cmds.emplace_back( std::move(c) );
}
void BC_Cmd::arg( std::string name )
{
	if (name.empty())
	{
		VLOGE( "BC_Cmd::arg() empty name" );
		return;
	}
	
	auto& a = args.emplace_back();
	a.pref = std::move(name);
	a.type = BCAT_FIXED;
}
void BC_Cmd::arg( std::string name, size_t val_count )
{
	if (name.empty())
	{
		VLOGE( "BC_Cmd::arg() empty name" );
		return;
	}
	
	auto& a = args.emplace_back();
	a.pref = std::move(name);
	a.vb = nullptr;
	a.type = BCAT_OPTPREF;
	a.skip = val_count;
}
void BC_Cmd::arg( std::string name, size_t val_count, bool& exists )
{
	if (name.empty())
	{
		VLOGE( "BC_Cmd::arg() empty name" );
		return;
	}
	
	auto& a = args.emplace_back();
	a.pref = std::move(name);
	a.vb = &exists;
	a.type = BCAT_OPTPREF;
	a.skip = val_count;
}
void BC_Cmd::val( int& value )
{
	auto& a = args.emplace_back();
	a.vi = &value;
	a.type = BCAT_INT;
}
void BC_Cmd::val( float& value )
{
	auto& a = args.emplace_back();
	a.vf = &value;
	a.type = BCAT_FLOAT;
}
void BC_Cmd::val( std::string& value )
{
	auto& a = args.emplace_back();
	a.vs = &value;
	a.type = BCAT_STR;
}
bool bc_process( const BC_Block& top, std::vector <BC_Cmd> cmds )
{
	for (auto& cm : cmds) cm.already = false;
	for (auto& ln : top.ls)
	{
		size_t i = 0;
		for (; i < cmds.size(); ++i)
		{
			if (cmds[i].name == ln.args[0])
				break;
		}
		if (i == cmds.size())
		{
			VLOGE( "bc_process() unknown command: \"{}\" at #{}", ln.args[0], ln.line );
			return false;
		}
		
		
		auto& cm = cmds[i];
		if (ln.block.ls.empty() != cm.cmds.empty())
		{
			VLOGE( "bc_process() block requrement not met for command: \"{}\" at #{}", cm.name, ln.line );
			return false;
		}
		if (cm.single && cm.already)
		{
			VLOGE( "bc_process() command already used: \"{}\" at #{}", cm.name, ln.line );
			return false;
		}
		cm.already = true;
		
		
		size_t arg_i = 0;
		for (size_t i = 1; i < ln.args.size(); ++i, ++arg_i)
		{
			if (arg_i == cm.args.size())
			{
				VLOGE( "bc_process() too much arguments for command: \"{}\" at #{}", cm.name, ln.line );
				return false;
			}
			
			auto& arg = ln.args[i];
			auto& typ = cm.args[ arg_i ];
			
			if (!typ.pref.empty())
			{
				if (typ.pref != arg)
				{
					if		(typ.type == BCAT_FIXED)
					{
						VLOGE( "bc_process() non-optional argument \"{}\" not used for command: \"{}\" at #{}", typ.pref, cm.name, ln.line );
						return false;
					}
					else if (typ.type == BCAT_OPTPREF)
					{
						if (typ.vb) *typ.vb = false;
						arg_i += typ.skip;
						--i;
					}
				}
				else if (typ.type == BCAT_OPTPREF && typ.vb)
					*typ.vb = true;
			}
			else if (typ.type == BCAT_INT)
			{
				if (!string_atoi( arg, *typ.vi ))
				{
					VLOGE( "bc_process() invalid value for command: \"{}\" at #{}", cm.name, ln.line );
					return false;
				}
			}
			else if (typ.type == BCAT_FLOAT)
			{
				if (!string_atof( arg, *typ.vf ))
				{
					VLOGE( "bc_process() invalid value for command: \"{}\" at #{}", cm.name, ln.line );
					return false;
				}
			}
			else if (typ.type == BCAT_STR)
			{
				*typ.vs = arg;
			}
		}
		if (arg_i != cm.args.size())
		{
			VLOGE( "bc_process() not enough arguments for command: \"{}\" at #{}", cm.name, ln.line );
			return false;
		}
		
		
		if (!cm.cmds.empty() && !bc_process( ln.block, cm.cmds ))
		{
			VLOGE( "bc_process() block processing failed for command: \"{}\" at #{}", cm.name, ln.line );
			return false;
		}
		if (cm.cb && !cm.cb())
		{
			VLOGE( "bc_process() callback failed for command: \"{}\" at #{}", cm.name, ln.line );
			return false;
		}
	}
	for (auto& cm : cmds)
	{
		if (!cm.opt && !cm.already)
		{
			VLOGE( "bc_process() non-optional command not used: \"{}\"", cm.name );
			return false;
		}
	}
	return true;
}



#include "vaslib/vas_file.hpp"

bool bc_parsefile( const char *filename, std::vector <BC_Cmd> cmds, int spaces_per_tab )
{
	auto str = readfile( filename );
	if (!str)
	{
		VLOGE( "bc_parsefile() failed" );
		return false;
	}
	
	BC_Block top;
	if (!bc_parse( top, *str, spaces_per_tab ))
	{
		VLOGE( "bc_parsefile() failed" );
		return false;
	}
	if (!bc_process( top, std::move(cmds) ))
	{
		VLOGE( "bc_parsefile() failed" );
		return false;
	}
	return true;
}
bool bc_dumpfile( const char *filename, const BC_Block& top, int spaces_per_tab )
{
	std::string s;
	bc_dump( top, s, spaces_per_tab );
	return writefile( filename, s.data(), s.length() );
}
