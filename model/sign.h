/*****************************************************************************
*
* ALPS Project: Algorithms and Libraries for Physics Simulations
*
* ALPS Libraries
*
* Copyright (C) 2003-2004 by Matthias Troyer <troyer@itp.phys.ethz.ch>,
*                            Synge Todo <wistaria@comp-phys.org>
*
* This software is part of the ALPS libraries, published under the ALPS
* Library License; you can use, redistribute it and/or modify it under
* the terms of the license, either version 1 or (at your option) any later
* version.
* 
* You should have received a copy of the ALPS Library License along with
* the ALPS Libraries; see the file LICENSE.txt. If not, the license is also
* available from http://alps.comp-phys.org/.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
* FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT 
* SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE 
* FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE, 
* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
* DEALINGS IN THE SOFTWARE.
*
*****************************************************************************/

/* $Id$ */

#ifndef ALPS_MODEL_SIGN_H
#define ALPS_MODEL_SIGN_H

#include <alps/lattice.h>
#include <alps/model/modellibrary.h>

#include <boost/graph/filtered_graph.hpp>
#include <boost/graph/undirected_dfs.hpp>
#include <boost/graph/visitors.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/vector_property_map.hpp>

namespace alps {

namespace parity {

template<class Graph, class PropertyMap, class BondPropertyMap>
class SignVisitor : public boost::dfs_visitor<>
{
public:
  typedef typename boost::graph_traits<Graph>::vertex_descriptor
    vertex_descriptor;
  typedef typename boost::graph_traits<Graph>::edge_descriptor
    edge_descriptor;

  SignVisitor(PropertyMap& map, bool* check, BondPropertyMap bondsign) :
    map_(map), check_(check), bond_sign_(bondsign) { *check_ = false; }

  void initialize_vertex(vertex_descriptor s, const Graph&) { map_[s] = 0; }
  void start_vertex(vertex_descriptor s, const Graph&) { map_[s] = 1; }
  void tree_edge(edge_descriptor e, const Graph& g)
  {
    if (map_[boost::target(e,g)] == 0) {
      if (bond_sign_[e] > 0) {
	map_[boost::target(e,g)] = map_[boost::source(e,g)];
      } else {
	map_[boost::target(e,g)] = -map_[boost::source(e,g)];
      }
    } else {
      if (bond_sign_[e] > 0) {
	map_[boost::source(e,g)] = map_[boost::target(e,g)];
      } else {
	map_[boost::source(e,g)] = -map_[boost::target(e,g)];
      }
    }
  }
  void back_edge(edge_descriptor e, const Graph& g) { check(e, g); }

protected:
  void check(edge_descriptor e, const Graph& g)
  {
    if (bond_sign_[e]*map_[boost::source(e,g)]*map_[boost::target(e,g)] < 0) 
      *check_ = true;
  }

private:
  PropertyMap map_;
  bool* check_;
  BondPropertyMap bond_sign_;
};

template<class Graph, class PropertyMap, class BondPropertyMap>
SignVisitor<Graph,PropertyMap,BondPropertyMap> make_sign_visitor(const Graph&, PropertyMap& map, bool* check, BondPropertyMap bondsign)
{
  return SignVisitor<Graph,PropertyMap,BondPropertyMap>(map,check,bondsign);
}

template <class G>
class BondMap {
public:
  typedef G graph_type;
  typedef std::map<boost::tuple<int,int,int>,int> map_type;
  BondMap() {}
  BondMap(const map_type& map, const graph_type& graph)
    : site_type_(alps::get_or_default(alps::site_type_t(), graph, 0)),
      bond_type_(alps::get_or_default(alps::bond_type_t(), graph, 0)),
      map_(&map),
      graph_(&graph)
  {}
            
  template <class E>
  int operator[] (const E& e) const {
    return const_cast<map_type&>(*map_)[boost::tie(bond_type_[e], site_type_[boost::source(e,*graph_)], site_type_[boost::target(e,*graph_)])];
}
private:
  typedef typename alps::property_map<alps::site_type_t, graph_type, int>::const_type site_type_map_t;
  typedef typename alps::property_map<alps::bond_type_t, graph_type, int>::const_type bond_type_map_t;

