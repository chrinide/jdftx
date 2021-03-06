/*-------------------------------------------------------------------
Copyright 2011 Ravishankar Sundararaman

This file is part of JDFTx.

JDFTx is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

JDFTx is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with JDFTx.  If not, see <http://www.gnu.org/licenses/>.
-------------------------------------------------------------------*/

#ifndef JDFTX_FLUID_FEX_H2O_SCALAREOS_INTERNAL_H
#define JDFTX_FLUID_FEX_H2O_SCALAREOS_INTERNAL_H

#include <core/Units.h>
#include <core/scalar.h>

//High freq cutoff on the coulomb kernel expressed as a site charge kernel
inline void setCoulombCutoffKernel(int i, double G2, double* siteChargeKernel)
{	const double Gc = 0.33;
	double G = sqrt(G2);
	siteChargeKernel[i] = 1./sqrt(1 + pow(G/Gc,4));
}

struct ScalarEOS_eval
{
	double T, alpha;
	double b; //!< exclusion volume
	double prefacHB, prefacVW1, prefacVW2; //!< prefactors to the HB and VW terms
	
	double sphereRadius, zi3; //!< Hard sphere radius and volume (= scalar weight function w3 at G=0)
	
	ScalarEOS_eval(double T) : T(T)
	{
		const double TB = 1408.4 * Kelvin; //Boyle temperature
		const double vB = 4.1782e-5 * pow(meter,3)/mol; //Boyle volume
		const double Tf = 273.16 * Kelvin; //Triple point temperature

		//Some of the random Jeff-Austin EOS parameters (see their paper for details)
		alpha = 2.145*vB;
		const double bStar = 1.0823*vB;
		const double aVW = 0.5542 * Joule*pow(meter,3)/pow(mol,2);
		const double C1 = 0.7140;
		const double lambda = 0.3241;

		const double epsHB = -11.49 * KJoule/mol;
		const double S0 = -61.468 * Joule/(mol*Kelvin), omega0 = exp(-S0);
		const double SHB = -5.128 * Joule/(mol*Kelvin), omegaHB = exp(-SHB);
		const double b1 = 0.25081;
		const double b2 = 0.99859;
		
		b = vB * (0.2*exp(-21.4*pow(T/TB+0.0445,3)) - b1*exp(1.016*T/TB) + b2);
		prefacHB = -2*T * log((omega0+omegaHB*exp(-epsHB/T))/(omega0+omegaHB)) * exp(-0.18*pow(T/Tf,8)) * (1+C1);
		prefacVW1 = -T*alpha/(lambda*b);
		prefacVW2 = bStar*T + aVW;
		
		//Hard sphere properties:
		sphereRadius = 1.36 * Angstrom;
		zi3 = (4*M_PI/3) * pow(sphereRadius,3);
	}

	//Compute the per-particle free energies at each grid point, and the gradient w.r.t the weighted density
	__hostanddev__ void operator()(int i, const double* Nbar, double* Aex, double* Aex_Nbar) const
	{
		if(Nbar[i]<0.)
		{	Aex[i] = 0.;
			Aex_Nbar[i] = 0.;
			return;
		}
		//More Jeff-Austin parameters:
		const double nHB = (0.8447e6/18.01528) * mol/pow(meter,3);
		const double dnHB = 0.1687*nHB;
		const double C1 = 0.7140;
		const double lambda = 0.3241;
		//HB part:
		double gaussHB = exp(pow((Nbar[i]-nHB)/dnHB,2));
		double fHBden = C1 + gaussHB;
		double fHBdenPrime = gaussHB * 2*(Nbar[i]-nHB)/pow(dnHB,2);
		double AHB = prefacHB / fHBden;
		double AHB_Nbar = -AHB * fHBdenPrime/fHBden;
		//VW part:
		double Ginv = 1 - lambda*b*Nbar[i]; if(Ginv<=0.0) { Aex_Nbar[i] = NAN; Aex[i]=NAN; return; }
		double AVW = prefacVW1*log(Ginv) -  Nbar[i]*prefacVW2;
		double AVW_Nbar = T*alpha/Ginv - prefacVW2;
		//FMT part:
		double n3 = zi3*Nbar[i], den = 1./(1-n3);
		double AFMT_Nbar = T*zi3 * (den*den*den)*2*(2-n3);
		double AFMT = T * (den*den)*n3*(4-3*n3); //corresponds to Carnahan-Starling EOS
		//Total
		Aex_Nbar[i] = AHB_Nbar + AVW_Nbar - AFMT_Nbar;
		Aex[i] = AHB + AVW - AFMT;
	}
	
