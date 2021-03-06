// $Id$
//==============================================================================
//!
//! \file PoroElasticity.C
//!
//! \date April 16 2015
//!
//! \author Yared Bekele
//!
//! \brief Integrand implementations for time-dependent poroelasticity problems.
//!
//==============================================================================

#include "PoroElasticity.h"
#include "PoroMaterial.h"
#include "FiniteElement.h"
#include "ElmNorm.h"
#include "TimeDomain.h"
#include "Utilities.h"
#include "AnaSol.h"
#include "Tensor.h"
#include "IFEM.h"
#include "tinyxml.h"


PoroElasticity::PoroElasticity (unsigned short int n) : Elasticity(n)
{
  sc = 0.0;
  gravity[n-1] = 9.81; // Default gravity acceleration
  calculateEnergy = useDynCoupling = false;
}


bool PoroElasticity::parse (const TiXmlElement* elem)
{
  if (!strncasecmp(elem->Value(),"calcEn",6))
    calculateEnergy = true;
  else if (!strncasecmp(elem->Value(),"useDynC",7))
    useDynCoupling = true;
  else if (strcasecmp(elem->Value(),"scaling"))
    return this->Elasticity::parse(elem);
  else if (utl::getAttribute(elem,"value",sc))
    IFEM::cout <<"\tScaling: sc = "<< sc << std::endl;

  return true;
}


Material* PoroElasticity::parseMatProp (const TiXmlElement* elem, bool)
{
  IFEM::cout <<" Poroelastic material, see \"Problem definition:\" below."
             << std::endl;
  material = new PoroMaterial();
  material->parse(elem);
  return material;
}


void PoroElasticity::printLog () const
{
  IFEM::cout <<"PoroElasticity: scaling = "<< sc <<" useDynCoupling = "
             << std::boolalpha << useDynCoupling << std::endl;
  this->Elasticity::printLog();
}


void PoroElasticity::setMode (SIM::SolutionMode mode)
{
  this->Elasticity::setMode(mode);
  eS = Fu + 1; // Elasticity::evalBou stores traction force vectors here
  iS = mode == SIM::DYNAMIC ? 1 : 0; // Flag calculation of internal forces
}


double PoroElasticity::getScaling (const Vec3& X, double dt) const
{
  if (sc != 0.0 || dt == 0.0)
    return sc;

  const PoroMaterial* pmat = dynamic_cast<const PoroMaterial*>(material);
  if (!pmat) return sc;

  double rhog = pmat->getFluidDensity(X) * gravity.length();
  Vec3 permeability = pmat->getPermeability(X);
  return sqrt(pmat->getStiffness(X) * rhog / permeability.x / dt);
}


LocalIntegral* PoroElasticity::getLocalIntegral (const std::vector<size_t>& nen,
                                                 size_t, bool neumann) const
{
  if (m_mode == SIM::DYNAMIC)
    return new NewmarkMats<MixedElmMats>(nsd * nen[0], nen[1], neumann,
                                         intPrm[2], intPrm[3],
                                         intPrm[0], intPrm[1], useDynCoupling);
  else
    return new MixedElmMats(nsd * nen[0], nen[1], neumann);
}


LocalIntegral* PoroElasticity::getLocalIntegral (size_t nen,
                                                 size_t, bool neumann) const
{
  if (m_mode == SIM::DYNAMIC)
    return new NewmarkMats<NonMixedElmMats>(nsd * nen, nen, neumann,
                                            intPrm[2], intPrm[3]);
  else
    return new NonMixedElmMats(nsd * nen, nen, neumann);
}


bool PoroElasticity::initElement (const std::vector<int>& MNPC,
                                  const std::vector<size_t>& elem_sizes,
                                  const std::vector<size_t>& basis_sizes,
                                  LocalIntegral& elmInt)
{
  if (primsol.empty() || primsol.front().empty())
    return true;

  // Split the nodal correspondance array into one for each solution field
  std::vector<int>::const_iterator pStart = MNPC.begin() + elem_sizes.front();
  std::vector<int> MNPCu(MNPC.begin(),pStart);
  std::vector<int> MNPCp(pStart,MNPC.end());

  size_t nsol = m_mode == SIM::DYNAMIC ? NSOL : 2;
  if (elmInt.vec.size() < nsol)
    elmInt.vec.resize(nsol);

  // Extract the element level solution vectors
  int ierr = utl::gather(MNPCu, nsd, primsol.front(), elmInt.vec[Vu]);
  ierr += utl::gather(MNPCp, 0, 1, primsol.front(), elmInt.vec[Vp],
                      nsd*basis_sizes.front(), basis_sizes.front());

  if (m_mode == SIM::DYNAMIC) {
    // Extract the element level velocity vectors
    ierr += utl::gather(MNPCu, nsd, primsol[1], elmInt.vec[Vuvel]);
    ierr += utl::gather(MNPCp, 0, 1, primsol[1], elmInt.vec[Vpvel],
                        nsd*basis_sizes.front(), basis_sizes.front());
    // Extract the element level acceleration vector for displacement
    ierr += utl::gather(MNPCu, nsd, primsol[2], elmInt.vec[Vuacc]);
  }

  if (ierr == 0)
    return true;

  std::cerr <<" *** PoroElasticity::initElement: Detected "<< ierr
            <<" node numbers out of range."<< std::endl;

  return false;
}


