#include "vaslib/vas_file.hpp"
#include "vaslib/vas_log.hpp"
#include "vaslib/vas_string_utils.hpp"
#include "line_cfg.hpp"

#include <algorithm>

#undef is_space
#define is_space linecfg_is_space

LineCfgArgError LineCfgArg_Int::read(std::string_view s) {
	if (!string_atoi(s, v)) return LineCfgArgError::Fail;
	if (v >= min && v <= max) return LineCfgArgError::Ok;
	return LineCfgArgError::OutOfRange;
}
LineCfgArgError LineCfgArg_Float::read(std::string_view s) {
	if (!string_atof(s, v)) return LineCfgArgError::Fail;
	if (v > min - eps && v < max + eps) return LineCfgArgError::Ok;
	return LineCfgArgError::OutOfRange;
}
LineCfgArgError LineCfgArg_Bool::read(std::string_view s) {
	if (s == "0" || s == "false" || s == "False") {
		v = false;
		return LineCfgArgError::Ok;
	}
	else if (s == "1" || s == "true" || s == "True") {
		v = true;
		return LineCfgArgError::Ok;
	}
	return LineCfgArgError::Fail;
}
LineCfgArgError LineCfgArg_Str::read(std::string_view s) {
	v = s;
	size_t i=0;
	while (true) {
		i = v.find('"', i);
		if (i == std::string::npos) break;
		v.erase(i, 1);
	}
	return LineCfgArgError::Ok;
}
LineCfgArgError LineCfgArg_Enum::read(std::string_view s) {
	return type->read(p, s) ? LineCfgArgError::Ok : LineCfgArgError::Fail;
}

void LineCfgArg_Int::write(std::string& s) const {
	s += FMT_FORMAT("{}", v);
}
void LineCfgArg_Float::write(std::string& s) const {
	s += FMT_FORMAT("{}", v);
}
void LineCfgArg_Bool::write(std::string& s) const {
	s += v ? "true" : "false";
}
void LineCfgArg_Str::write(std::string& s) const {
	for (auto& c : s) if (is_space(c)) {
		s += '"';
		s += v;
		s += '"';
		return;
	}
	// else
	s += v;
}
void LineCfgArg_Enum::write(std::string& s) const {
	if (!type->write(p, s))
		throw std::logic_error("LineCfgArg_Enum::write() invalid value");
}

LineCfgOption& LineCfgOption::vint(int& v, int max, int min) {
	args.emplace_back(LineCfgArg_Int{v, max, min});
	return *this;
}
LineCfgOption& LineCfgOption::vfloat(float& v, float max, float min) {
	args.emplace_back(LineCfgArg_Float{v, max, min});
	return *this;
}
LineCfgOption& LineCfgOption::vbool(bool&        v) {args.emplace_back(v); return *this;}
LineCfgOption& LineCfgOption::vstr (std::string& v) {args.emplace_back(v); return *this;}
LineCfgOption& LineCfgOption::descr(std::string  v) {descr_v = std::move(v); return *this;}

