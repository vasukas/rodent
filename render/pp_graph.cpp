#include "vaslib/vas_log.hpp"
#include "pp_graph.hpp"



class PP_Graph_Impl : public PP_Graph
{
public:
	struct NodeInfo
	{
		// object
		std::unique_ptr<PP_Node> ptr;
		size_t index;
		
		// connections
		std::optional<size_t> target;
		int ordering = 0;
		
		// status & rebuild
		bool enabled = false;
		
		struct rebuild_t
		{
			bool loopdet = false;
			bool proc = false;
			
			std::optional<size_t> i_tar;
			
			size_t prov_num = 0; // total input count
			size_t ready = 0; // current ready inputs
		};
		rebuild_t reb;
	};
	
	std::vector<NodeInfo> nodes;
	std::vector<NodeInfo*> ord;
	bool rebuild_req = false;
	
	
	
	void add_node(PP_Node* node) override
	{
		for (auto& n : nodes) {
			if (n.ptr->name == node->name)
				THROW_FMTSTR("PP_Graph::add_node() name already used - {}", node->name);
		}
		
		auto& n = nodes.emplace_back();
		n.ptr.reset(node);
		n.index = nodes.size() - 1;
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
		if (!n_in.ptr->has_input || !n_out.ptr->has_output)
			THROW_FMTSTR("PP_Graph::connect() invalid connection ({} -> {})", out_name, in_name);
		
		if (n_out.target) {
			if (*n_out.target == n_in.index) return;
			THROW_FMTSTR("PP_Graph::connect() already has output ({} -> {})", out_name, in_name);
		}
		
		n_out.target = n_in.index;
		n_out.ordering = ordering;
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
		{
			rebuild_req = false;
			rebuild();
		}
		
		for (auto& s : ord)
		{
			auto i = s->reb.i_tar;
			auto fbo = i? nodes[*i].ptr->get_input_fbo() : 0;
			s->ptr->proc(fbo);
		}
	}
	
	
	
	void rebuild_sub_1(NodeInfo& n, std::optional<size_t>& target)
	{
		if (n.reb.loopdet) THROW_FMTSTR("PP_Graph::rebuild() detected loop at - {}", n.ptr->name);
		n.reb.loopdet = true;
		
		if (n.enabled)
		{
			target = n.index;
			
			if (!n.reb.proc)
			{
				n.reb.proc = true;
				if (n.target)
				{
					rebuild_sub_1(nodes[*n.target], n.reb.i_tar);
					++ nodes[*n.reb.i_tar].reb.prov_num;
				}
			}
		}
		else if (n.target)
		{
			rebuild_sub_1(nodes[*n.target], target);
		}
		
		n.reb.loopdet = false;
	}
	void rebuild_sub_2(NodeInfo& n)
	{
		if (!n.ptr->has_input || ++ n.reb.ready == n.reb.prov_num)
		{
			ord.push_back(&n);
			if (n.reb.i_tar)
				rebuild_sub_2(nodes[*n.reb.i_tar]);
		}
	}
	void rebuild()
	{
		// reset
		
		ord.clear();
		ord.reserve( nodes.size() );
		
		for (auto& n : nodes)
			n.reb = NodeInfo::rebuild_t();
		
		// remove non-enabled nodes
		
		bool has_entry = false;
		for (auto& n : nodes)
		{
			if (n.enabled && !n.ptr->has_input)
			{
				std::optional<size_t> tar; // ignored
				rebuild_sub_1(n, tar);
				has_entry = true;
			}
		}
		if (!has_entry)
			THROW_FMTSTR("PP_Graph::rebuild() no entry nodes");
		
		// add in readiness order
	
		for (auto& n : nodes)
		{
			if (n.enabled && !n.ptr->has_input)
				rebuild_sub_2(n);
		}
		
		// adjust ordering
		
		for (size_t i = 0;   i < ord.size(); ++i)
		for (size_t j = i+1; j < ord.size(); ++j)
		{
			auto& na = ord[i];
			auto& nb = ord[j];
			
			if (na->reb.i_tar == nb->reb.i_tar &&
			    na->reb.i_tar &&
			    na->ordering > nb->ordering
			    )
				std::swap(na, nb);
		}
		
		//
		
//		debug_dump();
	}
	
	
	
	void debug_dump()
	{
		auto put = [this](NodeInfo& n)
		{
			VLOGV("{} {} -> {}/{} [{}]",
			      n.enabled? ' ' : 'x',
			      n.ptr->name,
			      n.target? nodes[*n.target].ptr->name : "NONE",
			      n.reb.i_tar? nodes[*n.reb.i_tar].ptr->name : "NONE",
			      n.ordering);
		};
		
		VLOGV("=== PP_Graph dump ===");
		for (auto& s : ord) put(*s);
		for (auto& n : nodes) if (!n.enabled) put(n);
		VLOGV("=== End dump ===");
	}
};
PP_Graph* PP_Graph::create() {
	return new PP_Graph_Impl;
}



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
		}
	}
	, true);
}
bool PPN_Chain::prepare()
{
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
	
	glActiveTexture(GL_TEXTURE0);
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