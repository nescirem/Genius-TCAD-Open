/********************************************************************************/
/*     888888    888888888   88     888  88888   888      888    88888888       */
/*   8       8   8           8 8     8     8      8        8    8               */
/*  8            8           8  8    8     8      8        8    8               */
/*  8            888888888   8   8   8     8      8        8     8888888        */
/*  8      8888  8           8    8  8     8      8        8            8       */
/*   8       8   8           8     8 8     8      8        8            8       */
/*     888888    888888888  888     88   88888     88888888     88888888        */
/*                                                                              */
/*       A Three-Dimensional General Purpose Semiconductor Simulator.           */
/*                                                                              */
/*                                                                              */
/*  Copyright (C) 2007-2008                                                     */
/*  Cogenda Pte Ltd                                                             */
/*                                                                              */
/*  Please contact Cogenda Pte Ltd for license information                      */
/*                                                                              */
/*  Author: Gong Ding   gdiso@ustc.edu                                          */
/*                                                                              */
/********************************************************************************/

//  $Id: control.cc,v 1.54 2008/07/09 12:56:23 gdiso Exp $

#include "genius_common.h"

#ifdef WINDOWS
    #include <io.h>
#else
    #include <unistd.h>
#endif

#include "parser.h"


#include "mesh_generation_tri3.h"
#include "mesh_generation_quad4.h"

// only commercial product support 3D mesh generator
#ifdef COGENDA_COMMERCIAL_PRODUCT
  #include "mesh_generation_hex8.h"
  #include "mesh_generation_prism6.h"
  #include "mesh_generation_tet4.h"
  #include "mesh_generation_cy2d.h"
  #include "mesh_generation_cy3d.h"
#endif

#include "control.h"
#include "mesh_tools.h"
#include "mesh_communication.h"
#include "mesh_refinement.h"
#include "mesh_modification.h"
#include "boundary_info.h"
#include "electrical_source.h"
#include "field_source.h"
#include "enum_solution.h"
#include "spice_ckt.h"


#ifdef TCAD_SOLVERS
#include "doping_analytic/doping_analytic.h"
#include "mole_analytic/mole_analytic.h"
#include "poisson/poisson.h"
#include "ddm1/ddm1.h"
#include "ddm2/ddm2.h"
#include "ebm3/ebm3.h"
#include "dg/dg.h"
#include "ddm_ac/ddm_ac.h"
#include "mixA1/mixA1.h"
#include "mixA2/mixA2.h"
#include "mixA3/mixA3.h"
#include "mix1/mix1.h"
#include "hall/hall.h"
// only commercial product support half implicit method
#ifdef COGENDA_COMMERCIAL_PRODUCT
  #include "halfimplicit/ddm1_half_implicit.h"
#endif
#endif

#ifdef IDC_SOLVERS
#ifdef COGENDA_COMMERCIAL_PRODUCT
  #include "ric/ric.h"
  #include "dictat/dictat.h"
#endif
#endif

#ifdef COGENDA_COMMERCIAL_PRODUCT
  #include "tid/tid_op.h"
  #include "tid/tid_trap.h"
#endif

#include "stress_solver/stress_solver.h"


#include "solver_specify.h"
#include "advanced_model.h"
#include "petsc_type.h"

#include "interpolation_2d_csa.h"
#include "interpolation_3d_qshep.h"
#include "interpolation_3d_nbtet.h"

#include "dlhook.h"
#ifndef DLLHOOK
 #include "cv_hook.h"
 #include "rawfile_hook.h"
 #include "gnuplot_hook.h"
 #include "probe_hook.h"
 #include "vtk_hook.h"
 #include "threshold_hook.h"
 #include "tunneling_hook.h"
 #include "data_hook.h"
 #include "cgns_hook.h"
#endif


#include "extend_to_3d.h"
#include "rotate_to_3d.h"
#ifdef HAVE_X11
 #include "show_mesh_2d.h"
#endif

#include "parallel.h"
#include "MXMLUtil.h"
#include "TRexpp.h"

using PhysicalUnit::A;
using PhysicalUnit::V;
using PhysicalUnit::W;
using PhysicalUnit::C;
using PhysicalUnit::s;
using PhysicalUnit::um;
using PhysicalUnit::cm;
using PhysicalUnit::rad;

//------------------------------------------------------------------------------


//------------------------------------------------------------------------------
SolverControl::SolverControl()
    : _decks(NULL), _mesh(NULL), _system(NULL)
{
  _dom_solution = mxmlNewXML("1.0");
  mxmlNewElement(_dom_solution, "genius-solutions");
}

SolverControl::~SolverControl()
{
  mxmlDelete(_dom_solution);
  _dom_solution=NULL;
}

mxml_node_t* SolverControl::get_dom_solution() const
{
  return _dom_solution;
}

int SolverControl::get_dom_solution_count() const
{
  int cnt=0;
  mxml_node_t* root = mxmlFindElement(_dom_solution, _dom_solution, "genius-solutions", NULL, NULL, MXML_DESCEND_FIRST);
  if (!root)
    return 0;

  for(mxml_node_t* node = mxmlFindElement(root, root, "solution", NULL, NULL, MXML_DESCEND_FIRST);
      node; node = mxmlFindElement(node, root, "solution", NULL, NULL, MXML_NO_DESCEND) )
  {
    cnt++;
  }
  return cnt;
}



void SolverControl::setDecks(Parser::InputParser *input)
{
  _decks = input;
}

int SolverControl::reset_simulation_system()
{
  if (_decks == NULL)
    return 0;

  _mesh = AutoPtr<Mesh>(new Mesh(3));
  _system = AutoPtr<SimulationSystem>(new SimulationSystem(mesh(), decks()));
  return 0;
}

//------------------------------------------------------------------------------
int SolverControl::mainloop()
{
  if (_decks == NULL)
    return 0;

  if (_mesh.get() == NULL || _system.get() == NULL)
    reset_simulation_system();

  // first, we should see if mesh generation card exist
  if ( decks().is_card_exist("MESH") )
  {
    // generate simple device mesh
    this->do_mesh();

    // then, we should see if doping profile and/or mole card exist
    this->do_process();
  }

  // from above tow steps, maybe the simulation system has been build.
  // if not, user should use IMPORT command to get an (previous) system into memory.

  // we can begin the main loop here
  for( decks().begin(); !decks().end(); decks().next() )
  {

    Parser::Card c = decks().get_current_card();

    if(c.key() == "MODEL")
      this->set_model ( c );

    if(c.key() == "METHOD")
      this->set_method ( c );

    if(c.key() == "HOOK")
      this->do_hook( c );

    if(c.key() == "SOLVE")
      this->do_solve( c );

    if(c.key() == "EXPORT")
      this->do_export( c );

    if(c.key() == "IMPORT")
      this->do_import( c );

    if(c.key() == "NODESET")
      this->set_initial_node_voltage( c );

    if(c.key() == "REFINE.CONFORM")
      this->do_refine_conform( c );

    if(c.key() == "REFINE.HIERARCHICAL")
      this->do_refine_hierarchical( c );

    if(c.key() == "REFINE.UNIFORM")
      this->do_refine_uniform( c );

    if(c.key() == "REGIONSET")
      this->do_region_set( c );

    if(c.key() == "BOUNDARYSET")
      this->do_boundary_set( c );

    if(c.key() == "PMI")
      this->set_physical_model ( c );

    if(c.key() == "TID")
      this->do_tid ( c );

    if(c.key() == "SOURCEAPPLY")
      this->apply_field_source ( c );

    if(c.key() == "ATTACH")
      this->set_electrode_source ( c );

    if(c.key() == "EXTEND")
      this->extend_to_3d( c );

    if(c.key() == "ROTATE")
      this->rotate_to_3d( c );

    if(c.key() == "PLOTMESH")
      this->plot_mesh( c );
  }

  return 0;
}


//------------------------------------------------------------------------------
int  SolverControl::do_mesh()
{

  if ( decks().is_card_exist("MESH") )
  {
    // build meshgenerator only on processor 0
    // I am afraid about mesh generator may have different
    // behavior due to float point round-off error
    if (Genius::processor_id() == 0)
    {
      // which mesh generator should we use?
      for( decks().begin(); !decks().end(); decks().next() )
      {
        Parser::Card c = decks().get_current_card();

        if(c.key() == "MESH")
        {
          if( c.is_enum_value("type","s_tri3"))
          {
            meshgen = AutoPtr<MeshGeneratorBase>(new MeshGeneratorTri3(mesh(),decks()));
          }
          else if( c.is_enum_value("type","s_quad4"))
          {
            meshgen = AutoPtr<MeshGeneratorBase>(new MeshGeneratorQuad4(mesh(),decks()));
          }
#ifdef COGENDA_COMMERCIAL_PRODUCT
          else if( c.is_enum_value("type","s_tet4"))
          {
            meshgen = AutoPtr<MeshGeneratorBase>(new MeshGeneratorTet4(mesh(),decks()));
          }
          else if( c.is_enum_value("type","s_prism6"))
          {
            meshgen = AutoPtr<MeshGeneratorBase>(new MeshGeneratorPrism6(mesh(),decks()));
          }
          else if( c.is_enum_value("type","s_hex8"))
          {
            meshgen = AutoPtr<MeshGeneratorBase>(new MeshGeneratorHex8(mesh(),decks()));
          }
          else if( c.is_enum_value("type","c_2d"))
          {
            meshgen = AutoPtr<MeshGeneratorBase>(new MeshGeneratorCylinder2D(mesh(),decks()));
          }
          else if( c.is_enum_value("type","c_3d"))
          {
            meshgen = AutoPtr<MeshGeneratorBase>(new MeshGeneratorCylinder3D(mesh(),decks()));
          }
#else
          else
          {
            MESSAGE<<"ERROR: 3D mesh generator is not supported by Open Source Version." << std::endl; RECORD();
            genius_error();
          }
#endif

        }
      }
      // ok, generate the mesh
      if ( meshgen->do_mesh() )
      {
        MESSAGE<<"ERROR: Mesh generation failed." << std::endl; RECORD();
        genius_error();
      }
    }

    // since we only build mesh on processor 0,
    // sync mesh to other processors.
    // this procedure also prepare the mesh for using
    MeshCommunication mesh_comm;
    mesh_comm.broadcast(mesh());

    // please note, until here, mesh is still not prepared
    // mesh.is_prepared() will return false

    // now we can build solution system since mesh is done
    system().build_simulation_system();
    system().sync_print_info();
  }

  //std::cout<<"Total " << Genius::memory_size().first/(1024*1024) << " " << Genius::memory_size().second/(1024*1024) <<std::endl;

  return 0;
}



