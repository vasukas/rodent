#include "vaslib/vas_log.hpp"
#include "pp_graph.hpp"



PP_Node::PP_Node(std::string name, bool needs_input, bool needs_output)
	: name(std::move(name)), needs_input(needs_input), needs_output(needs_output)
{
	PP_Graph::get().add_node(this);
}



class PP_Graph_Impl : public PP_Graph
{
public:
	struct NodeDeleter {void operator()(PP_Node* p) {delete p;}};
	struct Node
	{
		// input
		std::unique_ptr<PP_Node, NodeDeleter> ptr;
		std::optional<int> output;
		int order = 0;
		bool enabled = false;
		
		// output
		bool is_used;
		int res; // real output
		
		// resolving data
		int self;
		bool loopflag;
		std::vector<int> inputs;
	};
	
	std::vector<Node> nodes;
	std::vector<Node*> ord;
	bool rebuild_req = false;
	
	
	
	~PP_Graph_Impl()
	{
		nodes.clear();
	}
	void connect(std::string out_name, std::string in_name, int ordering) override
	{
		auto getn = [&](auto& s) -> auto&
		{
			auto it = std::find_if(nodes.begin(), nodes.end(), [&](auto&& v){ return v.ptr->name == s; });
			if (it == nodes.end()) THROW_FMTSTR("PP_Graph::connect() node not found - {} ({} -> {})", s, out_name, in_name);
			return *it;
		};
		
		auto& n_out = getn(out_name);
		auto& n_in  = getn(in_name);
		if (!n_in.ptr->needs_input || !n_out.ptr->needs_output)
			THROW_FMTSTR("PP_Graph::connect() invalid connection ({} -> {})", out_name, in_name);
		
		if (n_out.output) {
			if (*n_out.output == n_in.self) return;
			THROW_FMTSTR("PP_Graph::connect() already has output ({} -> {})", out_name, in_name);
		}
		
		n_out.output = n_in.self;
		n_out.order = ordering;
	}
	void render() override
	{
		for (auto& n : nodes)
		{
			bool ok = n.ptr->prepare();
			if (ok != n.enabled)
			{
				n.enabled = ok;
				rebuild_req = true;
			}
		}
		
		if (rebuild_req)
			rebuild();
		
		for (auto& s : ord)
		{
			auto fbo = s->res >= 0 ? nodes[s->res].ptr->get_input_fbo() : 0;
			s->ptr->proc(fbo);
		}
	}
	void add_node(PP_Node* node) override
	{
		for (auto& n : nodes) {
			if (n.ptr->name == node->name)
				THROW_FMTSTR("PP_Graph::add_node() name already used - {}", node->name);
		}
		
		auto& n = nodes.emplace_back();
		n.ptr.reset(node);
		n.self = nodes.size() - 1;
	}
	
	
	void rebuild()
	{
		rebuild_req = false;
		
		ord.clear();
		ord.reserve( nodes.size() );
		
		for (auto& n : nodes) {
			n.is_used = false;
			n.res = -2;
			n.loopflag = false;
			n.inputs.clear();
		}
		
		//
		
		auto proc = [&](Node& n, auto proc)-> bool
		{
			if (n.loopflag) THROW_FMTSTR("Loop detected");
			n.loopflag = true;
			
			for (auto& b : nodes) {
				if (b.output == n.self)
					n.inputs.push_back(b.self);
			}
			if (!n.inputs.empty())
			{
				for (auto& i : n.inputs) {
					int k = 0;
					for (auto& j : n.inputs) if (nodes[i].order == nodes[j].order) ++k;
					if (k > 1) THROW_FMTSTR("Order repeat");
				}
				std::sort(n.inputs.begin(), n.inputs.end(), [&](auto& a, auto& b) {return nodes[a].order < nodes[b].order;});
				
				bool ok = false;
				for (auto& i : n.inputs) {
					ok |= proc(nodes[i], proc);
				}
				if (ok) {
					if (n.enabled) {
						n.is_used = true;
						ord.push_back(&n);
					}
					return true;
				}
			}
			if (!n.enabled) return false;
			if (!n.ptr->needs_input) {
				n.is_used = true;
				ord.push_back(&n);
				return true;
			}
			return false;
		};
		auto set = [&](Node& n, int root, auto set)-> void {
			n.res = root;
			for (auto& i : n.inputs)
				set(nodes[i], n.is_used ? n.self : root, set);
		};
		
		for (auto& n : nodes) {
			if (!n.ptr->needs_output && n.enabled && proc(n, proc))
				set(n, -1, set);
		}
		
		//
		
//		debug_dump();
	}
	
	
	
