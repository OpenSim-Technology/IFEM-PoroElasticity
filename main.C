// $Id$
//==============================================================================
//!
//! \file main.C
//!
//! \date April 16 2015
//!
//! \author Yared Bekele
//!
//! \brief Main program for the isogeometric poroelasticity solver.
//!
//==============================================================================

#include "IFEM.h"
#include "SIM2D.h"
#include "SIM3D.h"
#include "SIMPoroElasticity.h"
#include "SIMSolver.h"
#include "InitialConditionHandler.h"
#include "ASMmxBase.h"
#include "AppCommon.h"
#include "Profiler.h"


/*!
  \brief Creates the poroelastic simulator and launches the simulation.
  \param[in] infile The input file to parse
*/

template<class Dim> int runSimulator (char* infile)
{
  utl::profiler->start("Model input");
  IFEM::cout <<"\n\n0. Parsing input file(s)."
             <<"\n========================="<< std::endl;

  // Establish the poroelastic FE model
  std::vector<unsigned char> fields;
  if (ASMmxBase::Type > ASMmxBase::NONE)
    fields = { Dim::dimension, 1 };
   else
    fields = { Dim::dimension+1 };
  SIMPoroElasticity<Dim> model(fields);
  if (!model.read(infile))
    return 1;
  else
    model.opt.print(IFEM::cout) << std::endl;

  // Establish the simulation driver
  SIMSolver< SIMPoroElasticity<Dim> > solver(model);
  if (!solver.read(infile))
    return 1;

  utl::profiler->stop("Model input");
  IFEM::cout <<"\n\n10. Preprocessing the finite element model:"
             <<"\n==========================================="<< std::endl;

  // Preprocess the model and establish data structures for the algebraic system
  if (!model.preprocess())
    return 2;

  // Initialize the linear solvers
  if (!model.initSystem(model.opt.solver))
    return 2;

  // Initialize the solution fields
  model.init(TimeStep());
  SIM::setInitialConditions(model);

  // HDF5 output
  DataExporter* exporter = nullptr;
  if (model.opt.dumpHDF5(infile))
    exporter = SIM::handleDataOutput(model,solver,model.opt.hdf5,false,1,1);

  int res = solver.solveProblem(infile,exporter,"100. Starting the simulation");

  delete exporter;
  return res;
}


/*!
  \brief Main program for NURBS-based poroelasticity solver.
*/

int main (int argc, char ** argv)
{
  Profiler prof(argv[0]);
  utl::profiler->start("Initialization");

  int i;
  char* infile = 0;
  bool twoD = false;
  ASMmxBase::Type = ASMmxBase::NONE;

  IFEM::Init(argc,argv);

  for (i = 1; i < argc; i++)
    if (SIMoptions::ignoreOldOptions(argc,argv,i))
      ; // ignore the obsolete option
    else if (!strcmp(argv[i],"-2D"))
      twoD = SIMElasticity<SIM2D>::planeStrain = true;
    else if (!strcmp(argv[i],"-mixed"))
      ASMmxBase::Type = ASMmxBase::FULL_CONT_RAISE_BASIS1;
    else if (!infile)
      infile = argv[i];
    else
      std::cerr <<"*** Unknown option ignored: "<< argv[i] << std::endl;

  if (!infile)
  {
    std::cout <<"Usage: "<< argv[0]
              <<" <inputfile> [-dense|-spr|-superlu[<nt>]|-samg|-petsc]\n"
              <<"       [-lag|-spec|-LR] [-2D] [-mixed] [-nGauss <n>]\n"
              <<"       [-vtf <format> [-nviz <nviz>] [-nu <nu> [-nv <nv>]"
              <<" [-nw <nw>]] [-hdf5]\n";
    return 0;
  }

  IFEM::cout <<"\n >>> IFEM Poroelasticity Solver <<<"
             <<"\n =================================="
             <<"\n Executing command:\n";
  for (i = 0; i < argc; i++) IFEM::cout <<" "<< argv[i];
  IFEM::cout <<"\n\n Input file: "<< infile;
  IFEM::getOptions().print(IFEM::cout);
  IFEM::cout << std::endl;

  if (twoD)
    return runSimulator<SIM2D>(infile);
  else
    return runSimulator<SIM3D>(infile);
}