int SolverControl::do_process()
{
#ifdef TCAD_SOLVERS
  // if doping profile card exist
  if ( decks().is_card_exist("DOPING") )
  {
    DopingSolver = AutoPtr<SolverBase>( new DopingAnalytic(system(), decks()) );
    // parse "PROFILE" card
    DopingSolver->create_solver();
    // set doping profile to semiconductor region
    DopingSolver->solve();
    // we will not destroy the doping solver here
  }

  // if mole card exist
  if ( decks().is_card_exist("MOLE") )
  {
    MoleSolver = AutoPtr<SolverBase>( new MoleAnalytic(system(), decks()) );
    // parse "MOLE" card
    MoleSolver->create_solver();
    // set mole fraction to semiconductor region
    MoleSolver->solve();
    // we will not destroy the mole solver here
  }
#endif

  // after doping profile and mole is set, we can init system data.
  // i.e. electron and hole initital concentration of semiconductor region

  // note: even no mesh and/or process are done, we can still call it safely
  // although it will do nothing
  system().init_region();
  system().init_region_post_process();

  return 0;
}




//------------------------------------------------------------------------------
int SolverControl::set_method ( const Parser::Card & c )
{
  // reset to default solver parameters
  SolverSpecify::set_default_parameter();

  // set nonlinear solver type
  SolverSpecify::NS = SolverSpecify::nonlinear_solver_type(c.get_string("ns", "basic"));

  // set linear solver type
  SolverSpecify::LS = SolverSpecify::linear_solver_type(c.get_string("ls", "gmres"));

  // set preconditioner type
  SolverSpecify::PC = SolverSpecify::preconditioner_type(c.get_string("pc", "lu"));

  // set preconditioner lag
  SolverSpecify::NSLagPCLU                  = c.get_int("pclu.lag", 5);
  // set jacobian lag
  SolverSpecify::NSLagJacobian              = c.get_int("jacobian.lag", 1);

  // set Newton damping type
  if(c.is_parameter_exist("damping"))
  {
    if (c.is_enum_value("damping", "no"))             SolverSpecify::Damping = SolverSpecify::DampingNo;
    if (c.is_enum_value("damping", "potential"))      SolverSpecify::Damping = SolverSpecify::DampingPotential;
    if (c.is_enum_value("damping", "superpotential")) SolverSpecify::Damping = SolverSpecify::DampingSuperPotential;
    if (c.is_enum_value("damping", "bankrose"))       SolverSpecify::Damping = SolverSpecify::DampingBankRose;
  }

  SolverSpecify::damping_spice             = c.get_bool("damping.spice", false);

   // set voronoi truncation flag
  if(c.is_parameter_exist("truncation"))
  {
    if (c.is_enum_value("truncation", "no"))            SolverSpecify::VoronoiTruncation = SolverSpecify::VoronoiTruncationNo;
    if (c.is_enum_value("truncation", "boundary"))      SolverSpecify::VoronoiTruncation = SolverSpecify::VoronoiTruncationBoundary;
    if (c.is_enum_value("truncation", "always"))        SolverSpecify::VoronoiTruncation = SolverSpecify::VoronoiTruncationAlways;
  }


  // set linear solver type for half implicit method
  SolverSpecify::LS_CARRIER = SolverSpecify::linear_solver_type(c.get_string("ls.carrier", "gmres"));
  SolverSpecify::LS_CURRENT = SolverSpecify::linear_solver_type(c.get_string("ls.current", c.get_string("ls", "bcgs")));
  SolverSpecify::LS_POISSON = SolverSpecify::linear_solver_type(c.get_string("ls.poisson", "gmres"));
  // set preconditioner type for half implicit method
  SolverSpecify::PC_CARRIER = SolverSpecify::preconditioner_type(c.get_string("pc.carrier", "asm"));
  SolverSpecify::PC_CURRENT = SolverSpecify::preconditioner_type(c.get_string("pc.current", c.get_string("pc", "asm")));
  SolverSpecify::PC_POISSON = SolverSpecify::preconditioner_type(c.get_string("pc.poisson", "asm"));

  SolverSpecify::DumpMatrixVector =  c.get_bool("halfimplicit.dumpsystem", false);

  // linearize error
  SolverSpecify::LinearizeErrorThreshold    = c.get_real("halfimplicit.let", 1.0);
  SolverSpecify::ArtificialCarrier          = c.get_bool("halfimplicit.artificialcarrier", true);
  SolverSpecify::ReSolveCarrier             = c.get_bool("halfimplicit.resolvecarrier", false);
  SolverSpecify::PoissonCorrectionParameter = c.get_real("halfimplicit.carrierweight", 0.0);

  // snes convergence test
  SolverSpecify::snes_rtol                  = c.get_real("snes.rtol", 1e-5);

  // ksp convergence test
  SolverSpecify::ksp_rtol                  = c.get_real("ksp.rtol", 1e-8);
  SolverSpecify::ksp_atol                  = c.get_real("ksp.atol", 1e-15);
  SolverSpecify::ksp_atol_fnorm            = c.get_real("ksp.atol.fnorm", 1e-7);
  SolverSpecify::ksp_singular              = c.get_bool("ksp.singular", false);

  //set convergence test
  SolverSpecify::MaxIteration              = c.get_int("maxiteration", 30);
  SolverSpecify::potential_update          = c.get_real("potential.update", 1.0);

  SolverSpecify::absolute_toler            = c.get_real("absolute.tol", 1e-12);
  SolverSpecify::relative_toler            = c.get_real("relative.tol", 1e-5);
  SolverSpecify::toler_relax               = c.get_real("toler.relax", 1e5, "tol.relax");
  SolverSpecify::poisson_abs_toler         = c.get_real("poisson.tol", 1e-26)*C;
  SolverSpecify::elec_continuity_abs_toler = c.get_real("elec.continuity.tol", 5e-18, "elec.c.tol")*A;
  SolverSpecify::hole_continuity_abs_toler = c.get_real("hole.continuity.tol", 5e-18, "hole.c.tol")*A;
  SolverSpecify::heat_equation_abs_toler   = c.get_real("latt.temp.tol", 1e-11)*W;
  SolverSpecify::elec_energy_abs_toler     = c.get_real("elec.energy.tol", 1e-18,"elec.e.tol")*W;
  SolverSpecify::hole_energy_abs_toler     = c.get_real("hole.energy.tol", 1e-18,"hole.e.tol")*W;
  SolverSpecify::electrode_abs_toler       = c.get_real("electrode.tol", 1e-14)*A;
  SolverSpecify::spice_abs_toler           = c.get_real("spice.tol", 1e-12)*A;

  SolverSpecify::elec_quantum_abs_toler    = c.get_real("elec.quantum.tol", 1e-26, "elec.q.tol")*C;
  SolverSpecify::hole_quantum_abs_toler    = c.get_real("hole.quantum.tol", 1e-26, "hole.q.tol")*C;

  SolverSpecify::divergence_factor         = c.get_real("divergence.factor", 1e20);

  // set which solver will be used
  if(c.is_parameter_exist("type"))
  {
    SolverSpecify::Solver = SolverSpecify::solver_type_string_to_enum(c.get_string("type", ""));
  }

  return 0;
}



/*--------------------------------------------------------
 *  set advanced semiconductor property
 */