	double vdwRadius() const
	{	return pow(3.*b/(16.*M_PI), 1./3);
	}
};


//!Tao-Mason equation of state [F. Tao and E. A. Mason, J. Chem. Phys. 100, 9075 (1994)]
struct TaoMasonEOS_eval
{
	double T, sphereRadius;
	double b, lambda, prefacQuad, prefacVap, prefacPole, zi3;
	
	//! Construct the equation of state for temperature T, given critical point, acentricity and hard sphere radius (all in atomic units)
	TaoMasonEOS_eval(double T, double Tc, double Pc, double omega, double sphereRadius) : T(T), sphereRadius(sphereRadius)
	{
		//Constants in vapor pressure correction:
		double kappa = 1.093 + 0.26*(sqrt(0.002+omega) + 4.5*(0.002+omega));
		double A1 = 0.143;
		double A2 = 1.64 + 2.65*(exp(kappa-1.093)-1);
		//Boyle temperature and volume:
		double TB = Tc * (2.6455 - 1.1941*omega);
		double vB = (Tc/Pc) * (0.1646 + 0.1014*omega);
		//Temperature dependent functions:
		const double a1 = -0.0648, a2 = 1.8067, c1 = 2.6038, c2 = 0.9726;
		double alpha = vB*( a1*exp(-c1*T/TB) + a2*(1 - exp(-c2*pow(TB/T,0.25))) );
		double B = (Tc/Pc) * (0.1445+omega*0.0637 + (Tc/T)*(-0.330 + (Tc/T)*(-0.1385+0.331*omega + (Tc/T)*(-0.0121-0.423*omega + pow(Tc/T,5)*(-0.000607-0.008*omega)))));
		b = vB*( a1*(1-c1*T/TB)*exp(-c1*T/TB) + a2*(1 - (1+0.25*c2*pow(TB/T,0.25))*exp(-c2*pow(TB/T,0.25))) );
		lambda = 0.4324 - 0.3331*omega;
		//Coalesce prefactors for each free energy term:
		prefacQuad = -T*(alpha - B);
		prefacVap = prefacQuad * (-A1 * (exp(kappa*Tc/T)-A2)) / (2*sqrt(1.8)*b);
		prefacPole = alpha*T/(lambda*b);
		zi3 = (4*M_PI/3) * pow(sphereRadius,3);
	}
	
	//Compute the per-particle free energies at each grid point, and the gradient w.r.t the weighted density
	__hostanddev__ void operator()(int i, const double* Nbar, double* Aex, double* Aex_Nbar) const
	{
		if(Nbar[i]<0.)
		{	Aex[i] = 0.;
			Aex_Nbar[i] = 0.;
			return;
		}
		//VW part:
		double Ginv = 1 - lambda*b*Nbar[i]; if(Ginv<=0.0) { Aex_Nbar[i] = NAN; Aex[i]=NAN; return; }
		double AVW = Nbar[i]*prefacQuad + prefacPole*(-log(Ginv));
		double AVW_Nbar = prefacQuad + prefacPole*(lambda*b/Ginv);
		//Vapor pressure correction:
		double b2term = sqrt(1.8)*b*b;
		double bn2term = b2term*Nbar[i]*Nbar[i];
		double Avap = prefacVap * atan(bn2term);
		double Avap_Nbar = prefacVap * b2term*Nbar[i]*2. / (1 + bn2term*bn2term);
		//FMT part:
		double n3 = zi3*Nbar[i], den = 1./(1-n3);
		double AFMT_Nbar = T*zi3 * (den*den*den)*2*(2-n3);
		double AFMT = T * (den*den)*n3*(4-3*n3); //corresponds to Carnahan-Starling EOS
		//Total
		Aex_Nbar[i] = AVW_Nbar + Avap_Nbar - AFMT_Nbar;
		Aex[i] = AVW + Avap - AFMT;
	}
	
	double vdwRadius() const
	{	return pow(3.*b/(16.*M_PI), 1./3);
	}
};

#endif // JDFTX_FLUID_FEX_H2O_SCALAREOS_INTERNAL_H
