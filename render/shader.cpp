#include "shader.hpp"
#include "vaslib/vas_cpp_utils.hpp"
#include "vaslib/vas_file.hpp"
#include "vaslib/vas_log.hpp"
#include "vaslib/vas_misc.hpp"
#include "vaslib/vas_string_utils.hpp"



/*
	Format directives: 
  
	//@vert [NAME]   // begins new block or includes existing (vertex shader)
	//@frag [NAME]   // begins new block or includes existing (fragment shader)
	//@geom [NAME]   // begins new block or includes existing (geometry shader)
	//@end           // ends block
	
	//@def <NAME> <DEF_VALUE>   // declares parameter
*/

struct SingleShader
{
	std::string fname; ///< file name (without res path)
	GLenum type;
	std::string full_name; ///< name:type
	std::string src_version; ///< #version
	std::string src_code;
	std::vector<Shader::Define> defs;
};
static std::vector<SingleShader> sh_col;

template<typename T, typename R>
bool begins_with(const T& s, const R& pref) {
	return !s.compare(0, pref.length(), pref);
}

template<typename T>
bool begins_with(const T& s, const char *pref) {
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

static std::optional<SingleShader> read_shader(const std::vector<std::string_view>& lines, size_t& i, const std::string& full_name)
{
	// i ->: first line of block (excluding header)
	// i <-: first line after block (excluding @end)
	
	SingleShader sh;
	
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
	
	return sh;
}

static std::optional<size_t> get_shd_existing(GLenum type, const std::string& fname)
{
	for (size_t i = 0; i < sh_col.size(); ++i)
	{
		auto& s = sh_col[i];
		if (s.type == type && s.fname == fname)
			return i;
	}
	return {};
}

static std::optional<size_t> get_shd(GLenum type, const std::string& fname)
{
	// search loaded
	
	if (auto i = get_shd_existing(type, fname))
		return i;
	
	// get pref & make name
	
	std::string_view pref;
	std::string full_name = fname;
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
		VLOGE("Shader:: no directive for shader type {}; '{}'", type, fname);
		return {};
	}
	
	// read file
	
	auto file_opt = readfile( (std::string("res/shaders/") + fname).data() );
	if (!file_opt) {
		VLOGE("Shader:: can't read shader \"{}\"; '{}'", fname, full_name);
		return {};
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
	
	sh->fname = fname;
	sh->type = type;
	sh->full_name = std::move(full_name);
	
	sh_col.emplace_back( std::move(*sh) );
	return sh_col.size() - 1;
}



static std::vector<Shader*> all_shader_ptr;

ptr_range<Shader*> Shader::get_all_ptrs()
{
	return all_shader_ptr;
}

std::unique_ptr<Shader> Shader::load(const char *name, Callbacks cbs, bool is_critical, bool do_build)
{
	std::unique_ptr<Shader> s(new Shader);
	all_shader_ptr.push_back(s.get());
	
	s->cbs = std::move(cbs);
	s->is_critical = is_critical;
	s->prog_name = name;
	
	if (s->reload() && do_build) s->rebuild();
	return s;
}

Shader::~Shader()
{
	reset_prog();
	
	auto it = std::find(all_shader_ptr.begin(), all_shader_ptr.end(), this);
	if (it != all_shader_ptr.end())
		all_shader_ptr.erase(it);
}

Shader::Define* Shader::get_def(std::string_view name)
{
	for (auto& d : def_list) {
		if (d.name == name)
			return &d;
	}
	return nullptr;
}

void Shader::set_def(std::string_view name, std::string value)
{
	auto d = get_def(name);
	if (d) {
		d->value = std::move(value);
		reset_prog();
	}
	else VLOGD("Shader::set_def() no '{}' in '{}'", name, prog_name);
}

bool Shader::reload()
{
	bool was_built = (prog != 0);
	
	// read file
	
	auto file_opt = readfile( (std::string("res/shaders/") + prog_name).data() );
	if (!file_opt) {
		VLOGE("Shader::reload() can't read file; '{}'", prog_name);
		return false;
	}
	
	const std::string& file = *file_opt;
	auto lines = string_split_view(file, {"\n"}, false);
	
	// clear
	
	src_ixs.clear();
	reset_prog();
	
	// parse file
	
	for (size_t i=0; i < lines.size(); ++i)
	{
		auto& ln = lines[i];
		
		if (begins_with(ln, "//@"))
		{
			GLenum type = 0;
			std::string full_name = prog_name;
			
			for (auto& p : pref_assoc)
			{
				if (begins_with(ln, p.pref))
				{
					type = p.type;
					full_name += ':';
					full_name += p.name;
					break;
				}
			}
			
			if (!type) {
				VLOGE("Shader::reload() inappropriate directive; '{}' (line {})", prog_name, i+1);
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
				
				sh->fname = prog_name;
				sh->type = type;
				sh->full_name = std::move(full_name);
				
				//
				
				auto i_src = get_shd_existing(sh->type, prog_name);
				if (i_src) sh_col[*i_src] = std::move(*sh);
				else {
					i_src = sh_col.size();
					sh_col.emplace_back(std::move(*sh));
				}
				src_ixs.push_back(*i_src);
			}
			else if (args.size() == 2)
			{
				auto sh = get_shd(type, std::string(args[1]));
				if (!sh) {
					VLOGE("Shader::reload() can't find or load shader; '{}' (line {})", full_name, i+1);
					return false;
				}
				src_ixs.push_back(*sh);
			}
			else {
				VLOGE("Shader::reload() inappropriate 'shader' directive; '{}' (line {})", prog_name, i+1);
				return false;
			}
		}
		else if (!begins_with(ln, "//"))
		{
			for (auto& c : ln)
			{
				if (c != ' ' && c != '\t') {
					VLOGE("Shader::reload() code outside of block; '{}' (line {})", prog_name, i+1);
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
	
	for (auto& i_src : src_ixs)
	{
		auto& s = sh_col[i_src];
		switch (s.type)
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
		for (auto& i_src : src_ixs) {ls += "  "; ls += sh_col[i_src].full_name;}
		VLOGE("Shader::reload() more than one shader of same type found; '{}':\n{}", prog_name, ls);
	}
	if (n_unk) VLOGW("Shader::reload() {} unknown shader types in '{}'", n_unk, prog_name);
	
	if (src_ixs.empty()) VLOGE("Shader::reload() no shaders in '{}'", prog_name);
	else {
		if (!n_vert) VLOGW("Shader::reload() no vertex shader in '{}'", prog_name);
		if (!n_frag) VLOGW("Shader::reload() no fragment shader in '{}'", prog_name);
	}
	
	// update defines
	
	auto old_defs = std::move(def_list);
	
	for (auto& i_src : src_ixs)
	{
		auto& s = sh_col[i_src];
		for (auto& new_d : s.defs)
		{
			bool skip = false;
			for (auto& d : def_list)
			{
				if (d.name == new_d.name)
				{
					if (d.value != new_d.value)
						VLOGW("Shader::reload() define declared multiple times with different value (first one is used) - '{}' in '{}'",
						      d.name, prog_name);
					
					skip = true;
					break;
				}
			}
			if (skip) continue;
			
			//
				
			auto& nd = def_list.emplace_back();
			nd = new_d;
			
			// check if had non-default value
			
			Define* d_old = {};
			for (auto& d : old_defs) {
				if (d.name == new_d.name) {
					d_old = &d;
					break;
				}
			}
			
			if (d_old && !d_old->is_default)
			{
				nd.value = std::move(d_old->value);
				nd.is_default = false;
			}
		}
	}
	
	//
	
//	VLOGV("Shader::reload() ok - {}", name);
	return was_built? rebuild() : true;
}

bool Shader::rebuild(bool forced)
{
	if (prog && !forced)
		return true;
	
	never_built = false;
	reset_prog();
	
	if (src_ixs.empty())
		return false;
	
	if (cbs.pre_build)
		cbs.pre_build(*this);
	
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
			VLOGE("Shader::rebuild() failed - [] {}", prog_name);
			reset_prog();
		}
	});
	
	// compile
	
	for (auto& i_src : src_ixs)
	{
		auto& src = sh_col[i_src];
		src_s[0] = src.src_version.data();
		src_n[0] = src.src_version.length();
		src_s[2] = src.src_code.data();
		src_n[2] = src.src_code.length();
		
		GLuint& sh = shs.emplace_back();
		sh = glCreateShader(src.type);
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
			      src.full_name, std::string_view(str, len));
			
			delete[] str;
			return false;
		}
		
		if (log_test_level(LogLevel::Verbose))
		{
			GLint str_n = 0;
			glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &str_n);
			if (str_n > 3) // just newline
			{
				char *str = new char [str_n];
				GLsizei len = 0;
				glGetShaderInfoLog(sh, str_n, &len, str);
				VLOGE("Shader::rebuild() compilation info for '{}' in {}:\n{}\nEND\n",
				      src.full_name, prog_name, std::string_view(str, len));
				delete[] str;
			}
		}
	}
	
	// link
	
	prog = glCreateProgram();
	
	for (auto& s : shs)
		glAttachShader(prog, s);
	
	if (cbs.pre_link)
		cbs.pre_link(*this);
	
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
			      prog, prog_name, std::string_view(str, len));
			delete[] str;
		}
	}
	
	for (auto &s : shs)
		glDetachShader(prog, s);
	
	// finished
	
	VLOGD("Shader::rebuild() ok - [{}] {}", prog, prog_name);
	did_fail = false;
	
	if (cbs.post_build) {
		glUseProgram(prog);
		cbs.post_build(*this);
	}
	
	validate = true;
	return true;
}

void Shader::bind()
{
	if (!prog)
	{
		if (!rebuild())
			LOG_THROW("Shader::bind() can't build: {}", prog_name);
	}
	
	if (validate)
	{
		validate = false;
		glValidateProgram(prog);
		
		GLint err;
		glGetProgramiv(prog, GL_VALIDATE_STATUS, &err);
		
		if (err == GL_TRUE) VLOGV("Validation ok: [{}] {}", prog, prog_name);
		else
		{
			VLOGW("Validation failed: [{}] {}", prog, prog_name);
			
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
		
		no_such_loc.clear();
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
	GLint loc = glGetUniformLocation(prog, name);
	if (loc < 0)
	{
		auto h = fast_hash32(name);
		if (no_such_loc.end() == std::find( no_such_loc.begin(), no_such_loc.end(), h )) {
			no_such_loc.push_back(h);
			VLOGW("Shader::find_loc() no '{}' in '{}'", name, prog_name);
		}
	}
	return loc;
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