int SolverControl::set_model ( const Parser::Card & c )
{
  std::string rgn_pattern = c.get_string("region", "");
  TRexpp rgn_rexp;
  rgn_rexp.Compile(rgn_pattern.c_str());

  AdvancedModel model;

  // advanced mobility model control
  model.ESurface                          = c.get_bool("esurface", true);
  model.HighFieldMobility                 = c.get_bool("highfieldmobility", true, "h.mob");
  model.HighFieldMobilitySelfConsistently = c.get_bool("h.mob.selfconsistent", true);
  model.QuasiFermiCarrierTruc             = c.get_real("quasifermicarriertrucation" , 1e-2);
  if(c.is_parameter_exist("mob.force") || c.is_parameter_exist("mobility.force"))
  {
    if (c.is_enum_value("mob.force", "ej") || c.is_enum_value("mobility.force", "ej"))                 model.Mob_Force = ModelSpecify::EJ;
    if (c.is_enum_value("mob.force", "esimple") || c.is_enum_value("mobility.force", "esimple"))       model.Mob_Force = ModelSpecify::ESimple;
    if (c.is_enum_value("mob.force", "eqf") || c.is_enum_value("mobility.force", "eqf"))               model.Mob_Force = ModelSpecify::EQF;
  }

  //impact ionization model
  model.ImpactIonization = false;
  if(c.is_parameter_exist("impactionization") || c.is_parameter_exist("ii"))
  {
    if(c.is_enum_value("impactionization", "local") || c.is_enum_value("ii", "local"))
    {
      model.ImpactIonization = true;
      if(c.is_parameter_exist("ii.force"))
      {
        model.II_Force = ModelSpecify::GradQf; // default
        if (c.is_enum_value("ii.force", "edotj"))  model.II_Force = ModelSpecify::IIForce_EdotJ;
        if (c.is_enum_value("ii.force", "eside"))  model.II_Force = ModelSpecify::ESide;
        if (c.is_enum_value("ii.force", "evector"))  model.II_Force = ModelSpecify::EVector;
        if (c.is_enum_value("ii.force", "gradqf"))  model.II_Force = ModelSpecify::GradQf;
      }
    }
  }

  // hot carrier injection
  model.HotCarrierInjection    = c.get_bool("hotcarrier", false);

  // FN  tunneling
  model.FNTunneling            = c.get_bool("fn.tunneling", false, "fntunneling");

  // direct tunneling
  model.DIRTunneling            = c.get_bool("dir.tunneling", false);

  // tunneling self consistently
  model.TunnelingSelfConsistently  = c.get_bool("tunneling.selfconsistent", false);

  // band to band tunneling
  model.BandBandTunneling = false;
  if(c.is_parameter_exist("bandbandtunneling") || c.is_parameter_exist("bbt"))
  {
    if(c.is_enum_value("bandbandtunneling", "local") || c.is_enum_value("bbt", "local"))
    {
      model.BandBandTunneling = true;
    }
  }

  // fermi statistics and incomplete ionization
  model.Fermi                 = c.get_bool("fermi", false);
  model.IncompleteIonization  = c.get_bool("incompleteionization", false);

  // charge trapping model
  model.Trap                  = c.get_bool("trap",false);

  //energy balance advanced model
  if(c.is_parameter_exist("eb.level"))
  {
    if (c.is_enum_value("eb.level", "none")) { model.EB_Level = ModelSpecify::NONE;   }
    if (c.is_enum_value("eb.level", "te"))   { model.EB_Level = ModelSpecify::Tn;     }
    if (c.is_enum_value("eb.level", "th"))   { model.EB_Level = ModelSpecify::Tp;     }
    if (c.is_enum_value("eb.level", "tl"))   { model.EB_Level = ModelSpecify::Tl;     }
    if (c.is_enum_value("eb.level", "teth")) { model.EB_Level = ModelSpecify::TnTp;   }
    if (c.is_enum_value("eb.level", "tetl")) { model.EB_Level = ModelSpecify::TnTl;   }
    if (c.is_enum_value("eb.level", "thtl")) { model.EB_Level = ModelSpecify::TpTl;   }
    if (c.is_enum_value("eb.level", "all"))  { model.EB_Level = ModelSpecify::ALL;    }
  }

  //density gradient model
  model.QNEnabled = c.get_bool("dg.elec", false);
  model.QPEnabled = c.get_bool("dg.hole", false);
  model.QNFactor  = c.get_real("qnfactor", 1.0);
  model.QPFactor  = c.get_real("qpfactor", 1.0);
  model.QMinConcentration  = c.get_real("qminconcentration", 1.0);

  int cnt_match = 0;
  for (int i=0; i<system().n_regions(); i++)
  {
    SimulationRegion* rgn = system().region(i);
    const std::string& rgn_name = rgn->name();

    if (!rgn_rexp.Match(rgn_name.c_str())) continue;
    cnt_match++;

    rgn->advanced_model() = model;
  }


  if( cnt_match==0 )
  {
    MESSAGE<<"ERROR at " <<c.get_fileline()<< " MODEL: Region " << rgn_pattern << " can't be found in mesh regions." << std::endl; RECORD();
    genius_error();
  }


  // we should check lattice temperature model, which should be set for all the regions!
  {
    bool temperature = false;
    for(unsigned int n=0; n < system().n_regions(); ++n)
    {
      SimulationRegion * region = system().region(n);
      if (region->get_advanced_model()->enable_Tl()) temperature = true;
    }

    if(temperature == true)
      for(unsigned int n=0; n < system().n_regions(); ++n)
      {
        SimulationRegion * region = system().region(n);
        region->advanced_model().force_temperature_usage();
      }

  }

  return 0;
}



int SolverControl::do_hook( const Parser::Card & c )
{
  if ( c.is_parameter_exist("load") )
  {
    std::string dll_name = c.get_string("load", "");
    std::string id;
    if ( c.is_parameter_exist("id") )
      id = c.get_string("id", "");
    else
      id = dll_name;

    if (SolverSpecify::Hooks.find(id) != SolverSpecify::Hooks.end())
    {
      MESSAGE<<"Warning at " <<c.get_fileline()<< " HOOK: hook " << id << " already exist, old one will be replaced." << std::endl; RECORD();
      SolverSpecify::Hooks.erase(id);
    }

    std::vector<Parser::Parameter> plist;
    for(unsigned int idx=0; idx<c.parameter_size(); idx++)
    {
      const Parser::Parameter & p = c.get_parameter(idx);
      if ( p.is_user_defined() )
        plist.push_back(p);
    }
    SolverSpecify::Hooks.insert( std::make_pair(id, std::make_pair(dll_name, plist)));
  }


  if ( c.is_parameter_exist("unload") )
  {
    std::string id;
    if ( c.is_parameter_exist("id") )
      id = c.get_string("id", "");
    else
      id = c.get_string("unload", "");

    if (SolverSpecify::Hooks.find(id) != SolverSpecify::Hooks.end())
      SolverSpecify::Hooks.erase(id);
    else
    {
      MESSAGE<<"Warning at " <<c.get_fileline()<< " HOOK: hook " << id << " can't be found for unloading." << std::endl; RECORD();
    }
  }

  return 0;
}


