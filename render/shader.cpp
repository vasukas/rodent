#include "shader.hpp"
#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_file.hpp"
#include "vaslib/vas_log.hpp"
#include "vaslib/vas_string_utils.hpp"

/*
	Format directives: 
  
	//@vert [NAME]   // begins new block or includes existing (vertex shader)
	//@frag [NAME]   // begins new block or includes existing (fragment shader)
	//@geom [NAME]   // begins new block or includes existing (geometry shader)
	//@end           // ends block
	
	//@def <NAME> <DEF_VALUE>   // declares parameter
*/

std::vector<std::unique_ptr<Shader, Shader::DEL>> Shader::sh_col;

template<typename T, typename R>
bool begins_with(T& s, const R& pref) {
	return !s.compare(0, pref.length(), pref);
}

template<typename T>
bool begins_with(T& s, const char *pref) {
	return !s.compare(0, strlen(pref), pref);
}

struct PrefAssoc
{
	GLenum type;
	std::string pref;
	std::string name;
};
static const std::vector<PrefAssoc> pref_assoc =
{
    {GL_VERTEX_SHADER,   "//@vert", "vert"},
    {GL_FRAGMENT_SHADER, "//@frag", "frag"},
    {GL_GEOMETRY_SHADER, "//@geom", "geom"}
};

std::shared_ptr<Shader::SingleShader> Shader::read_shader(std::vector<std::string_view>& lines, size_t& i, const std::string& full_name)
{
	// i ->: first line of block (excluding header)
	// i <-: first line after block (excluding @end)
	
	Shader::SingleShader sh;
	
	for (; i < lines.size(); ++i)
	{
		auto& ln = lines[i];
		
		if		(begins_with(ln, "//@end"))
		{
			++i;
			break;
		}
		else if (begins_with(ln, "//@def"))
		{
			reserve_more_block(sh.defs, 16);
			
			auto args = string_split_view(ln, {" "});
			if (args.size() != 3) {
				VLOGE("Shader:: invalid 'define' directive; '{}' (line {})", full_name, i+1);
				return {};
			}
			
			auto& d = sh.defs.emplace_back();
			d.is_src = true;
			d.name = args[1];
			d.value = args[2];
		}
		else if (begins_with(ln, "//@"))
		{
			bool ok = false;
			for (auto& p : pref_assoc)
			{
				if (begins_with(ln, p.pref))
				{
					ok = true;
				    break;
				}
			}
			if (ok) break;
			
			VLOGE("Shader:: inappropriate directive; '{}' (line {})", full_name, i+1);
			return {};
		}
		else if (begins_with(ln, "#version")) {
			sh.src_version = ln;
			sh.src_version += '\n';
		}
		else {
			reserve_more_block(sh.src_code, 4096);
			sh.src_code += ln;
			sh.src_code += '\n';
		}
	}
	
	if (sh.src_version.empty()) {
		VLOGE("Shader:: no '#version' directive; '{}'", full_name);
		return {};
	}
	
	return std::make_shared<Shader::SingleShader>(std::move(sh));
}

std::shared_ptr<Shader::SingleShader> Shader::get_shd(GLenum type, std::string name)
{
	// search loaded
	
	for (auto& p : sh_col)
	for (auto& s : p->src)
	{
		if (s->type == type && s->name == name)
			return s;
	}
	
	// get pref & make name
	
	std::string_view pref;
	std::string full_name = name;
	full_name += ':';
	
	for (auto& p : pref_assoc)
	{
		if (p.type == type)
		{
			pref = p.pref;
			full_name += p.name;
			break;
		}
	}
	if (pref.empty()) {
		VLOGE("Shader:: no directive for shader type {}; '{}'", type, name);
		return {};
	}
	
	// read file
	
	auto file_opt = readfile( (std::string("res/shaders/") + name).data() );
	if (!file_opt) {
		VLOGE("Shader:: can't read shader \"{}\"; '{}'", name, full_name);
		return nullptr;
	}
	
	const std::string& file = *file_opt;
	auto lines = string_split_view(file, {"\n"}, false);
	
	// find block
	
	size_t i = 0;
	
	for (; i < lines.size(); ++i)
	{
		auto& ln = lines[i];
		if (begins_with(ln, pref))
		{
			auto args = string_split_view(ln, {" "});
			if (args.size() != 1) {
				VLOGE("Shader:: inappropriate block directive; '{}' (line {})", full_name, i+1);
				return {};
			}
			break;
		}
	}
	
	if (i == lines.size()) {
		VLOGE("Shader:: block directive not found; '{}'", full_name);
		return {};
	}
	
	// read block
	
	++i;
	auto sh = read_shader(lines, i, full_name);
	if (!sh) return {};
	
	sh->name = std::move(name);
	sh->type = type;
	sh->full_name = std::move(full_name);
	return sh;
}