void LineCfg::read_s(std::string_view str)
{
	// pre-check
	for (size_t i=0; i<opts.size(); ++i)
	for (size_t j=0; j<opts.size(); ++j)
	{
		if (i != j && opts[i].name == opts[j].name)
			THROW_FMTSTR("LineCfg::read_s() duplicate option '{}'", opts[i].name);
	}
	
	// clear
	for (auto& p : opts) p.line = 0;
	comments.clear();
	
	// loop
	int line = 1;
	size_t i_end = 0;
	while (i_end < str.size())
	{
		// skip spaces
		while (i_end < str.size() && is_space(str[i_end]))
			++i_end;
		
		// find end of line
		size_t i_str = i_end;
		while (i_end < str.size() && str[i_end] != '\n')
			++i_end;
		
		// proc arguments
		std::optional<size_t> i_beg = i_str;
		LineCfgOption* opt = nullptr;
		size_t i_arg = 0;
		
		auto end_arg = [&]{
			std::string_view s = str.substr(*i_beg, i_str - *i_beg);
			i_beg.reset();
			
			if (!opt) {
				for (auto& p : opts) {
					if (p.name == s) {
						opt = &p;
						break;
					}
				}
				if (!opt) {
					if (ignore_unknown) {
						VLOGW("LineCfg::read_s() unknown option (option '{}', line {})", s, line);
						return false;
					}
					THROW_FMTSTR("LineCfg::read_s() unknown option (option '{}', line {})", s, line);
				}
				opt->line = line;
			}
			else if (i_arg == opt->args.size()) {
				THROW_FMTSTR("LineCfg::read_s() too many arguments (option '{}', line {})", opt->name, line);
			}
			else {
				auto ret = std::visit([&](auto& v){ return v.read(s); }, opt->args[i_arg]);
				if (ret != LineCfgArgError::Ok)
					THROW_FMTSTR("LineCfg::read_s() {} (arg '{}', option '{}', line {})",
					             ret == LineCfgArgError::OutOfRange ? "value out of range" : "invalid value",
					             i_arg+1, opt->name, line);
				++i_arg;
			}
			return true;
		};
		
		// read arguments
		bool quote = false;
		while (i_str < i_end)
		{
			if (quote) {
				if (str[i_str] == '"'/* && (!i_str || str[i_str-1] != '\\')*/) // unescape not impl in LineCfgArg_Str
					quote = false;
			}
			else if (str[i_str] == '"') {
				quote = true;
				if (!i_beg)
					i_beg = i_str;
			}
			else if (str[i_str] == '#') {
				break;
			}
			else if (!i_beg) {
				if (!is_space(str[i_str]))
					i_beg = i_str;
			}
			else if (is_space(str[i_str])) {
				if (!end_arg())
					break;
			}
			
			++i_str;
		}
		
		// post-checks
		if (quote)
			THROW_FMTSTR("LineCfg::read_s() unclosed quote (line {})", line);
		
		if (i_beg && *i_beg != i_str)
			end_arg();
		
		if (opt) {
			if (i_arg != opt->args.size())
				THROW_FMTSTR("LineCfg::read_s() not enough arguments (option '{}', line {})", opt->name, line);
		}
		
		// end line
		if (save_comments)
		{
			while (i_str < i_end && str[i_str] != '#')
				++i_str;
			
			if (str[i_str] == '#')
				comments.emplace_back(line, str.substr(i_str, i_end - i_str));
		}
		
		++line;
		++i_end;
	}
	
	// post-check
	for (auto& p : opts) {
		if (!p.optional && !p.line)
			THROW_FMTSTR("LineCfg::read_s() non-optional line '{}' is not present", p.name);
	}
}
std::string LineCfg::write_s(std::string str) const
{
	str.clear();
	
	size_t opts_left = opts.size();
	if (write_only_present) {
		opts_left = 0;
		for (auto& p : opts) {
			if (p.line || p.changed)
				++opts_left;
		}
	}
	bool first_line = !write_only_present;
	
	int line = 1;
	size_t i_comm = save_comments ? 0 : comments.size();
	
	while (opts_left != 0 || i_comm < comments.size())
	{
		if (str.size() + 512 > str.capacity())
			str.reserve( str.size() + 4096 );
		
		int next = std::numeric_limits<int>::max();
		
		// add option
		bool has_opt = false;
		for (auto& p : opts)
		{
			if (p.line == line || p.changed || (first_line && !p.line))
			{
				if (has_opt) str += '\n'; // 'line' must not be increased
				
				if (!p.line && !p.descr_v.empty()) {
					str += "\n# ";
					str += p.descr_v;
					str += '\n';
				}
				
				str += p.name;
				for (auto& arg : p.args) {
					str += ' ';
					std::visit([&](auto& v){ return v.write(str); }, arg);
				}
				
				--opts_left;
				has_opt = true;
				if (!first_line) break;
			}
			else if (p.line > line)
				next = std::min(next, p.line);
		}
		
		// add comment
		if (i_comm != comments.size()) {
			if (comments[i_comm].first == line)
			{
				if (has_opt) str += ' ';
				str += comments[i_comm].second;
				++i_comm;
			}
			if (i_comm != comments.size())
				next = std::min(next, comments[i_comm].first);
		}
		
		// end line
		if (next == std::numeric_limits<int>::max())
			break;
			
		str.insert(str.end(), next - line, '\n');
		line = next;
		first_line = false;
	}
	
	if (!str.empty() && str.back() != '\n')
		str.push_back('\n');
		
	return str;
}
bool LineCfg::read(const char *filename)
{
	auto s = readfile(filename);
	if (!s) return false;
	
	try {
		read_s(*s);
		return true;
	}
	catch (std::exception& e) {
		VLOGE("{}", e.what());
		return false;
	}
}
bool LineCfg::write(const char *filename) const
{
	try {
		auto s = write_s();
		if (!writefile(filename, s.data(), s.size())) {
			VLOGE("LineCfg::write() writefile failed");
			return false;
		}
		return true;
	}
	catch (std::exception& e) {
		VLOGE("LineCfg::write() failed - {}", e.what());
		return false;
	}
}