int SolverControl::do_solve( const Parser::Card & c )
{

  // set solution type solver will do
  SolverSpecify::Type = SolverSpecify::INVALID_SolutionType;
  if(c.is_parameter_exist("type"))
    SolverSpecify::Type = SolverSpecify::solution_type_string_to_enum(c.get_string("type", ""));

  if(c.is_parameter_exist("label"))
    SolverSpecify::label = c.get_string("label", "");

  // set more detailed solution parameters
  switch (SolverSpecify::Type)
  {
      case SolverSpecify::EQUILIBRIUM :
      {
        SolverSpecify::GminInit    = c.get_real("gmin.init", 1e-6);
        SolverSpecify::Gmin        = c.get_real("gmin", 1e-12);
        break;
      }
      case SolverSpecify::STEADYSTATE :
      case SolverSpecify::OP :
      {
        if( c.is_parameter_exist("electrode") )
        {
          std::string electrode = c.get_string("electrode", "");

          if( !system().get_bcs()->is_electrode(electrode) )
          {
            MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE OP: Electrode " << electrode << " can't be found in device structure." << std::endl; RECORD();
            genius_error();
          }

          if( c.is_parameter_exist("vconst") && c.is_parameter_exist("iconst"))
          {
            MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE OP: Vapp and Iapp can not be mix defined." << std::endl; RECORD();
            genius_error();
          }

          if( c.is_parameter_exist("vconst") )
          {
            system().get_electrical_source()->attach_voltage_to_electrode(electrode, c.get_real("vconst", 0.0)*V);
          }

          if ( c.is_parameter_exist("iconst") )
          {
            system().get_electrical_source()->attach_current_to_electrode(electrode, c.get_real("iconst", 0.0)*A);
          }
        }

        SolverSpecify::OptG        = c.get_bool("optical.gen", false);
        SolverSpecify::PatG        = c.get_bool("particle.gen", false);
        SolverSpecify::NodeSet     = c.get_bool("nodeset", true);
        SolverSpecify::RampUpSteps = c.get_int("rampup.steps", 0);

        if(c.is_parameter_exist("rampup.vstep"))
          SolverSpecify::RampUpVStep = c.get_real("rampup.vstep", 0.25)*V;
        if(c.is_parameter_exist("rampup.istep"))
          SolverSpecify::RampUpIStep = c.get_real("rampup.istep", 0.1)*A;
        if(c.is_parameter_exist("gmin.init"))
          SolverSpecify::GminInit    = c.get_real("gmin.init", 1e-12);
        if(c.is_parameter_exist("gmin"))
          SolverSpecify::Gmin        = c.get_real("gmin", 1e-12);

        SolverSpecify::OpToSteady          = c.get_bool("op.steadystate", true);
        SolverSpecify::SteadyStateThreshold= c.get_real("op.threshold", 1e-5);

        SolverSpecify::TStep               = c.get_real("tstep", 1e-10)*s;
        SolverSpecify::TStepMax            = c.get_real("tstepmax", 1e-7)*s;
        // pseudo time method
        SolverSpecify::PseudoTimeMethod            = c.get_bool("pseudotime", false);
        SolverSpecify::PseudoTimeCMOS              = c.get_bool("pseudotime.cmos", true);
        SolverSpecify::PseudoTimeCMOSLambda        = c.get_bool("pseudotime.cmos.lambda", 0.1)*um;
        SolverSpecify::PseudoTimeCMOSRes           = c.get_real("pseudotime.cmos.res", 1e3)*V/A;
        SolverSpecify::PseudoTimeCMOSCap           = c.get_real("pseudotime.cmos.cap", 1e-15)*C/V;
        SolverSpecify::PseudoTimeCMOSTime          = c.get_real("pseudotime.cmos.time", 1e-10)*s;
        SolverSpecify::PseudoTimeStepPotential     = c.get_real("pseudotime.step.potential", 1e-6)*s;
        SolverSpecify::PseudoTimeStepCarrier       = c.get_real("pseudotime.step.carrier", 1e-8)*s;
        SolverSpecify::PseudoTimeStepMetal         = c.get_real("pseudotime.step.metal", 1e-10)*s;
        SolverSpecify::PseudoTimeStepMax           = c.get_real("pseudotime.stepmax", 1e-6)*s;
        SolverSpecify::PseudoTimeSteps             = c.get_int("pseudotime.iteration", 50);
        SolverSpecify::VStepMax                    = c.get_real("vstepmax", 0.1)*V;
        SolverSpecify::IStepMax                    = c.get_real("istepmax", 1e-6)*A;
        break;
      }
      case SolverSpecify::DCSWEEP     :
      {
        // user should specify vscan OR iscan
        if(!(c.is_parameter_exist("vscan") || c.is_parameter_exist("iscan")))
        {
          MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: User should specify DC sweep electrode." << std::endl; RECORD();
          genius_error();
        }

        if( (c.is_parameter_exist("vscan") && c.is_parameter_exist("iscan")))
        {
          MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: User should only specify voltage or current DC sweep." << std::endl; RECORD();
          genius_error();
        }

        // clear electrode vector
        SolverSpecify::Electrode_VScan.clear();
        SolverSpecify::Electrode_IScan.clear();

        if(c.is_parameter_exist("vscan"))
        {
          // sweep voltage of an electrode
          if(system().get_circuit()==NULL)
          {
            unsigned int elec_num = c.parameter_count("vscan");
            for(unsigned int n=0; n<elec_num; n++)
            {
              std::string electrode = c.get_n_string("vscan", "", n, 0);
              if( !system().get_bcs()->is_electrode(electrode) )
              {
                MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: Electrode " << electrode << " can't be found in device structure." << std::endl; RECORD();
                genius_error();
              }
              SolverSpecify::Electrode_VScan.push_back(electrode);
            }

            if( !SolverSpecify::Electrode_VScan.size() )
            {
              MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: You must specify at least one electrode for voltage DC scan."<<std::endl; RECORD();
              genius_error();
            }
          }
          // vscan spice voltage source
          else
          {
            std::string spice_vsource = c.get_string_lower_case("vscan", "");
            if( !system().get_circuit()->is_ckt_voltage_source_exist_sync(spice_vsource) )
            {
              MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: VSRC " << spice_vsource << " can't be found in SPICE netlist." << std::endl; RECORD();
              genius_error();
            }
            SolverSpecify::Electrode_VScan.push_back(spice_vsource);
            if( SolverSpecify::Electrode_VScan.size() != 1 )
            {
              MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: You must specify one VSRC for voltage DC scan."<<std::endl; RECORD();
              genius_error();
            }
          }
          SolverSpecify::VStart    = c.get_real("vstart", 0.0)*V;
          SolverSpecify::VStep     = c.get_real("vstep", 0.1)*V;
          SolverSpecify::VStepMax  = c.get_real("vstepmax", SolverSpecify::VStep/V)*V;
          SolverSpecify::VStop     = c.get_real("vstop", 5.0)*V;

          if(SolverSpecify::VStep == 0.0)
          {
            MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: VStep shoud not be zero."<<std::endl; RECORD();
            genius_error();
          }
        }

        if(c.is_parameter_exist("iscan"))
        {
          if(system().get_circuit()==NULL)
          {
            unsigned int elec_num = c.parameter_count("iscan");
            for(unsigned int n=0; n<elec_num; n++)
            {
              std::string electrode = c.get_n_string("iscan", "", n, 0);
              if( !system().get_bcs()->is_electrode(electrode) )
              {
                MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: Electrode " << electrode << " can't be found in device structure." << std::endl; RECORD();
                genius_error();
              }
              SolverSpecify::Electrode_IScan.push_back(electrode);
            }
            if( !SolverSpecify::Electrode_IScan.size() )
            {
              MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: You must specify at least one electrode for current DC scan."<<std::endl; RECORD();
              genius_error();
            }
          }
          // iscan spice current source
          else
          {
            std::string spice_isource = c.get_string_lower_case("iscan", "");
            if( !system().get_circuit()->is_ckt_current_source_exist_sync(spice_isource))
            {
              MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: ISRC " << spice_isource << " can't be found in SPICE netlist." << std::endl; RECORD();
              genius_error();
            }
            SolverSpecify::Electrode_IScan.push_back(spice_isource);
            if( SolverSpecify::Electrode_IScan.size() != 1 )
            {
              MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: You must specify one ISRC for current DC scan."<<std::endl; RECORD();
              genius_error();
            }
          }
          SolverSpecify::IStart    = c.get_real("istart", 0.0)*A;
          SolverSpecify::IStep     = c.get_real("istep", 1e-5)*A;
          SolverSpecify::IStepMax  = c.get_real("istepmax", SolverSpecify::IStep/A)*A;
          SolverSpecify::IStop     = c.get_real("istop", 1e-2)*A;

          if(SolverSpecify::IStep == 0.0)
          {
            MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: IStep shoud not be zero."<<std::endl; RECORD();
            genius_error();
          }
        }

        SolverSpecify::Predict       = c.get_bool("predict", true);

        SolverSpecify::OptG          = c.get_bool("optical.gen", false);
        SolverSpecify::PatG          = c.get_bool("particle.gen", false);
        SolverSpecify::SourceCoupled = c.get_bool("source.coupled", false);
        SolverSpecify::GminInit      = c.get_real("gmin.init", 1e-6);
        SolverSpecify::Gmin          = c.get_real("gmin", 1e-12);
        // set the modulate envelop of light source
        if(c.is_parameter_exist("optical.modulate"))
        {
          std::string waveform = c.get_string("optical.modulate", "");
          FieldSource * field_source = system().get_field_source();
          if(!field_source->set_effect_waveform(waveform))
          {
            MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: optical.modulate specifies empty envelop."<<std::endl; RECORD();
            genius_error();
          }
        }
        else
        {
          FieldSource * field_source = system().get_field_source();
          field_source->clear_effect_waveform();
        }
        break;
      }
      case SolverSpecify::TRACE       :
      {
        // clear electrode vector
        SolverSpecify::Electrode_VScan.clear();
        SolverSpecify::Electrode_IScan.clear();

        // user should specify trace electrode here
        {
          unsigned int elec_num = c.parameter_count("vscan");
          for(unsigned int n=0; n<elec_num; n++)
          {
            std::string electrode = c.get_n_string("vscan", "", n, 0);
            if( !system().get_bcs()->is_electrode(electrode) )
            {
              MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: Electrode " << electrode << " can't be found in device structure." << std::endl; RECORD();
              genius_error();
            }
            std::vector<BoundaryCondition *> bcs = system().get_bcs()->get_bcs_by_electrode_label(electrode);
            if( bcs.size() != 1 )
            {
              MESSAGE<<"ERROR at " <<c.get_fileline()
                     << " SOLVE: Electrode region "<< electrode
                     << " has more than one electrical boundary, please define a SolderPad boundary and do trace on it." << std::endl; RECORD();
              genius_error();
            }
            SolverSpecify::Electrode_VScan.push_back(bcs[0]->label());
          }

          if( SolverSpecify::Electrode_VScan.size() != 1)
          {
            MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: You must specify one electrode for IV trace."<<std::endl; RECORD();
            genius_error();
          }
        }
        SolverSpecify::VStart    = c.get_real("vstart", 0.0)*V;
        SolverSpecify::VStep     = c.get_real("vstep", 0.1)*V;
        SolverSpecify::VStepMax  = c.get_real("vstepmax", SolverSpecify::VStep/V)*V;
        SolverSpecify::VStop     = c.get_real("vstop", 5.0)*V;
        SolverSpecify::IStop     = c.get_real("istop", 1.0)*A; //current limit
        SolverSpecify::IStepMax  = c.get_real("istepmax", SolverSpecify::IStop/A)*A;
        SolverSpecify::Predict   = c.get_bool("predict", true);

        SolverSpecify::OptG      = c.get_bool("optical.gen", false);
        SolverSpecify::PatG      = c.get_bool("particle.gen", false);

        // set the modulate envelop of light source
        if(c.is_parameter_exist("optical.modulate"))
        {
          std::string waveform = c.get_string("optical.modulate", "");
          FieldSource * field_source = system().get_field_source();
          if(!field_source->set_effect_waveform(waveform))
          {
            MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: optical.modulate specifies empty envelop."<<std::endl; RECORD();
            genius_error();
          }
        }
        else
        {
          FieldSource * field_source = system().get_field_source();
          field_source->clear_effect_waveform();
        }

        break;
      }

      case SolverSpecify::ACSWEEP     :
      {
        SolverSpecify::Electrode_ACScan.clear();

        SolverSpecify::FStart    = c.get_real("f.start", 1e6)/s;
        SolverSpecify::FStop     = c.get_real("f.stop", 10e9)/s;
        SolverSpecify::FMultiple = c.get_real("f.multiple", 1.1);
        SolverSpecify::VAC       = c.get_real("vac", 0.0026)*V;

        unsigned int elec_num = c.parameter_count("acscan");
        for(unsigned int n=0; n<elec_num; n++)
        {
          std::string electrode = c.get_n_string("acscan", "", n, 0);
          if( !system().get_bcs()->is_electrode(electrode) )
          {
            MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: Electrode " << electrode << " can't be found in device structure." << std::endl; RECORD();
            genius_error();
          }
          SolverSpecify::Electrode_ACScan.push_back(electrode);
        }

        if( SolverSpecify::Electrode_ACScan.size() != 1 )
        {
          MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: You must specify one electrode for AC scan."<<std::endl; RECORD();
          genius_error();
        }

        SolverSpecify::Type = SolverSpecify::ACSWEEP;

        break;
      }

      case SolverSpecify::TRANSIENT  :
      {
        SolverSpecify::TimeDependent = true;
        SolverSpecify::AutoStep  = c.get_bool("autostep", true);
        SolverSpecify::RejectStep= c.get_bool("rejectstep", true);
        SolverSpecify::Predict   = c.get_bool("predict", true);
        SolverSpecify::UIC       = c.get_bool("uic", false);
        SolverSpecify::tran_op   = c.get_bool("tran.op", true);

        SolverSpecify::TStart    = c.get_real("tstart", 0.0)*s;
        SolverSpecify::TStep     = c.get_real("tstep", 1e-9)*s;
        SolverSpecify::TStepMin  = c.get_real("tstepmin", 1e-14)*s;
        SolverSpecify::TStepMax  = c.get_real("tstepmax", 0.0)*s;
        SolverSpecify::dt        = SolverSpecify::TStep;
        SolverSpecify::TStop     = c.get_real("tstop", 1e-6)*s;

        SolverSpecify::TS_rtol   = c.get_real("ts.rtol", 1e-3);
        SolverSpecify::TS_atol   = c.get_real("ts.atol", 1e-7);

        SolverSpecify::VStepMax  = c.get_real("vstepmax", 1.0)*V;
        SolverSpecify::IStepMax  = c.get_real("istepmax", 1.0)*A;

        SolverSpecify::RampUpSteps = c.get_int("rampup.steps", 0);
        if(c.is_parameter_exist("rampup.vstep"))
          SolverSpecify::RampUpVStep = c.get_real("rampup.vstep", 0.25)*V;
        if(c.is_parameter_exist("rampup.istep"))
          SolverSpecify::RampUpIStep = c.get_real("rampup.istep", 0.1)*A;
        if(c.is_parameter_exist("gmin.init"))
          SolverSpecify::GminInit    = c.get_real("gmin.init", 1e-12);
        if(c.is_parameter_exist("gmin"))
          SolverSpecify::Gmin        = c.get_real("gmin", 1e-12);

        if(SolverSpecify::TStop <= SolverSpecify::TStart)
        {
          MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: tstop should be later than tstart."<<std::endl; RECORD();
          genius_error();
        }

        if(SolverSpecify::TStep <=0 )
        {
          MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: tstep should be positive."<<std::endl; RECORD();
          genius_error();
        }

        if(SolverSpecify::TStepMax <0 )
        {
          MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: tstepmax should not be negative."<<std::endl; RECORD();
          genius_error();
        }

        if(c.is_parameter_exist("ts"))
        {
          if (c.is_enum_value("ts", "impliciteuler"))   SolverSpecify::TS_type = SolverSpecify::BDF1;
          if (c.is_enum_value("ts", "bdf1"))            SolverSpecify::TS_type = SolverSpecify::BDF1;
          if (c.is_enum_value("ts", "bdf2"))            SolverSpecify::TS_type = SolverSpecify::BDF2;
        }

        SolverSpecify::OptG          = c.get_bool("optical.gen", false);
        SolverSpecify::PatG          = c.get_bool("particle.gen", false);
        SolverSpecify::SourceCoupled = c.get_bool("source.coupled", false);

        // set the waveform of light source
        if(c.is_parameter_exist("optical.modulate"))
        {
          std::string waveform = c.get_string("optical.modulate", "");
          FieldSource * field_source = system().get_field_source();
          if(!field_source->set_effect_waveform(waveform))
          {
            MESSAGE<<"ERROR at " <<c.get_fileline()<< " SOLVE: optical.modulate specifies empty envelop."<<std::endl; RECORD();
            genius_error();
          }
        }
        else
        {
          FieldSource * field_source = system().get_field_source();
          field_source->clear_effect_waveform();
        }

        if(c.is_parameter_exist("tran.histroy"))
          SolverSpecify::tran_histroy   = c.get_bool("tran.histroy", false);

        break;
      }

      default: break;

  }

  SolverSpecify::out_prefix = c.get_string("out.prefix", "result");
  SolverSpecify::out_append = c.get_bool("out.append", false);

  SolverBase * solver = NULL;

  // call each solver here
  switch (SolverSpecify::Solver)
  {
#ifdef TCAD_SOLVERS
      case SolverSpecify::POISSON :
      {
        solver = new PoissonSolver(system());
        break;
      }
      case SolverSpecify::DDML1 :
      {
        solver = new DDM1Solver(system());
        break;
      }
      case SolverSpecify::DDML1MIXA :
      {
        solver = new MixA1Solver(system());
        break;
      }
      case SolverSpecify::DDML1MIX :
      {
        solver = new Mix1Solver(system());
        break;
      }
      case SolverSpecify::DENSITY_GRADIENT :
      {
        solver = new DGSolver(system());
        break;
      }
      case SolverSpecify::HALLDDML1 :
      {
        solver = new HallSolver(system());
        break;
      }
      case SolverSpecify::DDML2 :
      {
        solver = new DDM2Solver(system());
        break;
      }
      case SolverSpecify::DDML2MIXA :
      {
        solver = new MixA2Solver(system());
        break;
      }
      case SolverSpecify::EBML3 :
      {
        solver = new EBM3Solver(system());
        break;
      }
      case SolverSpecify::EBML3MIXA:
      {
        solver = new MixA3Solver(system());
        break;
      }
      case SolverSpecify::DDMAC :
      {
        solver = new DDMACSolver(system());
        break;
      }
#ifdef COGENDA_COMMERCIAL_PRODUCT
      case SolverSpecify::HALF_IMPLICIT :
      {
        solver = new DDM1HalfImplicitSolver(system());
        break;
      }
#endif
#endif

#ifdef IDC_SOLVERS
#ifdef COGENDA_COMMERCIAL_PRODUCT
      case SolverSpecify::RIC :
      {
        solver = new RICSolver(system());
        break;
      }

      case SolverSpecify::DICTAT :
      {
        solver = new DICTATSolver(system());
        break;
      }
#endif
#endif
      default:
      MESSAGE<<"ERROR: Selected solver is not supported at present." << std::endl; RECORD();
      break;
  }

  if (solver)
  {
    solver->set_label(SolverSpecify::label);

    // create a solution group;
    mxml_node_t *eGroup = NULL;
    {
      mxml_node_t *eRoot = mxmlFindElement(_dom_solution, _dom_solution, "genius-solutions", NULL, NULL, MXML_DESCEND_FIRST);
      eGroup = mxmlNewElement(eRoot, "solution-group");
      mxml_node_t *eLabel = mxmlNewElement(eGroup, "label");
      mxmlAdd(eLabel, MXML_ADD_AFTER, NULL, MXMLQVariant::makeQVString(solver->label()));

      solver->set_solution_dom_root(eGroup);
    }

    // init (user defined) hook functions here

    if( SolverSpecify::Type == SolverSpecify::DCSWEEP     ||
        SolverSpecify::Type == SolverSpecify::STEADYSTATE ||
        SolverSpecify::Type == SolverSpecify::OP          ||
        SolverSpecify::Type == SolverSpecify::TRANSIENT   ||
        SolverSpecify::Type == SolverSpecify::TRACE       ||
        SolverSpecify::Solver == SolverSpecify::DDMAC
      )
    {
      // gnuplot hook, write electrode IV in gnuplot file format, as default hook
#ifdef DLLHOOK
      Hook * gnuplot_hook =  new DllHook(*solver, "gnuplot_hook", (void *)(Genius::input_file()));
      solver->add_hook(gnuplot_hook);
#else
      // for windows platform, dynamic link is not supported. we have to use static link.
      // it is not as flexible as unix/linux platform.
      Hook * gnuplot_hook =  new GnuplotHook(*solver, "gnuplot_hook", (void *)Genius::input_file());
      solver->add_hook(gnuplot_hook);
#endif

    }

#ifdef DLLHOOK
    // dynamic load user defined hooks, stupid win32 platform does not support this function.
    for (std::map<std::string, std::pair<std::string, std::vector<Parser::Parameter> > >::iterator it=SolverSpecify::Hooks.begin();
         it!=SolverSpecify::Hooks.end(); it++)
    {
      const std::vector<Parser::Parameter> & parm_list = it->second.second;
      solver->add_hook( new DllHook(*solver, (it->second.first)+"_hook", (void *)&parm_list) );
    }

#else
    // load static user defined hooks, only support predefined hooks, sigh
    for (std::map<std::string, std::pair<std::string, std::vector<Parser::Parameter> > >::iterator it=SolverSpecify::Hooks.begin();
         it!=SolverSpecify::Hooks.end(); it++)
    {
      Hook * hook=NULL;

      if((*it).second.first=="cgns")
        hook = new CGNSHook(*solver, "cgns_hook", (void *)(&(it->second.second)));
      if((*it).second.first=="vtk")
        hook = new VTKHook(*solver, "vtk_hook", (void *)(&(it->second.second)));
      if((*it).second.first=="cv")
        hook = new CVHook (*solver, "cv_hook",  (void *)(&(it->second.second)));
      if((*it).second.first=="probe")
        hook = new ProbeHook (*solver, "probe_hook",  (void *)(&(it->second.second)));
      if((*it).second.first=="threshold")
        hook = new ThresholdHook (*solver, "threshold_hook",  (void *)(&(it->second.second)));
      if((*it).second.first=="data")
        hook = new DataHook (*solver, "data_hook",  (void *)(&(it->second.second)));
      if((*it).second.first=="tunneling")
        hook = new TunnelingHook (*solver, "tunneling_hook",  (void *)(&(it->second.second)));

      if(hook) solver->add_hook(hook);
    }

#endif

    {
      // always load the control hook. We load it last, such that it is called last
      SolverControlHook * control_hook =  new SolverControlHook(*solver, "control_hook", *this, _fname_solution);
      solver->add_hook(control_hook);
    }

    solver->create_solver();
    solver->solve();
    solver->destroy_solver(); // hooks are deleted here

    {
      // if there is a solution in the group, add it to the solution document
      if (mxmlFindElement(eGroup, eGroup, "solution", NULL, NULL, MXML_DESCEND_FIRST)==NULL)
      {
        mxmlDelete(eGroup);
      }
    }

    delete solver;

  }

  return 0;
}