Shader* Shader::load(const char *name, bool, bool do_build)
{
	for (auto& s : sh_col)
	{
		if (s->name == name)
		{
			if (do_build) s->rebuild(false);
			return s.get();
		}
	}
	
	auto& s = sh_col.emplace_back(new Shader);
	s->name = name;
	
	s->reload();
	if (do_build) s->rebuild();
	
	return s.get();
}

Shader* Shader::load_cb(const char *name, std::function<void(Shader&)> on_reb)
{
	Shader* s = load(name, false, false);
	s->on_reb = std::move(on_reb);
	s->rebuild();
	return s;
}

Shader::Define* Shader::get_def(std::string_view name)
{
	for (auto& d : def_list) {
		if (d.name == name)
			return &d;
	}
	return nullptr;
}

bool Shader::rebuild(bool forced)
{
	if (prog && !forced)
		return true;
	
	reset_prog();
	
	if (src.empty())
		return false;
	
	if (pre_reb)
		pre_reb(*this);
	
	// get defines
	
	const GLchar* src_s[3];
	GLint src_n[3];
	
	std::string def_str;
	def_str.reserve(512);
	for (auto& d : def_list)
		def_str += FMT_FORMAT("#define {} {}\n", d.name, d.value);
	
	src_s[1] = def_str.data();
	src_n[1] = def_str.length();
	
	// raii
	
	bool did_fail = true;
	std::vector<GLuint> shs;
	
	RAII_Guard g([&]
	{
		for (auto& s : shs) glDeleteShader(s);
		if (did_fail) {
			VLOGE("Shader::rebuild() failed - [] {}", name);
			reset_prog();
		}
	});
	
	// compile
	
	for (auto& src : src)
	{
		src_s[0] = src->src_version.data();
		src_n[0] = src->src_version.length();
		src_s[2] = src->src_code.data();
		src_n[2] = src->src_code.length();
		
		GLuint& sh = shs.emplace_back();
		sh = glCreateShader(src->type);
		glShaderSource(sh, 3, src_s, src_n);
		glCompileShader(sh);
		
		GLint err = 0;
		glGetShaderiv(sh, GL_COMPILE_STATUS, &err);
		
		if (err == GL_FALSE)
		{
			GLint str_n = 0;
			glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &str_n);
			char *str = new char [str_n];
			GLsizei len = 0;
			glGetShaderInfoLog(sh, str_n, &len, str);
			VLOGE("Shader:: compilation of '{}' failed:\n{}\nEND\n",
			      src->full_name, std::string_view(str, len));
			
			delete[] str;
			return false;
		}
		
		if (log_test_level(LogLevel::Verbose))
		{
			GLint str_n = 0;
			glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &str_n);
			if (str_n)
			{
				char *str = new char [str_n];
				GLsizei len = 0;
				glGetShaderInfoLog(sh, str_n, &len, str);
				VLOGE("Shader::rebuild() compilation info for '{}' in {}:\n{}\nEND\n",
				      src->full_name, name, std::string_view(str, len));
				delete[] str;
			}
		}
	}
	
	// link
	
	prog = glCreateProgram();
	
	for (auto& s : shs)
		glAttachShader(prog, s);
	
	if (pre_link)
		pre_link(*this);
	
	glLinkProgram(prog);
	
	GLint err;
	glGetProgramiv(prog, GL_LINK_STATUS, &err);
	
	if (err == GL_FALSE)
	{
		GLint str_n = 0;
		glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &str_n);
		char *str = new char [str_n];
		GLsizei len = 0;
		glGetProgramInfoLog(prog, str_n, &len, str);
		VLOGE("Shader:: link failed:\n{}\nEND\n", std::string_view(str, len));
		
		delete[] str;
		return false;
	}
	
	if (log_test_level(LogLevel::Verbose))
	{
		GLint str_n = 0;
		glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &str_n);
		if (str_n)
		{
			char *str = new char [str_n];
			GLsizei len = 0;
			glGetProgramInfoLog(prog, str_n, &len, str);
			VLOGV("Shader::rebuild() link info for [{}] {}:\n{}\nEND\n",
			      prog, name, std::string_view(str, len));
			delete[] str;
		}
	}
	
	for (auto &s : shs)
		glDetachShader(prog, s);
	
	// finished
	
	VLOGD("Shader::rebuild() ok - [{}] {}", prog, name);
	did_fail = false;
	
	if (on_reb) {
		glUseProgram(prog);
		on_reb(*this);
	}
	
	validate = true;
	return true;
}