bool PoroElasticity::initElement (const std::vector<int>& MNPC,
                                  LocalIntegral& elmInt)
{
  if (primsol.empty() || primsol.front().empty())
    return true;

  size_t nsol = m_mode == SIM::DYNAMIC ? NSOL : 2;
  if (elmInt.vec.size() < nsol)
    elmInt.vec.resize(nsol);

  Matrix temp(nsd+1, MNPC.size());

  // Extract the element level solution vectors
  int ierr = utl::gather(MNPC, nsd+1, primsol.front(), temp);
  elmInt.vec[Vp] = temp.getRow(nsd+1);
  elmInt.vec[Vu] = temp.expandRows(-1);

  size_t ip = primsol.size();
  if (m_mode == SIM::DYNAMIC && ip > 2) {
    // Extract the element level acceleration vector
    temp.resize(nsd+1, MNPC.size());
    ierr += utl::gather(MNPC, nsd+1, primsol[--ip], temp);
    elmInt.vec[Vuacc] = temp.expandRows(-1);
    // Extract the element level velocity vectors
    temp.resize(nsd+1, MNPC.size());
    ierr += utl::gather(MNPC, nsd+1, primsol[--ip], temp);
    elmInt.vec[Vpvel] = temp.getRow(nsd+1);
    elmInt.vec[Vuvel] = temp.expandRows(-1);
  }

  if (ierr == 0)
    return true;

  std::cerr <<" *** PoroElasticity::initElement: Detected "<< ierr
            <<" node numbers out of range."<< std::endl;

  return false;
}


bool PoroElasticity::evalIntMx (LocalIntegral& elmInt,
                                const MxFiniteElement& fe,
                                const TimeDomain& time, const Vec3& X) const
{
  return this->evalInt(elmInt, fe, time, X);
}


bool PoroElasticity::evalElasticityMatrices (ElmMats& elMat, const Matrix& B,
                                             const FiniteElement& fe,
                                             const Vec3& X) const
{
  SymmTensor eps(nsd), sigma(nsd);
  if (!B.multiply(elMat.vec[Vu],eps))
    return false;

  Matrix C; double U = 0.0;
  if (!material->evaluate(C,sigma,U,fe,X,eps,eps,iS))
    return false;

  // Integrate the (material) stiffness
  Matrix CB;
  CB.multiply(C,B).multiply(fe.detJxW); // CB = dSdE*B*|J|*w
  elMat.A[uu_K].multiply(B,CB,true,false,true); // K_uu += B^T * CB

  // Integrate the internal forces, if deformed configuration
  if (iS == 0 || eps.isZero(1.0e-16))
    return true;

  sigma *= fe.detJxW;
  return B.multiply(sigma,elMat.b[Fu],true,-1); // F_u -= B^T*sigma
}


bool PoroElasticity::evalCouplingMatrix (Matrix& mx, const Matrix& B,
                                         const Vector& N, double scl) const
{
  Matrix K(N.size(), nsd * (nsd + 1) / 2);
  for (size_t i = 1; i <= N.size(); i++)
    for (size_t j = 1; j <= nsd; j++)
      K(i,j) = scl * N(i);

  mx.multiply(B, K, true, true, true);

  return true;
}


bool PoroElasticity::evalCompressibilityMatrix (Matrix& mx, const Vector& N,
                                                double scl) const
{
  Matrix temp(mx.rows(), mx.cols());
  if (!temp.outer_product(N,N))
    return false;

  mx.add(temp,scl);

  return true;
}


bool PoroElasticity::evalPermeabilityMatrix (Matrix& mx, const Matrix& dNdX,
                                             const Vec3& permeability,
                                             double scl) const
{
  for (size_t i = 1; i <= dNdX.rows(); i++)
    for (size_t j = 1; j <= dNdX.rows(); j++)
      for (size_t k = 1; k <= nsd; k++)
        mx(i,j) += scl * permeability[k-1] * dNdX(i,k) * dNdX(j,k);

  return true;
}