int  SolverControl::set_electrode_source  ( const Parser::Card & c )
{

  // A contact may have several electrode
  std::vector<std::string> electrodes;
  if(c.is_parameter_exist("contact"))
  {
    std::string contact   = c.get_string("contact", "");
    electrodes = system().get_bcs()->electrode_bc_by_contact(contact);
  }

  // electrode
  if(c.is_parameter_exist("electrode"))
    electrodes.push_back(c.get_string("electrode", ""));

  // check
  for(unsigned int n=0; n<electrodes.size(); ++n)
  {
    const std::string & electrode = electrodes[n];
    if( !system().get_bcs()->is_electrode(electrode) )
    {
      MESSAGE<<"ERROR at " <<c.get_fileline()<< " ATTACH: Electrode " << electrode << " can't be found in device structure." << std::endl; RECORD();
      genius_error();
    }
  }

  // const vsource / isource
  if( c.is_parameter_exist("vconst") )
  {
    for(unsigned int n=0; n<electrodes.size(); ++n)
    {
      const std::string & electrode = electrodes[n];
      system().get_electrical_source()->attach_voltage_to_electrode(electrode, c.get_real("vconst", 0.0)*V);
      return 0;
    }
  }

  // const vsource / isource
  if ( c.is_parameter_exist("iconst") )
  {
    for(unsigned int n=0; n<electrodes.size(); ++n)
    {
      const std::string & electrode = electrodes[n];
      system().get_electrical_source()->attach_current_to_electrode(electrode, c.get_real("iconst", 0.0)*A);
      return 0;
    }
  }

  // check if user mixed vsource and isource
  if( (c.is_parameter_exist("vapp") || c.is_parameter_exist("vapps")) &&
      (c.is_parameter_exist("iapp") || c.is_parameter_exist("iapps")) )
  {
    MESSAGE<<"ERROR at " <<c.get_fileline()<< " ATTACH: Vapp and Iapp can not be mix defined." << std::endl; RECORD();
    genius_error();
  }

  std::vector<std::string> source_list;
  if( c.is_parameter_exist("vapps") )
    source_list = c.get_array<std::string>("vapps");
  if( c.is_parameter_exist("iapps") )
    source_list = c.get_array<std::string>("iapps");
  if( c.is_parameter_exist("vapp") || c.is_parameter_exist("iapp") )
  {
    // since several vapp or iapp parameters may exist in the card, we should search for all
    for(unsigned int idx=0; idx<c.parameter_size(); idx++)
    {
      const Parser::Parameter & p = c.get_parameter(idx);
      if ( p.name() == "vapp" )
      {
        if(system().get_electrical_source()->is_vsource_exist(p.get_string()))
          source_list.push_back( p.get_string() );
        else
        {
          MESSAGE<<"ERROR at " <<c.get_fileline()<< " ATTACH: Vapp " << p.get_string() << " hasn't been defined." << std::endl; RECORD();
          genius_error();
        }
      }

      if ( p.name() == "iapp" )
      {
        if(system().get_electrical_source()->is_isource_exist(p.get_string()))
          source_list.push_back( p.get_string() );
        else
        {
          MESSAGE<<"ERROR at " <<c.get_fileline()<< " ATTACH: Iapp " << p.get_string() << " hasn't been defined." << std::endl; RECORD();
          genius_error();
        }
      }
    }
  }

  for(unsigned int n=0; n<electrodes.size(); ++n)
  {
    const std::string & electrode = electrodes[n];
    system().get_electrical_source()->attach_sources_to_electrode(electrode, source_list);
  }

  return 0;
}