bool Shader::reload()
{
	bool was_built = is_ok();
	
	// read file
	
	auto file_opt = readfile( (std::string("res/shaders/") + name).data() );
	if (!file_opt) {
		VLOGE("Shader::reload() can't read file; '{}'", name);
		return false;
	}
	
	const std::string& file = *file_opt;
	auto lines = string_split_view(file, {"\n"}, false);
	
	// clear
	
	src.clear();
	reset_prog();
	
	// parse file
	
	for (size_t i=0; i < lines.size(); ++i)
	{
		auto& ln = lines[i];
		
		if (begins_with(ln, "//@"))
		{
			GLenum type = 0;
			std::string full_name = name;
			
			for (auto& p : pref_assoc)
			{
				if (begins_with(ln, p.pref))
				{
					type = p.type;
					full_name += p.name;
					break;
				}
			}
			
			if (!type) {
				VLOGE("Shader::reload() inappropriate directive; '{}' (line {})", name, i+1);
				return false;
			}
			
			auto args = string_split_view(ln, {" "});
			if		(args.size() == 1)
			{
				++i;
				auto sh = read_shader(lines, i, full_name);
				if (!sh) {
					VLOGE("Shader::reload() can't load shader; '{}' (line {})", full_name, i+1);
					return false;
				}
				--i;
				
				sh->name = name;
				sh->type = type;
				sh->full_name = std::move(full_name);
				src.emplace_back(std::move(sh));
			}
			else if (args.size() == 2)
			{
				auto sh = get_shd(type, std::string(args[1]));
				if (!sh) {
					VLOGE("Shader::reload() can't find or load shader; '{}' (line {})", full_name, i+1);
					return false;
				}
				src.emplace_back(std::move(sh));
			}
			else {
				VLOGE("Shader::reload() inappropriate 'shader' directive; '{}' (line {})", name, i+1);
				return false;
			}
		}
		else if (!begins_with(ln, "//"))
		{
			for (auto& c : ln)
			{
				if (c != ' ' && c != '\t') {
					VLOGE("Shader::reload() code outside of block; '{}' (line {})", name, i+1);
					return false;
				}
			}
		}
	}
	
	// check shader types
	
	int n_vert = 0;
	int n_frag = 0;
	int n_geom = 0;
	int n_unk = 0;
	
	for (size_t i=0; i < src.size(); ++i)
	{
		switch (src[i]->type)
		{
		case GL_VERTEX_SHADER:   ++n_vert; break;
		case GL_FRAGMENT_SHADER: ++n_frag; break;
		case GL_GEOMETRY_SHADER: ++n_geom; break;
		default: ++n_unk; break;
		}
	}
	
	if (n_vert > 1 || n_frag > 1 || n_geom > 1)
	{
		std::string ls;
		for (auto& s : src) {ls += "  "; ls += s->full_name;}
		VLOGE("Shader::reload() more than one shader of same type found; '{}':\n{}", name, ls);
	}
	if (n_unk) VLOGW("Shader::reload() {} unknown shader types in '{}'", n_unk, name);
	
	if (src.empty()) VLOGE("Shader::reload() no shaders in '{}'", name);
	else {
		if (!n_vert) VLOGW("Shader::reload() no vertex shader in '{}'", name);
		if (!n_frag) VLOGW("Shader::reload() no fragment shader in '{}'", name);
	}
	
	// defines: remove unused, remove new dupes, default defaultable
	
	for (auto i = def_list.begin(); i != def_list.end(); )
	{
		bool any = false;
		
		for (auto& s : src)
		for (auto n = s->defs.begin(); n != s->defs.end(); )
		{
			if (n->name == i->name)
			{
				if (i->is_default)
					i->value = n->value;
				
				any = true;
				n = s->defs.erase(n);
			}
			else ++n;
		}
		
		if (any) ++i;
		else i = def_list.erase(i);
	}
	
	// defines: add new
	
	for (auto& s : src)
		append(def_list, s->defs);
	
	//
	
//	VLOGV("Shader::reload() ok - {}", name);
	return was_built? rebuild() : true;
}

