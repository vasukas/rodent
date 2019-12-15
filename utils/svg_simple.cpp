#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_file.hpp"
#include "vaslib/vas_log.hpp"
#include "vaslib/vas_string_utils.hpp"
#include "svg_simple.hpp"

struct XML_Element
{
	std::string name;
	std::unordered_map<std::string, std::string> attrs;
	std::vector<XML_Element> els;
	
	const std::string& get_attr(const std::string& name) const
	{
		auto it = attrs.find(name);
		if (it != attrs.end()) return it->second;
		THROW_FMTSTR("No such attribute: {} in {}", name, this->name);
	}
	XML_Element& get_el(const std::string& name)
	{
		for (auto& e : els) if (e.name == name) return e;
		THROW_FMTSTR("No such element: {} in {}", name, this->name);
	}
};

static std::optional<XML_Element> read(const std::string& str, size_t& i)
{
	auto check = [&]()
	{
		if (i >= str.length())
			THROW_FMTSTR("Incomplete tag at {}", i);
	};
	auto detail_skip = [&](std::string add_syms, bool chars)
	{
		while (i < str.length())
		{
			bool is_sp = false;
			for (auto c : {' ', '\t', '\n'})    if (str[i] == c) {is_sp = true; break;}
			if (!is_sp) for (auto c : add_syms) if (str[i] == c) {is_sp = true; break;}
			
			if (chars) is_sp = !is_sp;
			if (!is_sp) break;
			++i;
		}
	};
	auto skip_spaces = [&](std::string add_syms = {})
	{
		while (true) {
			detail_skip(add_syms, false);
			check();
			if (str.compare(i, 4, "<!--")) break;
			i += 3;
			check();
		}
	};
	auto skip_chars  = [&](std::string add_syms = {})
	{
		detail_skip(add_syms, true);
		check();
	};
	
	
	
	if (i >= str.length()) return {}; // nothing left to read
	
	skip_spaces();
	if (!str.compare(i, 5, "<?xml"))
	{
		i = str.find("?>", i);
		check();
		i += 2;
		skip_spaces();
		
		XML_Element el;
		el.name = "ROOT";
		while (auto e = read(str, i))
			el.els.emplace_back( std::move(*e) );
		
		return el;
	}
	
	if (str[i] != '<') // skip tag data
	{
		i = str.find('<', i);
		if (i >= str.length()) return {};
	}
	++i;
	skip_spaces();
	
	if (str[i] == '/') // skip closing tag
	{
		i = str.find('>', i);
		if (i >= str.length()) return {};
		
		skip_spaces();
		if (str[i] != '<') return {};
	}
	skip_spaces();
	
	size_t beg = i;
	skip_chars(">/");
	
	XML_Element el;
	el.name = str.substr(beg, i - beg);
	
	skip_spaces();
	while (str[i] != '>' && str[i] != '/')
	{
		beg = i;
		skip_chars("=");
		
		std::string name = str.substr(beg, i - beg);
		if (el.attrs.find(name) != el.attrs.end())
			THROW_FMTSTR("Duplicate attribute at {}", i);
		
		skip_spaces();
		if (str[i] != '=')
			THROW_FMTSTR("Invalid attribute value at {}", i);
		
		++i;
		skip_spaces();
		
		if (str[i] != '"')
			THROW_FMTSTR("Invalid attribute value at {}", i);
		
		beg = ++i;
		i = str.find('"', i);
		check();
		
		el.attrs.emplace(std::move(name), str.substr(beg, i - beg));
		
		++i;
		skip_spaces();
	}
	if (str[i] == '>')
	{
		++i;
		while (auto e = read(str, i))
			el.els.emplace_back( std::move(*e) );
	}
	else if (str[i] == '>') ++i;
	else if (str[i] == '/') i += 2;
	
	return el;
}



