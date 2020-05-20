#ifndef LINE_CFG_HPP
#define LINE_CFG_HPP

// Note: multi-line string not supported

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <typeindex>

inline bool linecfg_is_space(char c) {
	return c == ' ' || c == '\t';
}

enum class LineCfgArgError {
	Ok,
	Fail,
	OutOfRange
};



struct LineCfgEnumType {
	using Base = int;
	Base (*man_get)(const void*);
	void (*man_set)(void*, Base);
	
	std::function<bool(void*, std::string_view)> read;
	std::function<bool(const void*, std::string&)> write;
	std::type_index type;
	
	template<typename T>
	static std::shared_ptr<LineCfgEnumType> make(std::initializer_list<std::pair<T, std::string>> values)
	{
		static_assert(sizeof(T) <= sizeof(Base));
		for (auto& v1 : values) {
			int cou = 0;
			for (auto& v2 : values) {
				if (v1.first == v2.first || v1.second == v2.second)
					++cou;
			}
			if (cou > 2)
				throw std::logic_error("LineCfgEnumType:: value repeat");
		}
		
		std::shared_ptr<LineCfgEnumType> t(new LineCfgEnumType(typeid(T)));
		t->man_get = [](const void* p) {return static_cast<Base>(*static_cast<const T*>(p));};
		t->man_set = [](void *p, Base v) {*static_cast<T*>(p) = static_cast<T>(v);};
		
		t->vps.reserve(values.size());
		for (auto& val : values)
			t->vps.emplace_back(static_cast<Base>(val.first), std::move(val.second));
		
		t->read = [vps = &t->vps](void* p, std::string_view s){
			for (auto& vp : *vps) {
				if (vp.second == s) {
					*static_cast<T*>(p) = static_cast<T>(vp.first);
					return true;
				}
			}
			return false;
		};
		t->write = [vps = &t->vps](const void* p, std::string& s){
			for (auto& vp : *vps) {
				if (vp.first == static_cast<Base>(*static_cast<const T*>(p))) {
					s += vp.second;
					return true;
				}
			}
			return false;
		};
		return t;
	}
	
	template<typename T>
	static std::shared_ptr<LineCfgEnumType> make(
		std::function<std::optional<T>(std::string_view)> get_value,
		std::function<std::string_view(T)> get_name)
	{
		static_assert(sizeof(T) <= sizeof(Base));
		std::shared_ptr<LineCfgEnumType> t(new LineCfgEnumType(typeid(T)));
		t->read = [f = std::move(get_value)](void* p, std::string_view s){
			if (s.front() == '"') s = s.substr(1, s.size() - 2);
			if (auto v = f(s)) {
				*static_cast<T*>(p) = *v;
				return true;
			}
			return false;
		};
		t->write = [f = std::move(get_name)](const void* p, std::string& s){
			auto v = f(*static_cast<const T*>(p));
			if (!v.empty()) {
				for (auto& c : v) if (linecfg_is_space(c)) {
					s.push_back('"');
					s += v;
					s.push_back('"');
					return true;
				}
				// else
				s += v;
				return true;
			}
			return false;
		};
		return t;
	}
	
	/// May be empty
	const std::vector<std::pair<Base, std::string>>& get_values() const {return vps;}
	
private:
	std::vector<std::pair<Base, std::string>> vps;
	LineCfgEnumType(std::type_index type): type(type) {}
};

struct LineCfgArg_Int {
	int& v;
	int min, max;
	LineCfgArg_Int(int& v, int max, int min): v(v), min(min), max(max) {}
	LineCfgArgError read(std::string_view s);
	void write(std::string& s) const;
};
struct LineCfgArg_Float {
	static constexpr float eps = 1e-5; // used for comparison
	float& v;
	float min, max;
	LineCfgArg_Float(float& v, float max, float min): v(v), min(min), max(max) {}
	LineCfgArgError read(std::string_view s);
	void write(std::string& s) const;
};
struct LineCfgArg_Bool {
	bool& v;
	LineCfgArg_Bool(bool& v): v(v) {}
	LineCfgArgError read(std::string_view s);
	void write(std::string& s) const;
};
struct LineCfgArg_Str {
	std::string& v;
	LineCfgArg_Str(std::string& v): v(v) {}
	LineCfgArgError read(std::string_view s);
	void write(std::string& s) const;
};
struct LineCfgArg_Enum {
	void* p;
	std::shared_ptr<LineCfgEnumType> type;
	LineCfgArg_Enum(void* p,std::shared_ptr<LineCfgEnumType> type): p(p), type(std::move(type)) {}
	LineCfgArgError read(std::string_view s);
	void write(std::string& s) const;
	LineCfgEnumType::Base get_int() const {return type->man_get(p);}
};
using LineCfgArg = std::variant<LineCfgArg_Int, LineCfgArg_Float, LineCfgArg_Bool, LineCfgArg_Str, LineCfgArg_Enum>;



struct LineCfgOption {
	bool changed = false; ///< Not set internally; used for saving changes with LineCfg::write_only_present set
	
	LineCfgOption(std::string name, bool optional = true)
		: name(std::move(name)), optional(optional)
	{}
	
	LineCfgOption& vint  (int&   v, int max = std::numeric_limits<int>::max(), int min = 0);
	LineCfgOption& vfloat(float& v, float max = 1, float min = 0);
	LineCfgOption& vbool (bool&  v);
	LineCfgOption& vstr  (std::string& v);
	LineCfgOption& descr (std::string  v);
	
	template<typename T>
	LineCfgOption& venum(T& v, std::shared_ptr<LineCfgEnumType> type) {
		if (!type) throw std::logic_error("LineCfgOption::venum() null type");
		if (type->type != typeid(T)) throw std::logic_error("LineCfgOption::venum() type mismatch");
		args.emplace_back(LineCfgArg_Enum(static_cast<void*>(&v), std::move(type)));
		return *this;
	}
	
	std::string_view get_name()  const {return name;}
	std::string_view get_descr() const {return descr_v;}
	std::vector<LineCfgArg>& get_args() {return args;}
	
private:
	friend struct LineCfg;
	std::string name;
	bool optional;
	std::vector<LineCfgArg> args;
	int line = 0;
	std::string descr_v;
};



struct LineCfg
{
	bool save_comments = true; ///< If set, comments are preserved on reading and written on write()
	bool ignore_unknown = false; ///< If set, doesn't fail on reading unknown option
	bool write_only_present = false; ///< If set, writes only options which were read or which are marked as changed
	
	LineCfg(std::vector<LineCfgOption> opts): opts(std::move(opts)) {}
	
	void read_s(std::string_view file); ///< Throws on error
	std::string write_s(std::string s = {}) const; ///< Throws on error
	
	bool read(const char *filename); ///< Returns false on error
	bool write(const char *filename) const; ///< Returns false on error
	
	std::vector<LineCfgOption>& get_opts() {return opts;}
	
private:
	std::vector<LineCfgOption> opts;
	std::vector<std::pair<int, std::string>> comments;
};

#endif // LINE_CFG_HPP