int  SolverControl::set_physical_model  ( const Parser::Card & c )
{

  std::string rgn_pattern = c.get_string("region", "");
  std::string type         = c.get_string("type", "");
  std::string model        = c.get_string("model", "Default");

  TRexpp rgn_rexp;
  rgn_rexp.Compile(rgn_pattern.c_str());

  std::vector<Parser::Parameter> pmi_parameters;

  for(unsigned int idx=0; idx<c.parameter_size(); idx++)
  {
    const Parser::Parameter & p = c.get_parameter(idx);
    // find user defined parameter, which used to calibrate the PMI
    if ( p.is_user_defined() )
    {
      pmi_parameters.push_back(p);
    }
  }

  int cnt_match = 0;
  for (int i=0; i<system().n_regions(); i++)
  {
    SimulationRegion* rgn = system().region(i);
    const std::string& rgn_name = rgn->name();

    if (!rgn_rexp.Match(rgn_name.c_str())) continue;
    cnt_match++;

    if ( (type.length() > 0  ) )
    {
      rgn->set_pmi(type, model, pmi_parameters);
      system().get_bcs()->pmi_init_bc(rgn_name,type);
    }
    else
    {
      MESSAGE<<"ERROR at " <<c.get_fileline()<< " PMI: Must specify the type parameter." << std::endl; RECORD();
      genius_error();
    }

    int print_verbosity = c.get_int("print", 0);
    if ( print_verbosity > 0 )
    {
      // verbose output, let's print out the new material parameters
      MESSAGE << rgn->get_pmi_info(type, print_verbosity) << std::endl;
    }
  }

  if( cnt_match==0 )
  {
    MESSAGE<<"ERROR at " << c.get_fileline()<< " PMI: Region " << rgn_pattern << " can't be found in mesh regions." << std::endl; RECORD();
    genius_error();
  }
  else
  {
    MESSAGE<<"PMI: Region " << rgn_pattern << " model " << model << " for " << type << " updated." << std::endl; RECORD();
  }

  return 0;

}


int SolverControl::apply_field_source  ( const Parser::Card & c )
{
  _system->get_field_source()->update_source();
  return 0;
}



int  SolverControl::do_tid ( const Parser::Card & c )
{
#ifdef COGENDA_COMMERCIAL_PRODUCT
  // reset to default solver parameters
  //SolverSpecify::set_default_parameter();

  SolverSpecify::TID_TotalDose  = c.get_real("totaldose", 0.0)*rad;
  SolverSpecify::TID_DoseRate   = c.get_real("doserate", 1.0)*rad/s;
  SolverSpecify::TID_DoseStep   = c.get_real("dosestep", 500)*rad;
  SolverSpecify::TID_OPStep     = c.get_real("opstep", 3e3)*rad;
  SolverSpecify::TID_FixedCharge= c.get_bool("fixedcharge", true);

  SolverSpecify::out_prefix = c.get_string("out.prefix", "result");

  if( SolverSpecify::TID_TotalDose <= 0.0 )
  {
    MESSAGE<<"ERROR at " <<c.get_fileline()<< " TID: total dose should be positive." << std::endl; RECORD();
    genius_error();
  }

  if( SolverSpecify::TID_DoseRate <= 0.0 )
  {
    MESSAGE<<"ERROR at " <<c.get_fileline()<< " TID: dose rate should be positive." << std::endl; RECORD();
    genius_error();
  }

  if( SolverSpecify::TID_DoseStep <= 0.0 )
  {
    MESSAGE<<"ERROR at " <<c.get_fileline()<< " TID: dose step should be positive." << std::endl; RECORD();
    genius_error();
  }

  // set nonlinear solver type
  //SolverSpecify::NS = SolverSpecify::nonlinear_solver_type("basic");
  // set linear solver type
  //SolverSpecify::LS = SolverSpecify::linear_solver_type("lu");
  // set transient parameter
  SolverSpecify::TS_type             = SolverSpecify::BDF1;

  if(c.is_parameter_exist("type"))
  {
    SolverBase * solver = 0;

    if (c.is_enum_value("type", "drift"))
    {
      solver = new TIDOP(system(), "drift");
    }
    else if (c.is_enum_value("type", "full"))
    {
      solver = new TIDOP(system(), "full");
    }
    else if (c.is_enum_value("type", "trap"))
    {
      solver = new TIDTrap(system());
    }
    else
    {
      MESSAGE<<"ERROR at " <<c.get_fileline()<< " TID: TID solver type not supported." << std::endl; RECORD();
      genius_error();
    }

    solver->create_solver();
    solver->solve();
    solver->destroy_solver();

    delete solver;
  }
#endif
  return 0;
}



int SolverControl::do_export( const Parser::Card & c )
{
  // if export to VTK format is required
  if(c.is_parameter_exist("vtkfile"))
  {
    std::string vtk_filename = c.get_string("vtkfile", "");
    bool ascii = c.get_bool("ascii", false);
    system().export_vtk(vtk_filename, ascii);
  }

  // if export to VTK format is required
  if(c.is_parameter_exist("vtufile"))
  {
    std::string vtu_filename = c.get_string("vtufile", "");
    std::vector<std::string> variables = c.get_array<std::string>("vtu.variables");
    system().export_vtk2(vtu_filename, variables);
  }

  // if export to CGNS format is required
  if(c.is_parameter_exist("cgnsfile"))
  {
    std::string cgns_filename = c.get_string("cgnsfile", "");
    system().export_cgns(cgns_filename);
  }

  // if export to DF-ISE format is required
  if(c.is_parameter_exist("isefile"))
  {
    std::string ise_filename = c.get_string("isefile", "");
    system().export_ise(ise_filename);
  }


  // if export to tif format is required
  if(c.is_parameter_exist("tiffile"))
  {
    std::string tif_filename = c.get_string("tiffile", "");
    system().export_tif(tif_filename);
  }

  // if export GDML surface file is required
  if(c.is_parameter_exist("gdml") || c.is_parameter_exist("gdml.surface"))
  {
    if( mesh().mesh_dimension()==3 )
    {
      std::string gdml_filename = c.get_string("gdml", "", "gdml.surface");
      system().export_gdml_surface(gdml_filename);
    }
    else
    {
      MESSAGE<<"WARNING at " <<c.get_fileline()<< " EXPORT: Only 3D device structure have GDML support" << std::endl; RECORD();
    }
  }


  // if export boundary condition is required
  if(c.is_parameter_exist("bcinfo"))
  {
    std::string bc_filename = c.get_string("bcinfo", "");
    system().get_bcs()->export_boundary_condition(bc_filename);
  }

  // if export node location is required
  if(c.is_parameter_exist("nodeinfo"))
  {
    std::string node_filename = c.get_string("nodeinfo", "");
    bool numbering = c.get_bool("numbering", true, "");

    if (c.is_enum_value("lunit", "m"))
      system().export_node_location(node_filename, 1.0, numbering );
    else if (c.is_enum_value("lunit", "cm"))
      system().export_node_location(node_filename, 1e-2, numbering );
    else if (c.is_enum_value("lunit", "um"))
      system().export_node_location(node_filename, 1e-6, numbering );
    else if (c.is_enum_value("lunit", "nm"))
      system().export_node_location(node_filename, 1e-9, numbering );
    else
      system().export_node_location(node_filename, 1e-6, numbering );
  }

  // if export spice nodel solution to file is required
  if(c.is_parameter_exist("spice.nodeset"))
  {
    std::string spice_filename = c.get_string("spice.nodeset", "");
    system().get_circuit()->export_solution(spice_filename);
  }


  return 0;
}



