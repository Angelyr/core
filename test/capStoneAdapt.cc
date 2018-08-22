#include <PCU.h>
#include <apf.h>
#include <apfCAP.h>
#include <gmi.h>
#include <gmi_cap.h>
#include <apfConvert.h>
#include <apfMesh2.h>
#include <apfNumbering.h>
#include <ma.h>
#include <pcu_util.h>
#include <cstdlib>
#include <cstring>
#include <iostream>


#include "CapstoneModule.h"
/* #include "CreateMG_Framework_Core.h" */
/* #include "CreateMG_Framework_Analysis.h" */
/* #include "CreateMG_Framework_Application.h" */
/* #include "CreateMG_Framework_Attributes.h" */
/* #include "CreateMG_Framework_Core.h" */
/* #include "CreateMG_Framework_Geometry.h" */
/* #include "CreateMG_Framework_Mesh.h" */

using namespace CreateMG;
using namespace CreateMG::Attribution;
using namespace CreateMG::Mesh;
using namespace CreateMG::Geometry;

// analytic size field classes //
class UniformAniso : public ma::AnisotropicFunction
{
  public:
    UniformAniso(ma::Mesh* m)
    {
      mesh = m;
    }
    virtual void getValue(ma::Entity* v, ma::Matrix& R, ma::Vector& H)
    {
      (void)v;
      ma::Vector h;
      // principal scales
      h = ma::Vector(1,
		     0.1,
		     0.1);
      // principal directions
      ma::Matrix r(1.0, 0.0, 0.0,
		   0.0, 1.0, 0.0,
		   0.0, 0.0, 1.0);
      H = h;
      R = r;
    }
  private:
    ma::Mesh* mesh;
};

class WingShock : public ma::AnisotropicFunction
{
  public:
    WingShock(ma::Mesh* m, double inFactor)
    {
      mesh = m;
      factor = inFactor;
    }
    virtual void getValue(ma::Entity* v, ma::Matrix& R, ma::Vector& H)
    {
      ma::Vector p = ma::getPosition(mesh,v);
      double x = p[0];
      double x0 = 0.5;
      double planeSize = 0.03125;
      double spanSize  = 0.5;
      double delta = 0.5;

      double beta = 0.3;
      double x1   = 0.2;
      double f    = beta + x * (1. - beta) / x1;
      double multipier = (x <= x1) ? 1.0 : f;

      double s0 = planeSize / factor;
      double alpha = planeSize * (1. - 1./factor) / delta;
      double hx = s0 + alpha * std::abs(x - x0);
      double hy = spanSize;
      double hz = spanSize;
      hx *= multipier;
      hy *= multipier;

      ma::Vector h;

      h = ma::Vector(hx, hy, hz);
      /* // principal directions */
      ma::Matrix r(1.0, 0.0, 0.0,
		   0.0, 1.0, 0.0,
		   0.0, 0.0, 1.0);
      H = h;
      R = r;
    }
  private:
    ma::Mesh* mesh;
    double factor;
};

class Shock : public ma::AnisotropicFunction
{
  public:
    Shock(ma::Mesh* m)
    {
      mesh = m;
    }
    virtual void getValue(ma::Entity* v, ma::Matrix& R, ma::Vector& H)
    {
      ma::Vector p = ma::getPosition(mesh,v);
      double x = p[0];
      double x0 = 0.0;
      double s = 0.25 * (1 - exp(-2*std::abs(x - x0))) + 0.001;

      double hx = s;
      double hy = 0.25;
      double hz = 0.25;

      ma::Vector h;

      h = ma::Vector(hx, hy, hz);
      /* // principal directions */
      ma::Matrix r(1.0, 0.0, 0.0,
		   0.0, 1.0, 0.0,
		   0.0, 0.0, 1.0);
      H = h;
      R = r;
    }
  private:
    ma::Mesh* mesh;
};

void writeCre(CapstoneModule& cs, const std::string& vtkFileName)
{
  GeometryDatabaseInterface    *gdbi = cs.get_geometry();
  MeshDatabaseInterface        *mdbi = cs.get_mesh();
  AppContext		       *ctx = cs.get_context();

  // Get the VTK writer.
  Writer *creWriter = get_writer(ctx, "Create Native Writer");
  if (!creWriter)
	  error(HERE, ERR_GENERIC, "Could not find the CRE writer");

  IdMapping idmapping;
  std::vector<M_MModel> mmodels;
  M_GModel gmodel;
  M_MModel mmodel;
  gdbi->get_current_model(gmodel);
  mdbi->get_current_model(mmodel);
  mmodels.clear();
  mmodels.push_back(mmodel);
  creWriter->write(ctx, gmodel, mmodels, vtkFileName.c_str(), idmapping);
}