static void parse(SVG_File& f, const XML_Element& el)
{
	auto rpos = [&](const std::string& s)
	{
		float val;
		if (!string_atof(s, val)) throw std::runtime_error("Not a number");
		return val;
	};
	try
	{
		for (auto& e : el.els)
		{
			if (e.name == "g") parse(f, e);
			else if (e.name == "path")
			{
				reserve_more_block(f.paths, 256);
				auto& p = f.paths.emplace_back();
				p.id = e.get_attr("id");
				
				auto vs = string_split(e.get_attr("d"), {" ", ","});
				p.ps.reserve( vs.size() /2 );
				
				try
				{
					if (vs.size() < 4) THROW_FMTSTR("empty path");
					if (vs[0] != "M") THROW_FMTSTR("no absolute moveto");
					
					vec2fp cur = {
					    rpos(vs.at(1)),
					    rpos(vs.at(2))
					};
					p.ps.push_back(cur);
					int cmd = 0;
					
					for (size_t i=3; i<vs.size(); ++i)
					{
						if		(vs[i] == "L") cmd = 0; else if (vs[i] == "l") cmd = 1;
						else if (vs[i] == "H") cmd = 2; else if (vs[i] == "h") cmd = 3;
						else if (vs[i] == "V") cmd = 4; else if (vs[i] == "v") cmd = 5;
						else if (vs[i] == "Z" || vs[i] == "z") {
							p.ps.push_back(p.ps[0]);
							break;
						}
						else {
							if (cmd < 2) {
								vec2fp pt = {
								    rpos(vs.at(i)),
								    rpos(vs.at(i+1))
								};
								if (cmd & 1) cur += pt; else cur = pt;
								++i;
							}
							else if (cmd < 4) {
								float v = rpos(vs.at(i));
								if (cmd & 1) cur.x += v; else cur.x = v;
							}
							else {
								float v = rpos(vs.at(i));
								if (cmd & 1) cur.y += v; else cur.y = v;
							}
							p.ps.push_back(cur);
						}
					}
				}
				catch (std::exception& exc) {
					THROW_FMTSTR("path {}: {}", e.name, exc.what());
				}
			}
			else if (e.name == "rect")
			{
				reserve_more_block(f.paths, 256);
				auto& p = f.paths.emplace_back();
				p.id = e.get_attr("id");
				
				float x1 = rpos(e.get_attr("x"));
				float y1 = rpos(e.get_attr("y"));
				float x2 = rpos(e.get_attr("width"))  + x1;
				float y2 = rpos(e.get_attr("height")) + y1;
				
				p.ps.emplace_back(x1, y1);
				p.ps.emplace_back(x2, y1);
				p.ps.emplace_back(x2, y2);
				p.ps.emplace_back(x1, y2);
				p.ps.emplace_back(x1, y1);
			}
			else if (e.name == "circle" || e.name == "ellipse")
			{
				reserve_more_block(f.points, 64);
				auto& p = f.points.emplace_back();
				p.id = e.get_attr("id");
				p.pos.x = rpos(e.get_attr("cx"));
				p.pos.y = rpos(e.get_attr("cy"));
				
				if (e.name == "circle") p.radius = rpos(e.get_attr("r"));
				else p.radius = std::max(rpos(e.get_attr("rx")), rpos(e.get_attr("ry")));
			}
		}
	}
	catch (std::exception& e) {
		THROW_FMTSTR("{} - {}", el.name, e.what());
	}
}
SVG_File svg_read(const char *filename)
{
	auto str = readfile(filename);
	if (!str)
		LOG_THROW("svg_read() readfile failed");
	
	try {
		size_t i = 0;
		auto elrd = read(*str, i);
		if (!elrd) THROW_FMTSTR("not XML");
		
		auto el = std::move(elrd->get_el("svg"));
		elrd.reset();
		
		SVG_File f;
		parse(f, el);
		if (f.paths.empty() && f.points.empty())
			THROW_FMTSTR("empty SVG");
		
		return f;
	}
	catch (std::exception& e) {
		LOG_THROW("svg_read() failed - {}", e.what());
	}
}