int SolverControl::do_import( const Parser::Card & c )
{
  if(c.is_parameter_exist("cgnsfile"))
  {
    std::string cgns_filename = c.get_string("cgnsfile", "");
#ifdef WINDOWS
    if ( _access( (char *)cgns_filename.c_str(),  04 ) == -1 )
#else
    if (  access( (char *)cgns_filename.c_str(),  R_OK ) == -1 )
#endif
    {
      MESSAGE<<"ERROR at " <<c.get_fileline()<< " IMPORT: CGNS File " << cgns_filename << " doesn't exist." << std::endl; RECORD();
      genius_error();
    }
    system().import_cgns(cgns_filename);
  }

  if(c.is_parameter_exist("vtkfile"))
  {
    std::string vtk_filename = c.get_string("vtkfile", "");
#ifdef WINDOWS
    if ( _access( (char *)vtk_filename.c_str(),  04 ) == -1 )
#else
    if (  access( (char *)vtk_filename.c_str(),  R_OK ) == -1 )
#endif
    {
      MESSAGE<<"ERROR at " <<c.get_fileline()<< " IMPORT: VTK File " << vtk_filename << " doesn't exist." << std::endl; RECORD();
      genius_error();
    }
    system().import_vtk(vtk_filename);
  }

  if(c.is_parameter_exist("silvacofile") || c.is_parameter_exist("strfile"))
  {
    std::string silvaco_filename = c.get_string("silvacofile", "", "strfile");
#ifdef WINDOWS
    if ( _access( (char *)silvaco_filename.c_str(),  04 ) == -1 )
#else
    if (  access( (char *)silvaco_filename.c_str(),  R_OK ) == -1 )
#endif
    {
      MESSAGE<<"ERROR at " <<c.get_fileline()<< " IMPORT: Silvaco File " << silvaco_filename << " doesn't exist." << std::endl; RECORD();
      genius_error();
    }
    system().import_silvaco(silvaco_filename);
  }

  if(c.is_parameter_exist("silvacolist"))
  {
    std::vector<std::string> silvaco_filenames = c.get_array<std::string>("silvacolist");
    system().import_silvaco_list(silvaco_filenames);
  }

  if(c.is_parameter_exist("tiffile"))
  {
    std::string tif_filename = c.get_string("tiffile", "");
#ifdef WINDOWS
    if ( _access( (char *)tif_filename.c_str(),  04 ) == -1 )
#else
    if (  access( (char *)tif_filename.c_str(),  R_OK ) == -1 )
#endif
    {
      MESSAGE<<"ERROR at " <<c.get_fileline()<< " IMPORT: TIF File " << tif_filename << " doesn't exist." << std::endl; RECORD();
      genius_error();
    }
    system().import_tif(tif_filename);
  }


  if(c.is_parameter_exist("tiflist"))
  {
    std::vector<std::string> tif_filenames = c.get_array<std::string>("tiflist");
    system().import_tif_list(tif_filenames);
  }

  if(c.is_parameter_exist("tif3dfile"))
  {
    std::string tif3d_filename = c.get_string("tif3dfile", "");
#ifdef WINDOWS
    if ( _access( (char *)tif3d_filename.c_str(),  04 ) == -1 )
#else
    if (  access( (char *)tif3d_filename.c_str(),  R_OK ) == -1 )
#endif
    {
      MESSAGE<<"ERROR at " <<c.get_fileline()<< " IMPORT: TIF3D File " << tif3d_filename << " doesn't exist." << std::endl; RECORD();
      genius_error();
    }
    system().import_tif3d(tif3d_filename);
  }
  
  if(c.is_parameter_exist("supremfile"))
  {
    std::string sup_filename = c.get_string("supremfile", "");
#ifdef WINDOWS
    if ( _access( (char *)sup_filename.c_str(),  04 ) == -1 )
#else
    if (  access( (char *)sup_filename.c_str(),  R_OK ) == -1 )
#endif
    {
      MESSAGE<<"ERROR at " <<c.get_fileline()<< " IMPORT: Suprem File " << sup_filename << " doesn't exist." << std::endl; RECORD();
      genius_error();
    }
    system().import_suprem(sup_filename);
  }


  if(c.is_parameter_exist("isefile"))
  {
    std::string ise_filename = c.get_string("isefile", "");
    system().import_ise(ise_filename);
  }

  if(c.is_parameter_exist("gmshfile"))
  {
    std::string gmsh_filename = c.get_string("gmshfile", "");
    system().import_gmsh(gmsh_filename);
  }


  if(c.is_parameter_exist("unvfile"))
  {
    std::string unv_filename = c.get_string("unvfile", "");
    system().import_unv(unv_filename);
  }

  //std::cout<<"Final Mesh " << _mesh->memory_size()/(1024*1024)<<std::endl;
  //std::cout<<"Final System " << _system->memory_size()/(1024*1024)<<std::endl;
  //std::cout<<"Total " << Genius::memory_size().first/(1024*1024) << " " << Genius::memory_size().second/(1024*1024) <<std::endl;

  return 0;
}




int  SolverControl::set_initial_node_voltage  ( const Parser::Card & c )
{

  std::string electrode = c.get_string("electrode", "");

  BoundaryCondition * bc = system().get_bcs()->get_bc(electrode);
  if( bc == NULL || bc->is_electrode() == false )
  {
    MESSAGE<<"ERROR at " <<c.get_fileline()<< " NODESET: Electrode " << electrode << " can't be found in device structure." << std::endl; RECORD();
    genius_error();
  }

  bc->ext_circuit()->potential() = c.get_real("v", 0.0);

  return 0;
}




int SolverControl::do_refine_conform(const Parser::Card & c)
{
  // TODO can we refine during the solver solution processing?

  // save previous solution
  AutoPtr<InterpolationBase> interpolator;
  if( mesh().mesh_dimension() == 2 )
    interpolator = AutoPtr<InterpolationBase>(new Interpolation2D_CSA);
  else
    interpolator = AutoPtr<InterpolationBase>(new Interpolation3D_nbtet);

  if( DopingSolver.get() == NULL )
  {
    system().fill_interpolator(interpolator.get(), "doping.na", InterpolationBase::Asinh);
    system().fill_interpolator(interpolator.get(), "doping.nd", InterpolationBase::Asinh);
  }

  if(system().has_single_compound_semiconductor_region()  && MoleSolver.get() == NULL )
  {
    system().fill_interpolator(interpolator.get(), "mole.x", InterpolationBase::Linear);
  }
  if(system().has_complex_compound_semiconductor_region()  && MoleSolver.get() == NULL )
  {
    system().fill_interpolator(interpolator.get(), "mole.y", InterpolationBase::Linear);
  }

  // fill error vector from system level
  ErrorVector error_per_cell;
  system().estimate_error(c, error_per_cell);

  // gather mesh to processor 0 since we may have a distributed mesh
  mesh().gather(0);

  if (Genius::processor_id() == 0)
  {

    MeshRefinement mesh_refinement(mesh());

    // at least one refine criterion should be exist!
    genius_assert(c.is_parameter_exist("error.fraction") || c.is_parameter_exist("cell.fraction") || c.is_parameter_exist("error.threshold"));

    if(c.is_parameter_exist("error.fraction") )
      mesh_refinement.flag_elements_by_error_fraction (error_per_cell, c.get_real("error.fraction", 0.3), 0.0);

    if(c.is_parameter_exist("cell.fraction") )
      mesh_refinement.flag_elements_by_elem_fraction  (error_per_cell, c.get_real("cell.fraction",  0.3), 0.0);

    if(c.is_parameter_exist("error.threshold") )
      mesh_refinement.flag_elements_by_error_threshold(error_per_cell, c.get_real("error.threshold",0.1), 0.0);

    // if mesh generator exist, we call it to do particular refine
    if( meshgen.get() != NULL )
      meshgen->do_refine(mesh_refinement);
    // else, we have to do general mesh refinement
    else
    {
      genius_assert(mesh().magic_num() != invalid_uint);

      // for 2D, we call triangle
      if( mesh().mesh_dimension() == 2 )
      {
        MeshGeneratorBase *meshgen = new MeshGeneratorTri3(mesh(), decks());
        meshgen->do_refine(mesh_refinement);
        delete meshgen;
      }
      // for 3D, we still have no idea here.
      else
      {
        MESSAGE<<"ERROR at " <<c.get_fileline()<< " Refine: Genius still can not do 3D conform refine without mesh generator exist." << std::endl; RECORD();
        genius_error();
      }
    }

  }
#if defined(HAVE_FENV_H) && defined(DEBUG)
  genius_assert( !fetestexcept(FE_INVALID) );
#endif
  // clear the system. however we should reserve mesh information
  system().clear(false);

  // call mesh generator again to make new mesh consistence with FVM request.

  // rebuild the system
  // since we only build mesh on processor 0,
  // sync mesh to other processors.
  // this procedure also prepare the mesh for using
  MeshCommunication mesh_comm;
  mesh_comm.broadcast(mesh());

  // now we can build solution system again
  system().build_simulation_system();
  system().sync_print_info();

  // set doping profile to semiconductor region
  if( DopingSolver.get() != NULL )
    DopingSolver->solve();
  else
  {
    // no doping information?
    system().do_interpolation(interpolator.get(), "doping.na");
    system().do_interpolation(interpolator.get(), "doping.nd");
  }

  // set mole fraction to semiconductor region
  if( MoleSolver.get() != NULL )
    MoleSolver->solve();
  else
  {
    if(system().has_single_compound_semiconductor_region())
      system().do_interpolation(interpolator.get(), "mole.x");
    if(system().has_complex_compound_semiconductor_region())
      system().do_interpolation(interpolator.get(), "mole.y");
  }

  // after doping profile is set, we can init system data.
  system().init_region();
  system().init_region_post_process();
#if defined(HAVE_FENV_H) && defined(DEBUG)
  genius_assert( !fetestexcept(FE_INVALID) );
#endif

  return 0;

}