  site_type_map_t site_type_;
  bond_type_map_t bond_type_;
  const map_type* map_;
  const graph_type* graph_;
};
}

template <typename EdgeWeightMap>
struct nonzero_edge_weight {
  nonzero_edge_weight() { }
  nonzero_edge_weight(EdgeWeightMap weight) : m_weight(weight) { }
  template <typename Edge>
  bool operator()(const Edge& e) const {
    return m_weight[e]!=0;
  }
  EdgeWeightMap m_weight;
};

template <class G, class M>
bool is_frustrated(const G& graph, M bond_map)
{  
  typedef G graph_type;
  boost::filtered_graph<graph_type, nonzero_edge_weight<M> >
    g(graph, nonzero_edge_weight<M>(bond_map));
  boost::vector_property_map<int> map; // map to store the relative signs of the sublattices
  bool check=false; // no sign problem
  std::vector<boost::default_color_type> vcolor_map(boost::num_vertices(g));
  std::vector<boost::default_color_type> ecolor_map(boost::num_edges(g));
  boost::undirected_dfs(
    g,
    parity::make_sign_visitor(g, map, &check, bond_map),
    boost::make_iterator_property_map(vcolor_map.begin(),
      boost::get(vertex_index_t(), g)),
    boost::make_iterator_property_map(ecolor_map.begin(),
      boost::get(edge_index_t(), g)));
  return check; // no sign problem=>not frustrated
}
                                 
template <class I, class G>
bool has_sign_problem(const HamiltonianDescriptor<I>& ham, const graph_helper<G>& lattice, const Parameters& p) {
  typedef G graph_type;
  const graph_type& graph(lattice.graph());
  
  if (lattice.disordered_bonds())
    boost::throw_exception(std::runtime_error("Disordered bonds on lattice not currently supported by the sign check program. Please contact the ALPS developers for assistance.\n"));

  // build and check bond matrices for all bond types
  std::map<boost::tuple<int,int,int>,int> bond_sign;
  for (typename boost::graph_traits<graph_type>::edge_iterator
         it=boost::edges(graph).first; it!=boost::edges(graph).second ; ++it) {
//    int dbtype = lattice.disordered_bond_type(*it);
    int btype  = lattice.bond_type(*it);
    int stype1 = lattice.site_type(lattice.source(*it));
    int stype2 = lattice.site_type(lattice.target(*it));
    if (bond_sign.find(boost::make_tuple(btype,stype1,stype2)) ==
        bond_sign.end()) {
      boost::multi_array<double,4> mat = get_matrix(0.,ham.bond_term(btype),ham.basis().site_basis(stype1),
                                        ham.basis().site_basis(stype2),p);
      int dim1 = mat.shape()[0];
      int dim2 = mat.shape()[1];
      int sign=0;
      for (int i1=0;i1<dim1;++i1)
        for (int j1=0;j1<dim2;++j1)
          for (int i2=0;i2<dim1;++i2)
            for (int j2=0;j2<dim2;++j2)
              if (i1!=i2 && j1 !=j2) {
            int this_sign=(mat[i1][j1][i2][j2] < -1.0e-10 ? -1 : (mat[i1][j1][i2][j2] > 1.0e-10 ? 1 : 0));
            if (!sign) // the first nonzero matrix element
              sign = this_sign; // is stored
            else if (this_sign && sign!=this_sign) // compare other nonzero matrix elements
              return true; // we might have a sign problem: indefinite sign of matrix elements 
          }
      bond_sign[boost::make_tuple(btype,stype1,stype2)]=-sign;
      bond_sign[boost::make_tuple(btype,stype2,stype1)]=-sign;
    }
  }

  // determine "parity" of lattice w.r.t. bond signs

  parity::BondMap<graph_type> bond_map(bond_sign,graph);
  return is_frustrated(graph,bond_map);
}


} // namespace alps

#endif // ALPS_MODEL_SIGN_H