int main(int argc, char** argv)
{
  /* INIT CALLS */
  MPI_Init(&argc, &argv);
  PCU_Comm_Init();

  if (argc < 3) {
    if (PCU_Comm_Self() == 0) {
      printf("USAGE: %s <create_file.cre> <size-field>\n", argv[0]);
      printf("Size-fields:\n");
      printf("%d, for uniform anisotropic size-field\n", 1);
      printf("%d, for wing-shock size-field\n", 2);
      printf("%d, for cube-shock size-field\n", 3);
    }
    MPI_Finalize();
    exit(EXIT_FAILURE);
  }

  const char* createFileName = argv[1];
  int mode = atoi(argv[2]);

  gmi_cap_start();
  gmi_register_cap();

  /* LOAD CAPSTONE MESH */
  // create an instance of the Capstone Module activating CREATE/CREATE/CREATE
  // for the Geometry/Mesh/Attribution databases
  const std::string gdbName("Geometry Database : SMLIB");// Switch Create with SMLIB for CAD
  const std::string mdbName("Mesh Database : Create");
  const std::string adbName("Attribution Database : Create");

  CapstoneModule  cs("test", gdbName.c_str(), mdbName.c_str(), adbName.c_str());

  GeometryDatabaseInterface     *g = cs.get_geometry();
  MeshDatabaseInterface         *m = cs.get_mesh();
  AppContext                    *c = cs.get_context();

  PCU_ALWAYS_ASSERT(g);
  PCU_ALWAYS_ASSERT(m);
  PCU_ALWAYS_ASSERT(c);

  v_string filenames;
  filenames.push_back(createFileName);

  M_GModel gmodel = cs.load_files(filenames);

  M_MModel mmodel;
  // Pick the volume mesh model from associated mesh models to this geom model
  std::vector<M_MModel> mmodels;
  MG_API_CALL(m, get_associated_mesh_models(gmodel, mmodels));
  PCU_ALWAYS_ASSERT(mmodels.size() == 1);
  MG_API_CALL(m, set_current_model(mmodels[0]));

  /* SET THE ADJACENCIES */
  MG_API_CALL(m, set_adjacency_state(REGION2FACE|
				     REGION2EDGE|
				     REGION2VERTEX|
				     FACE2EDGE|
				     FACE2VERTEX));
  MG_API_CALL(m, set_reverse_states());
  MG_API_CALL(m, set_adjacency_scope(TOPO_EDGE, SCOPE_FULL));
  MG_API_CALL(m, set_adjacency_scope(TOPO_FACE, SCOPE_FULL));
  MG_API_CALL(m, compute_adjacency());

  /* CONVERT THE MESH TO APF::MESH2 */
  apf::Mesh2* apfCapMesh = apf::createMesh(m, g);

  /* ADD A TEST FIELD TO THE MESH TO DEMONSTRATE SOLUTION TRANSFER */
  apf::Field* tf  = apf::createFieldOn(apfCapMesh, "test_field", apf::VECTOR);
  apf::MeshEntity* ent;
  apf::MeshIterator* it = apfCapMesh->begin(0);
  while( (ent = apfCapMesh->iterate(it)) ) {
    apf::Vector3 p = ma::getPosition(apfCapMesh, ent);
    double x = p[0];
    double y = p[1];
    double z = p[2];
    apf::Vector3 s(y, z, 2*x);
    apf::setVector(tf, ent, 0, s);
  }
  apfCapMesh->end(it);

  /* WRITE THE BEFORE ADAPT MESH TO VTK USING APF VTK WRITER */
  apf::writeVtkFiles("before", apfCapMesh);


  /* SETUP AND CALL ADAPT */
  ma::Input* in;

  // choose a size field here
  ma::AnisotropicFunction* sf = 0;
  switch (mode) {
    case 1:
      sf = new UniformAniso(apfCapMesh);
      break;
    case 2:
      sf = new WingShock(apfCapMesh, 50);
      break;
    case 3:
      sf = new Shock(apfCapMesh);
      break;
  }
  in = ma::configure(apfCapMesh, sf);
  in->shouldSnap = true;
  in->shouldTransferParametric = true;
  in->shouldFixShape = true;
  in->shouldForceAdaptation = true;
  if (apfCapMesh->getDimension() == 2)
    in->goodQuality = 0.2;
  else // 3D meshes
    in->goodQuality = 0.027; // this is the mean-ratio cubed
  in->maximumIterations = 10;

  ma::adaptVerbose(in, false);

  /* apfCapMesh->verify(); */

  /* WRITE THE AFTER ADAPT MESH TO VTK USING APF VTK WRITER */
  apf::writeVtkFiles("after", apfCapMesh);


  /* PRINT ADAPTED MESH INFO */
  M_MModel mmdl;
  m->get_current_model(mmdl);
  std::string info;
  m->print_info(mmdl, info);

  /* WRITE THE ADAPTED MESH IN NATIVE CREATE FORMAT */
  writeCre(cs, "after.cre");

  /* CLEAN UPS */
  delete sf;

  /* EXIT CALLS */
  gmi_cap_stop();
  PCU_Comm_Free();
  MPI_Finalize();
}