int SolverControl::do_refine_hierarchical(const Parser::Card & c)
{

  MESSAGE<<"Hierarchical mesh refinement...\n"<<std::endl; RECORD();

  // save previous solution
  AutoPtr<InterpolationBase> interpolator;
  if( mesh().mesh_dimension() == 2 )
    interpolator = AutoPtr<InterpolationBase>(new Interpolation2D_CSA);
  else
    interpolator = AutoPtr<InterpolationBase>(new Interpolation3D_nbtet);

  if( DopingSolver.get() == NULL )
  {
    system().fill_interpolator(interpolator.get(), "doping.na", InterpolationBase::Asinh);
    system().fill_interpolator(interpolator.get(), "doping.nd", InterpolationBase::Asinh);
  }

  if(system().has_single_compound_semiconductor_region()  && MoleSolver.get() == NULL )
  {
    system().fill_interpolator(interpolator.get(), "mole.x", InterpolationBase::Linear);
  }
  if(system().has_complex_compound_semiconductor_region()  && MoleSolver.get() == NULL )
  {
    system().fill_interpolator(interpolator.get(), "mole.y", InterpolationBase::Linear);
  }

  // fill error vector from system level
  ErrorVector error_per_cell;
  system().estimate_error(c, error_per_cell);

  if (Genius::processor_id() == 0)
  {

    MeshRefinement mesh_refinement(mesh());

    // at least one refine criterion should be exist!
    genius_assert(c.is_parameter_exist("error.refine.fraction") || c.is_parameter_exist("cell.refine.fraction") || c.is_parameter_exist("error.refine.threshold"));

    if(c.is_parameter_exist("error.refine.fraction") )
      mesh_refinement.flag_elements_by_error_fraction (error_per_cell, c.get_real("error.refine.fraction", 0.3), c.get_real("error.coarsen.fraction", 0.0));

    if(c.is_parameter_exist("cell.refine.fraction") )
      mesh_refinement.flag_elements_by_elem_fraction  (error_per_cell, c.get_real("cell.refine.fraction",  0.3), c.get_real("cell.coarsen.fraction",  0.0));

    if(c.is_parameter_exist("error.refine.threshold") )
      mesh_refinement.flag_elements_by_error_threshold(error_per_cell, c.get_real("error.refine.threshold",0.1), c.get_real("error.coarsen.threshold",0.0));

    // call MeshRefinement class to do FEM refine
    mesh_refinement.refine_and_coarsen_elements ();
  }

  // clear the system(). however we should reserve mesh information
  system().clear(false);

  // call mesh generator again to make new mesh consistence with FVM request.

  // rebuild the system
  // since we only build mesh on processor 0,
  // sync mesh to other processors.
  // this procedure also prepare the mesh for using
  MeshCommunication mesh_comm;
  mesh_comm.broadcast(mesh());

  // now we can build solution system again
  system().build_simulation_system();
  system().sync_print_info();

  // set doping profile to semiconductor region
  if( DopingSolver.get() != NULL )
    DopingSolver->solve();
  else
  {
    // no doping information?
    system().do_interpolation(interpolator.get(), "doping.na");
    system().do_interpolation(interpolator.get(), "doping.nd");
  }

  // set mole fraction to semiconductor region
  if( MoleSolver.get() != NULL )
    MoleSolver->solve();
  else
  {
    if(system().has_single_compound_semiconductor_region())
      system().do_interpolation(interpolator.get(), "mole.x");
    if(system().has_complex_compound_semiconductor_region())
      system().do_interpolation(interpolator.get(), "mole.y");
  }

  // after doping profile is set, we can init system data.
  system().init_region();
  system().init_region_post_process();
  return 0;

}


/*--------------------------------------------------------------------
 * uniform refine the mesh, it is intended to be used only for test!
 */
int SolverControl::do_refine_uniform(const Parser::Card & c)
{

  if (Genius::processor_id() == 0)
  {
    int step =  c.get_int("step", 1);
    MeshRefinement mesh_refinement(mesh());
    mesh_refinement.uniformly_refine(step);
    MeshTools::Modification::flatten(mesh());
  }

  // clear the system. however we should reserve mesh information
  system().clear(false);

  // since we only build mesh on processor 0,
  // sync mesh to other processors.
  // this procedure also prepare the mesh for using
  MeshCommunication mesh_comm;
  mesh_comm.broadcast(mesh());

  // now we can build solution system again
  system().build_simulation_system();
  system().sync_print_info();

  // set doping profile to semiconductor region
  if( DopingSolver.get() != NULL )
    DopingSolver->solve();

  // set mole fraction to semiconductor region
  if( MoleSolver.get() != NULL )
    MoleSolver->solve();

  // after doping profile is set, we can init system data.
  system().init_region();
  system().init_region_post_process();
  return 0;
}



int SolverControl::do_region_set ( const Parser::Card & c)
{
  std::string region_name = c.get_string("region", "");
  SimulationRegion * region = system().region(region_name);

  if( region == NULL )
  {
    MESSAGE<<"ERROR at " <<c.get_fileline()<< " REGIONSET: region "<< region_name << " does not exist." << std::endl; RECORD();
    genius_error();
  }

  std::string variable = c.get_string("variable", "");

  if( region->has_variable(variable, POINT_CENTER))
  {
    if(c.is_parameter_exist("unit"))
    {
      std::string unit_string = c.get_string("unit", "");
      ConstanteExprEvalute expr_eva(unit_string);
      double unit = expr_eva.eval();
      region->set_variable_data<PetscScalar>(variable, POINT_CENTER, c.get_real("value", 0.0), unit);
    }
    else
      region->set_variable_data<PetscScalar>(variable, POINT_CENTER, c.get_real("value", 0.0));
  }
  else
  {
    MESSAGE<<"ERROR at " <<c.get_fileline()<< " REGIONSET: region variable "<< variable << " does not exist." << std::endl; RECORD();
    genius_error();
  }


  if(c.is_parameter_exist("reinit") && c.get_bool("reinit", false))
    region->reinit_after_import();

  return 0;
}


int SolverControl::do_boundary_set ( const Parser::Card & c)
{
  std::string boundary_name = c.get_string("boundary", "");
  BoundaryCondition * bc = system().get_bcs()->get_bc(boundary_name);

  if( bc == NULL )
  {
    MESSAGE<<"ERROR at " <<c.get_fileline()<< " BOUNDARYSET: boundary "<< boundary_name << " does not exist." << std::endl; RECORD();
    genius_error();
  }

  std::string variable = c.get_string("variable", "");
  double value = c.get_real("value", 0.0);
  double unit=1.0;
  if(c.is_parameter_exist("unit"))
  {
    std::string unit_string = c.get_string("unit", "");
    ConstanteExprEvalute expr_eva(unit_string);
    unit = expr_eva.eval();
  }

  bc->scalar(variable) =  value*unit;

  return 0;
}



int SolverControl::extend_to_3d ( const Parser::Card & c )
{
  MESSAGE<<"Extend mesh to 3D...\n"<<std::endl; RECORD();
  ExtendTo3D(system(), c)();
  return 0;
}


int  SolverControl::rotate_to_3d ( const Parser::Card & c )
{
  MESSAGE<<"Rotate mesh to 3D...\n"<<std::endl; RECORD();
  RotateTo3D(system(), c)();
  return 0;
}




int SolverControl::plot_mesh(const Parser::Card & c)
{
#ifdef HAVE_X11
  bool inv = c.get_bool("y.inverse", true);
  std::string tiff = c.get_string("tiff.out", "");

  if( system().mesh().mesh_dimension() == 2)
  {
    ShowMesh2D  show_mesh(system(),inv);
    if(Genius::is_first_processor() && !show_mesh.show_mesh_init())
      show_mesh.show_mesh(tiff=="" ? NULL : tiff.c_str());
  }
#endif
  return 0;
}

// ------
SolverControlHook::SolverControlHook(SolverBase & solver, const std::string & name, SolverControl& control, const std::string& fname)
    : Hook(solver, name), _control(control), _fname(fname)
{}

SolverControlHook::~SolverControlHook()
{}

void SolverControlHook::on_init()
{}

void SolverControlHook::on_close()
{}

void SolverControlHook::pre_solve()
{}

void SolverControlHook::post_solve()
{
  if (Genius::processor_id()==0)
  {
    if (!_fname.empty())
    {
      FILE *fout = fopen(_fname.c_str(), "w");
      mxmlSaveFile(_control.get_dom_solution(), fout, MXML_NO_CALLBACK);
      fclose(fout);
    }
  }
}

void SolverControlHook::post_iteration()
{}