void Shader::bind()
{
	if (!prog)
	{
		if (!rebuild())
			LOG_THROW("Shader::bind() can't build: {}", name);
	}
	
	if (validate)
	{
		validate = false;
		glValidateProgram(prog);
		
		GLint err;
		glGetProgramiv(prog, GL_VALIDATE_STATUS, &err);
		
		if (err == GL_TRUE) VLOGV("Validation ok: [{}] {}", prog, name);
		else
		{
			VLOGW("Validation failed: [{}] {}", prog, name);
			
			GLint str_n = 0;
			glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &str_n);
			if (str_n)
			{
				char *str = new char [str_n];
				GLsizei len = 0;
				glGetProgramInfoLog(prog, str_n, &len, str);
				VLOGW("Shader:: validation:\n{}\nEND\n", std::string_view(str, len));
				delete[] str;
			}
		}
	}
	
	glUseProgram(prog);
}

void Shader::reset_prog()
{
	glDeleteProgram(prog);
	prog = 0;
}

GLint Shader::find_loc( const char *name )
{
	return glGetUniformLocation(prog, name);
}



void Shader::set1i(int loc, int v) {
	glUniform1i(loc, v);
}
void Shader::set1f(int loc, float v) {
	glUniform1f(loc, v);
}
void Shader::set2f(int loc, float a, float b) {
	glUniform2f(loc, a, b);
}
void Shader::set3f(int loc, float x, float y, float z) {
	glUniform3f(loc, x, y, z);
}
void Shader::set4f(int loc, float r, float g, float b, float a) {
	glUniform4f(loc, r, g, b, a);
}
void Shader::setfv(int loc, const float *v, int n) {
	glUniform1fv(loc, n, v);
}
void Shader::set2mx(int loc, const float *v, bool transp) {
	glUniformMatrix2fv(loc, 1, transp, v);
}
void Shader::set3mx(int loc, const float *v, bool transp) {
	glUniformMatrix3fv(loc, 1, transp, v);
}
void Shader::set4mx(int loc, const float *v, bool transp) {
	glUniformMatrix4fv(loc, 1, transp, v);
}
void Shader::set_rgba(int loc, uint32_t clr, float mul) {
	mul /= 255.f;
	glUniform4f(loc,
	      mul * (clr >> 24),
	      mul * ((clr >> 16) & 0xff),
	      mul * ((clr >> 8) & 0xff),
	      (clr & 0xff) / 255.f);
}
void Shader::set_clr(int loc, const FColor& clr) {
	glUniform4f(loc, clr.r, clr.g, clr.b, clr.a);
}
void Shader::set2f(int loc, const vec2fp& p) {
	glUniform2f(loc, p.x, p.y);
}



void Shader::set1i(const char *name, int v) {
	set1i( find_loc(name), v );
}
void Shader::set1f(const char *name, float v) {
	set1f( find_loc(name), v );
}
void Shader::set2f(const char *name, float a, float b) {
	set2f( find_loc(name), a, b );
}
void Shader::set3f(const char *name, float x, float y, float z) {
	set3f( find_loc(name), x, y, z );
}
void Shader::set4f(const char *name, float r, float g, float b, float a) {
	set4f( find_loc(name), r, g, b, a );
}
void Shader::setfv(const char *name, const float *v, int n) {
	setfv( find_loc(name), v, n );
}
void Shader::set2mx(const char *name, const float *v, bool transp) {
	set2mx( find_loc(name), v, transp );
}
void Shader::set3mx(const char *name, const float *v, bool transp) {
	set3mx( find_loc(name), v, transp );
}
void Shader::set4mx(const char *name, const float *v, bool transp) {
	set4mx( find_loc(name), v, transp );
}
void Shader::set_rgba(const char *name, uint32_t clr, float mul) {
	set_rgba( find_loc(name), clr, mul );
}
void Shader::set_clr(const char *name, const FColor& clr) {
	set_clr( find_loc(name), clr );
}
void Shader::set2f(const char *name, const vec2fp& p) {
	set2f( find_loc(name), p );
}