bool PoroElasticity::evalDynamicCouplingMatrix(Matrix& mx, const Vector& Nu,
                                               const Matrix& dNpdx,
                                               const Vec3& permeability,
                                               double scl) const
{
  for (size_t i = 1; i <= dNpdx.rows(); i++)
    for (size_t j = 1; j <= Nu.size(); j++)
      for (size_t k = 1; k <= nsd; k++)
        mx(nsd*(j-1) + k, i) += dNpdx(i,k) * permeability[k-1] * Nu(j) * scl;

  return true;
}

bool PoroElasticity::evalInt (LocalIntegral& elmInt,
                              const FiniteElement& fe,
                              const TimeDomain& time, const Vec3& X) const
{
  const PoroMaterial* pmat = dynamic_cast<const PoroMaterial*>(material);
  if (!pmat) {
    std::cerr << __FUNCTION__ << ": No material data." << std::endl;
    return false;
  }

  Matrix Bmat;
  if (!this->formBmatrix(Bmat,fe.dNdX))
    return false;

  // Evaluate the permeability
  Vec3 permeability = pmat->getPermeability(X);

  // Evaluate other material parameters
  double rhog  = pmat->getFluidDensity(X) * gravity.length();
  double poro  = pmat->getPorosity(X);
  double alpha = pmat->getBiotCoeff(X);
  double Minv  = pmat->getBiotModulus(X,alpha,poro);
  double scl   = this->getScaling(X,time.dt);

  // Integrate the element matrices, depending on solution mode
  ElmMats& elMat = static_cast<ElmMats&>(elmInt);

  if (!elMat.A[uu_M].empty())
    this->formMassMatrix(elMat.A[uu_M], fe.basis(1), X, fe.detJxW);

  if (!elMat.A[up_D].empty())
    this->evalDynamicCouplingMatrix(elMat.A[up_D], fe.basis(1), fe.grad(2),
                                    permeability, scl / rhog * fe.detJxW);

  if (!this->evalElasticityMatrices(elMat, Bmat, fe, X))
    return false;

  if (!this->evalCouplingMatrix(elMat.A[up_Q], Bmat, fe.basis(2),
                                scl*alpha*fe.detJxW))
    return false;

  if (!this->evalCompressibilityMatrix(elMat.A[pp_S], fe.basis(2),
                                       scl*scl*Minv*fe.detJxW))
    return false;

  return this->evalPermeabilityMatrix(elMat.A[pp_P], fe.grad(2),
                                      permeability, scl*scl/rhog*fe.detJxW);
}


bool PoroElasticity::evalBouMx (LocalIntegral& elmInt,
                                const MxFiniteElement& fe,
                                const TimeDomain&, const Vec3& X,
                                const Vec3& normal) const
{
  // Using the inherited standard method from Elasticity
  return this->evalBou(elmInt, fe, X, normal);
}


bool PoroElasticity::finalizeElement (LocalIntegral& elmInt,
                                      const TimeDomain& time, size_t)
{
  static_cast<Mats&>(elmInt).setStepSize(time.dt);
  return true;
}


bool PoroElasticity::finalizeElementBou (LocalIntegral& elmInt,
                                         const FiniteElement&,
                                         const TimeDomain& time)
{
  static_cast<Mats&>(elmInt).setStepSize(time.dt);
  return true;
}


size_t PoroElasticity::getNoFields (int fld) const
{
  if (fld < 2)
    return nsd+1;
  return nsd * (nsd + 1);
}


std::string PoroElasticity::getField1Name (size_t i, const char* prefix) const
{
  if (i == 11)
    return "Displacements";
  if (i == 12)
    return "Pressure";

  if (i >= nsd)
    i = 3;

  static const char* s[5] = {"u_x", "u_y", "u_z", "p^w"};

  if (!prefix)
    return s[i];
  return prefix + std::string(" ") + s[i];
}


std::string PoroElasticity::getField2Name (size_t i, const char* prefix) const
{
  static const char* s[][6] = {{"x", "y", "xy"},
                               {"x", "y", "z", "yz", "xz", "xy"}};
  size_t ncomps = nsd * (nsd + 1) / 2;

  std::string name = (i < ncomps ? "eps" : "sig") + std::string("_") + s[nsd-2][i % ncomps];
  if (!prefix)
    return name;

  return prefix + std::string(" ") + name;
}


bool PoroElasticity::evalSol (Vector& s, const MxFiniteElement& fe,
                              const Vec3& X, const std::vector<int>& MNPC,
                              const std::vector<size_t>& elem_sizes,
                              const std::vector<size_t>&) const
{
  Vector eV;
  std::vector<int>::const_iterator fstart = MNPC.begin() + elem_sizes.front();
  utl::gather(std::vector<int>(MNPC.begin(),fstart),nsd,primsol.front(),eV);

  return this->evalSolCommon(s,fe,X,Vector(eV.ptr(),nsd*fe.N.size()));
}


