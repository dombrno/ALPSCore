/*****************************************************************************
*
* ALPS Project: Algorithms and Libraries for Physics Simulations
*
* ALPS Libraries
*
* Copyright (C) 2001-2003 by Matthias Troyer <troyer@itp.phys.ethz.ch>,
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

#ifndef ALPS_LATTICE_LIBRARY_H
#define ALPS_LATTICE_LIBRARY_H

#include <alps/config.h>
#include <alps/lattice/latticegraph.h>
#include <alps/lattice/latticegraphdescriptor.h>
#include <alps/lattice/latticedescriptor.h>
#include <alps/lattice/graph.h>
#include <alps/parser/xmlstream.h>

#include <fstream>

namespace alps {

class LatticeLibrary
{
public:
  LatticeLibrary() {};
  LatticeLibrary(std::istream& in) { read_xml(in);}
  LatticeLibrary(const XMLTag& tag, std::istream& p) {read_xml(tag,p);}
  LatticeLibrary(const Parameters& p);
  void read_xml(std::istream& in);
  void read_xml(const XMLTag& tag, std::istream& p);

  void write_xml(oxstream&) const;
  
  bool has_graph(const std::string& name) const;
  bool has_lattice(const std::string& name) const;
  
  const LatticeGraphDescriptor& lattice_descriptor(const std::string& name) const;
  hypercubic_lattice<coordinate_lattice<simple_lattice<GraphUnitCell> > > lattice(const std::string& name) const;
  const coordinate_graph_type& graph(const std::string& name) const;
  
  template <class G>
  bool get_graph(G& graph,const std::string& name) const;
  
  void make_all_graphs();

private:
  typedef std::map<std::string,LatticeGraphDescriptor> LatticeGraphMap;
  typedef std::map<std::string,coordinate_graph_type> GraphMap;

  LatticeMap lattices_;
  FiniteLatticeMap finitelattices_;
  UnitCellMap unitcells_;
  LatticeGraphMap latticegraphs_;
  GraphMap graphs_;
};

template <class G=coordinate_graph_type>
class graph_factory : public LatticeLibrary
{
public:
  typedef G graph_type;
  graph_factory() : g_(0), to_delete_(false) {}
  graph_factory(std::istream& in) : LatticeLibrary(in), g_(0), to_delete_(false) {}
  graph_factory(std::istream& in, const Parameters& parm) 
    : LatticeLibrary(in), g_(0), to_delete_(false) { make_graph(parm);}
  graph_factory(const Parameters& parm);
  ~graph_factory() { if (to_delete_) delete g_;}

  void make_graph(const Parameters& p);
  graph_type& graph()
  {
    if (g_==0) boost::throw_exception(std::runtime_error("no graph created in graph_factory"));
    return *g_;
  }
  const graph_type& graph() const
  {
    if (g_==0) boost::throw_exception(std::runtime_error("no graph created in graph_factory"));
    return *g_;
  }

private:
  typedef lattice_graph<hypercubic_lattice<coordinate_lattice<simple_lattice<GraphUnitCell> > >,graph_type> lattice_type;
  graph_type* g_;
  bool to_delete_;
  lattice_type l_;
};

template <class G>
inline bool LatticeLibrary::get_graph(G& g, const std::string& name) const
{
  if (!has_graph(name))
    return false;
  else
  {
    copy_graph(const_cast<GraphMap&>(graphs_)[name],g);
    return true;
  }
}

template <class G>
inline graph_factory<G>::graph_factory(const Parameters& parms)
 : LatticeLibrary(parms), g_(0), to_delete_(false)
{
  make_graph(parms);
}

template <class G>
inline void
graph_factory<G>::make_graph(const Parameters& parms)
{
  std::string name;
  bool have_graph=false;
  bool have_lattice=false;
  
  if (have_graph = parms.defined("GRAPH"))
    name = static_cast<std::string>(parms["GRAPH"]);
  if (have_lattice = parms.defined("LATTICE"))
    name = static_cast<std::string>(parms["LATTICE"]);
  if (have_lattice && have_graph)
    boost::throw_exception(std::runtime_error("both GRAPH and LATTICE were specified"));
  if (have_lattice && has_lattice(name)) {
    LatticeGraphDescriptor desc(lattice_descriptor(name));
    desc.set_parameters(parms);
    l_ = lattice_type(desc);
    if (to_delete_)
      delete g_;
    g_ = &(l_.graph());
    to_delete_=false;
  }
  else if ((have_lattice || have_graph) && has_graph(name)) {
    if (to_delete_)
      delete g_;
    g_= new graph_type();
    get_graph(*g_,name);
    to_delete_=true;
  }
  else
    boost::throw_exception(std::runtime_error("could not find graph/lattice specified in parameters"));
}

} // end namespace alps

#ifndef BOOST_NO_OPERATORS_IN_NAMESPACE
namespace alps {
#endif

inline alps::oxstream& operator<<(alps::oxstream& xml, const alps::LatticeLibrary& l)
{
  l.write_xml(xml);
  return xml;
}

inline std::ostream& operator<<(std::ostream& os, const alps::LatticeLibrary& l)
{
  oxstream xml(os);
  xml << l;
  return os;
}

inline std::istream& operator>>(std::istream& is, alps::LatticeLibrary& l)
{
  l.read_xml(is);
  return is;
}

#ifndef BOOST_NO_OPERATORS_IN_NAMESPACE
} // end namespace alps
#endif

#endif // ALPS_LATTICE_LIBRARY_H