	void debug_dump()
	{
		auto put = [this](Node& n)
		{
			VLOGV("{} {} -> {}[{}] / {}",
			      n.enabled? (n.is_used? ' ' : 'd') : 'x',
			      n.ptr->name,
			      n.output? nodes[*n.output].ptr->name : "NONE",
			      n.order,
			      n.res >= 0? nodes[n.res].ptr->name : "NONE");
		};
		
		VLOGV("=== PP_Graph dump ===");
		for (auto& s : ord) put(*s);
		for (auto& n : nodes) if (n.enabled && !n.is_used) put(n);
		for (auto& n : nodes) if (!n.enabled) put(n);
		VLOGV("=== End dump ===");
	}
};



static PP_Graph* rni;
PP_Graph& PP_Graph::get() {
	if (!rni) LOG_THROW_X("RenImm::get() null");
	return *rni;
}
PP_Graph* PP_Graph::init() {return rni = new PP_Graph_Impl;}
PP_Graph::~PP_Graph() {rni = nullptr;}



PPN_Chain::PPN_Chain(std::string name, std::vector<std::unique_ptr<PP_Filter>> fts_in, std::function<void()> pre_draw_in)
    : PP_Node(name), fts(std::move(fts_in)), pre_draw(std::move(pre_draw_in))
{
	for (auto& f : fts) f->_chain = this;
	rsz_g = RenderControl::get().add_size_cb([this]
	{
		for (int i=0; i<2; ++i)
		{
			tex_s[i].set(GL_RGBA, RenderControl::get_size(), 0, 4);
			fbo_s[i].attach_tex(GL_COLOR_ATTACHMENT0, tex_s[i]);
			fbo_s[i].check_throw(FMT_FORMAT("PPN_Chain({}) {}", this->name, i));
		}
	});
}
bool PPN_Chain::prepare()
{
	if (is_enabled && !is_enabled())
		return false;
	
	bool any = false;
	for (auto& f : fts) if (f->enabled && f->is_ok()) {any = true; break;}
	
	clear_fbo = true;
	return any;
}
void PPN_Chain::proc(GLuint output_fbo)
{
	fbo_out = output_fbo;
	glClearColor(0, 0, 0, 0);
	bs_index = true;
	
	RenderControl::get().ndc_screen2().bind();
	
	size_t last = fts.size() - 1;
	for (; last; --last) if (fts[last]->enabled && fts[last]->is_ok()) break;
	
	bs_index = true;
	for (size_t i=0; i <= last; ++i)
	{
		auto& f = fts[i];
		if (f->enabled && f->is_ok())
		{
			bs_last = (i == last);
			f->proc();
		}
	}
}
GLuint PPN_Chain::get_input_fbo()
{
	if (clear_fbo)
	{
		clear_fbo = false;
		
		fbo_s[0].bind();
		glClearColor(0, 0, 0, 0);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	return fbo_s[0].fbo;
}
void PPN_Chain::draw(bool is_last)
{
	if (is_last && bs_last) {
		if (pre_draw) pre_draw();
		glBindFramebuffer(GL_FRAMEBUFFER, fbo_out);
	}
	else {
		fbo_s[bs_index].bind();
		glClear(GL_COLOR_BUFFER_BIT);
	}
	
	tex_s[!bs_index].bind();
	glDrawArrays(GL_TRIANGLES, 0, 6);
	bs_index = !bs_index;
}



PPN_InputDraw::PPN_InputDraw(std::string name, Middle_FBO mid_fbo, std::function<void(GLuint fbo)> func)
    : PP_Node(name, false, true), func(std::move(func))
{
	pass = Shader::load("pp/pass", {}, true);
	if (mid_fbo)
	{
		fbo.emplace();
		fbo_g = RenderControl::get().add_size_cb([this, mid_fbo, name]
		{
			if (fbo->em_texs.empty()) fbo->em_texs.emplace_back();
			fbo->em_texs[0].set(GL_RGBA, RenderControl::get_size(), 0, 4);
			fbo->attach_tex(GL_COLOR_ATTACHMENT0, fbo->em_texs[0]);
			
			if (mid_fbo == MID_DEPTH_STENCIL)
			{
				if (fbo->em_rbufs.empty()) fbo->em_rbufs.emplace_back();
				fbo->em_rbufs[0].set(GL_DEPTH24_STENCIL8, RenderControl::get_size());
				fbo->attach_rbf(GL_DEPTH_STENCIL_ATTACHMENT, fbo->em_rbufs[0]);
			}
			
			fbo->check_throw(FMT_FORMAT("PPN_InputDraw - {}", name));
		});
	}
}
void PPN_InputDraw::proc(GLuint fbo_out)
{
	if (!fbo) func(fbo_out);
	else
	{
		fbo->bind();
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		
		func(fbo->fbo);
		
		//
		
		glBindFramebuffer(GL_FRAMEBUFFER, fbo_out);
		pass->bind();
		
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glBlendEquation(GL_FUNC_ADD);
		
		glActiveTexture(GL_TEXTURE0);
		fbo->em_texs[0].bind();
		
		RenderControl::get().ndc_screen2().bind();
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}
}