bool PoroElasticity::evalSol (Vector& s, const FiniteElement& fe,
                              const Vec3& X, const std::vector<int>& MNPC) const
{
  Vector eV;
  utl::gather(MNPC,nsd+1,primsol.front(),eV);

  Vector disp(nsd * fe.N.size());
  for (size_t i = 0; i < nsd; i++)
    for (size_t bfun = 0; bfun < fe.N.size(); bfun++)
      disp[nsd*bfun+i] = eV[(nsd+1)*bfun+i];

  return this->evalSolCommon(s,fe,X,disp);
}


bool PoroElasticity::evalSolCommon (Vector& s,
                                    const FiniteElement& fe, const Vec3& X,
                                    const Vector& disp) const
{
  if (!material)
  {
    std::cerr << __FUNCTION__ <<": No material data."<< std::endl;
    return false;
  }

  Matrix Bmat;
  if (!this->formBmatrix(Bmat,fe.dNdX))
    return false;

  SymmTensor eps(nsd), sigma(nsd);
  if (!Bmat.multiply(disp,eps))
    return false;

  Matrix Cmat; double U = 0.0;
  if (!material->evaluate(Cmat,sigma,U,fe,X,eps,eps))
    return false;

  s = eps;
  const RealArray& sig = sigma;
  s.insert(s.end(),sig.begin(),sig.end());

  return true;
}


NormBase* PoroElasticity::getNormIntegrand (AnaSol* sol) const
{
  if (!calculateEnergy)
    return nullptr;

  if (sol)
    return new PoroNorm(*const_cast<PoroElasticity*>(this),
                        sol->getVectorSol(), sol->getVectorSecSol(),
                        sol->getScalarSol(), sol->getScalarSecSol());

  return new PoroNorm(*const_cast<PoroElasticity*>(this));
}


PoroNorm::PoroNorm(PoroElasticity& poroel,
                   VecFunc* disp, TensorFunc* d_disp,
                   RealFunc* press, VecFunc* d_press)
  : NormBase(poroel)
  , displacement(disp)
  , d_displacement(d_disp)
  , pressure(press)
  , d_pressure(d_press)
{
  nrcmp = 2;
}


size_t PoroNorm::getNoFields(int group) const
{
  // FIXME: Projected solutions
  if (group == 0)
    return 1;

  size_t ret = 2;
  if (displacement) ret++;
  if (pressure) ret++;
  return ret;
}


std::string PoroNorm::getName(size_t i, size_t j, const char* prefix) const
{
  if (i == 0 || j == 0 || j > 4)
    return this->NormBase::getName(i, j, prefix);

  static const char* s[6] = {
    // Group 1: regular norms
    "|u^h|_l2",
    "|p^h|_l2",

    // Group 1: error norms
    "|u-u^h|_l2",
    "|p-p^h|_l2",
  };

  if (!displacement && j > 2)
    j++;

  if (!prefix)
    return s[j-1];

  static std::string name;
  name = prefix + std::string(" ") + s[j-1];
  return name.c_str();
}


bool PoroNorm::evalIntMx(LocalIntegral& elmInt, const MxFiniteElement& fe,
                         const TimeDomain& time, const Vec3& X) const
{
  return this->evalInt(elmInt, fe, time, X);
}


bool PoroNorm::evalInt(LocalIntegral& elmInt, const FiniteElement& fe,
                       const TimeDomain& time, const Vec3& X) const
{
  ElmNorm& norms = static_cast<ElmNorm&>(elmInt);
  size_t nsd = fe.grad(1).cols();
  size_t nbfu = fe.basis(1).size();

  Vector& eU = norms.vec[PoroElasticity::Vu];
  Vector& eP = norms.vec[PoroElasticity::Vp];

  // Numerical displacement
  Vector disp_h(nsd);
  for (size_t i = 1; i <= nbfu; i++)
    for (size_t j = 1; j <= nsd; j++)
      disp_h(j) += eU(nsd*(i-1) + j) * fe.basis(1)(i);

  // Numerical pressure
  double press_h = eP.dot(fe.basis(2));

  // Norm index counter
  size_t np = 0;

  // Displacement L2-norm
  norms[np++] += disp_h.dot(disp_h) * fe.detJxW;

  // Pressure L2-norm
  norms[np++] += press_h * press_h * fe.detJxW;

  // Displacement error L2-norm
  if (displacement) {
    Vec4 Xt(X, time.t);
    Vec3 disp_a = (*displacement)(Xt);
    for (size_t i = 1; i <= nsd; i++)
      norms[np] += (disp_a(i) - disp_h(i)) * (disp_a(i) - disp_h(i)) * fe.detJxW;
    np++;
  }

  // Pressure error L2-norm
  if (pressure) {
    Vec4 Xt(X, time.t);
    double press_a = (*pressure)(Xt);
    norms[np++] += (press_a - press_h) * (press_a - press_h) * fe.detJxW;
  }

  return true;
}
