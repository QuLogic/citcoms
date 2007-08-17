/*
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *<LicenseText>
 *
 * CitcomS by Louis Moresi, Shijie Zhong, Lijie Han, Eh Tan,
 * Clint Conrad, Michael Gurnis, and Eun-seo Choi.
 * Copyright (C) 1994-2005, California Institute of Technology.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *</LicenseText>
 *
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
/*   Functions which solve the heat transport equations using Petrov-Galerkin
     streamline-upwind methods. The process is basically as described in Alex
     Brooks PhD thesis (Caltech) which refers back to Hughes, Liu and Brooks.  */

#include <sys/types.h>
#include <math.h>
#include "element_definitions.h"
#include "global_defs.h"

#include "advection_diffusion.h"
#include "parsing.h"

extern int Emergency_stop;

/*struct el { double gpt[9]; }; */
void set_diffusion_timestep(struct All_variables *E);

/* ============================================
   Generic adv-diffusion for temperature field.
   ============================================ */


/***************************************************************/
void PG_timestep_init(struct All_variables *E)
{

  set_diffusion_timestep(E);

  return;
}


void set_diffusion_timestep(struct All_variables *E)
{
  float diff_timestep, ts;
  int m, el, d;

  float global_fmin();

  diff_timestep = 1.0e8;
  for(m=1;m<=E->sphere.caps_per_proc;m++)
    for(el=1;el<=E->lmesh.nel;el++)  {
      for(d=1;d<=E->mesh.nsd;d++)    {
	ts = E->eco[m][el].size[d] * E->eco[m][el].size[d];
	diff_timestep = min(diff_timestep,ts);
      }
    }

  diff_timestep = global_fmin(E,diff_timestep);
/*   diff_timestep = ((3==dims)? 0.125:0.25) * diff_timestep; */
  E->advection.diff_timestep = 0.5 * diff_timestep;

  return;
}


void PG_timestep_solve(struct All_variables *E)
{

  double Tmaxd();
  double CPU_time0();
  void filter();
  void predictor();
  void corrector();
  void pg_solver();
  void temperatures_conform_bcs();
  double Tmaxd();
  int i,m,psc_pass,iredo;
  double time0,time1,T_interior1;
  double *DTdot[NCS], *T1[NCS], *Tdot1[NCS];

  E->advection.timesteps++;

  for(m=1;m<=E->sphere.caps_per_proc;m++)  {
    DTdot[m]= (double *)malloc((E->lmesh.nno+1)*sizeof(double));
    T1[m]= (double *)malloc((E->lmesh.nno+1)*sizeof(double));
    Tdot1[m]= (double *)malloc((E->lmesh.nno+1)*sizeof(double));
  }

  for(m=1;m<=E->sphere.caps_per_proc;m++)
    for (i=1;i<=E->lmesh.nno;i++)   {
      T1[m][i] = E->T[m][i];
      Tdot1[m][i] = E->Tdot[m][i];
    }

  /* get the max temperature for old T */
  T_interior1 = Tmaxd(E,E->T);

  E->advection.dt_reduced = 1.0;
  E->advection.last_sub_iterations = 1;

/*   time1= CPU_time0(); */

  do {
    E->advection.timestep *= E->advection.dt_reduced;

    iredo = 0;
    if (E->advection.ADVECTION) {

      predictor(E,E->T,E->Tdot);

      for(psc_pass=0;psc_pass<E->advection.temp_iterations;psc_pass++)   {
        /* XXX: replace inputdiff with refstate.conductivity */
	pg_solver(E,E->T,E->Tdot,DTdot,E->convection.heat_sources,E->control.inputdiff,1,E->node);
	corrector(E,E->T,E->Tdot,DTdot);
	temperatures_conform_bcs(E);
      }
    }

    /* get the max temperature for new T */
    E->monitor.T_interior = Tmaxd(E,E->T);

    if (E->monitor.T_interior/T_interior1 > E->monitor.T_maxvaried) {
      for(m=1;m<=E->sphere.caps_per_proc;m++)
	for (i=1;i<=E->lmesh.nno;i++)   {
	  E->T[m][i] = T1[m][i];
	  E->Tdot[m][i] = Tdot1[m][i];
	}
      iredo = 1;
      E->advection.dt_reduced *= 0.5;
      E->advection.last_sub_iterations ++;
    }

  }  while ( iredo==1 && E->advection.last_sub_iterations <= 5);

  if(E->control.filter_temperature)
    filter(E);

  /*   time0= CPU_time0()-time1; */
  /*     if(E->control.verbose) */
  /*       fprintf(E->fp_out,"time=%f\n",time0); */

  E->advection.total_timesteps++;
  E->monitor.elapsed_time += E->advection.timestep;

  if (E->advection.last_sub_iterations==5)
    E->control.keep_going = 0;

  for(m=1;m<=E->sphere.caps_per_proc;m++) {
    free((void *) DTdot[m] );
    free((void *) T1[m] );
    free((void *) Tdot1[m] );
  }

  if(E->control.lith_age) {
      if(E->parallel.me==0) fprintf(stderr,"PG_timestep_solve\n");
      lith_age_conform_tbc(E);
      assimilate_lith_conform_bcs(E);
  }


  return;
}

/***************************************************************/

void advection_diffusion_parameters(E)
     struct All_variables *E;

{

    /* Set intial values, defaults & read parameters*/
    int m=E->parallel.me;

    input_boolean("ADV",&(E->advection.ADVECTION),"on",m);

    input_int("minstep",&(E->advection.min_timesteps),"1",m);
    input_int("maxstep",&(E->advection.max_timesteps),"1000",m);
    input_int("maxtotstep",&(E->advection.max_total_timesteps),"1000000",m);
    input_float("finetunedt",&(E->advection.fine_tune_dt),"0.9",m);
    input_float("fixed_timestep",&(E->advection.fixed_timestep),"0.0",m);
    input_float("adv_gamma",&(E->advection.gamma),"0.5",m);
    input_int("adv_sub_iterations",&(E->advection.temp_iterations),"2,1,nomax",m);

    input_float("inputdiffusivity",&(E->control.inputdiff),"1.0",m);

/*     input_float("sub_tolerance",&(E->advection.vel_substep_aggression),"0.005",m);   */
/*     input_int("maxsub",&(E->advection.max_substeps),"25",m); */

/*     input_float("liddefvel",&(E->advection.lid_defining_velocity),"0.01",m); */
/*     input_float("sublayerfrac",&(E->advection.sub_layer_sample_level),"0.5",m);             */


  return;
}

void advection_diffusion_allocate_memory(E)
     struct All_variables *E;

{ int i,m;

  for(m=1;m<=E->sphere.caps_per_proc;m++)  {
    E->Tdot[m]= (double *)malloc((E->lmesh.nno+1)*sizeof(double));

    for(i=1;i<=E->lmesh.nno;i++)
      E->Tdot[m][i]=0.0;
    }

return;
}

void PG_timestep(struct All_variables *E)
{
  std_timestep(E);

  PG_timestep_solve(E);

  return;
}


/* ==============================
   predictor and corrector steps.
   ============================== */

void predictor(E,field,fielddot)
     struct All_variables *E;
     double **field,**fielddot;

{
  int node,m;
  double multiplier;

  multiplier = (1.0-E->advection.gamma) * E->advection.timestep;

  for (m=1;m<=E->sphere.caps_per_proc;m++)
    for(node=1;node<=E->lmesh.nno;node++)  {
      field[m][node] += multiplier * fielddot[m][node] ;
      fielddot[m][node] = 0.0;
    }

  return;
}

void corrector(E,field,fielddot,Dfielddot)
     struct All_variables *E;
     double **field,**fielddot,**Dfielddot;

{
  int node,m;
  double multiplier;

  multiplier = E->advection.gamma * E->advection.timestep;

  for (m=1;m<=E->sphere.caps_per_proc;m++)
    for(node=1;node<=E->lmesh.nno;node++) {
      field[m][node] += multiplier * Dfielddot[m][node];
      fielddot[m][node] +=  Dfielddot[m][node];
    }

  return;
 }

/* ===================================================
   The solution step -- determine residual vector from
   advective-diffusive terms and solve for delta Tdot
   Two versions are available -- one for Cray-style
   vector optimizations etc and one optimized for
   workstations.
   =================================================== */


void pg_solver(E,T,Tdot,DTdot,Q0,diff,bc,FLAGS)
     struct All_variables *E;
     double **T,**Tdot,**DTdot;
     struct SOURCES Q0;
     double diff;
     int bc;
     unsigned int **FLAGS;
{
    void process_heating();
    void get_global_shape_fn();
    void pg_shape_fn();
    void element_residual();
    void velo_from_element();

    int el,e,a,i,a1,m;
    double Eres[9],rtf[4][9];  /* correction to the (scalar) Tdot field */
    float VV[4][9];

    struct Shape_function PG;
    struct Shape_function GN;
    struct Shape_function_dA dOmega;
    struct Shape_function_dx GNx;

    const int dims=E->mesh.nsd;
    const int dofs=E->mesh.dof;
    const int ends=enodes[dims];
    const int sphere_key = 1;

    /* adiabatic and dissipative heating*/
    process_heating(E);

    for (m=1;m<=E->sphere.caps_per_proc;m++)
      for(i=1;i<=E->lmesh.nno;i++)
 	 DTdot[m][i] = 0.0;

    for (m=1;m<=E->sphere.caps_per_proc;m++)
       for(el=1;el<=E->lmesh.nel;el++)    {

          velo_from_element(E,VV,m,el,sphere_key);

          get_global_shape_fn(E, el, &GN, &GNx, &dOmega, 0,
                              sphere_key, rtf, E->mesh.levmax, m);

          /* XXX: replace diff with refstate.conductivity */
          pg_shape_fn(E, el, &PG, &GNx, VV,
                      rtf, diff, m);
          element_residual(E, el, PG, GNx, dOmega, VV, T, Tdot,
                           Q0, Eres, rtf, diff, E->sphere.cap[m].TB,
                           FLAGS, m);

        for(a=1;a<=ends;a++) {
	    a1 = E->ien[m][el].node[a];
	    DTdot[m][a1] += Eres[a];
           }

        } /* next element */

    (E->exchange_node_d)(E,DTdot,E->mesh.levmax);

    for (m=1;m<=E->sphere.caps_per_proc;m++)
      for(i=1;i<=E->lmesh.nno;i++) {
        if(!(E->node[m][i] & (TBX | TBY | TBZ)))
	  DTdot[m][i] *= E->TMass[m][i];         /* lumped mass matrix */
	else
	  DTdot[m][i] = 0.0;         /* lumped mass matrix */
      }

    return;
}



/* ===================================================
   Petrov-Galerkin shape functions for a given element
   =================================================== */

void pg_shape_fn(E,el,PG,GNx,VV,rtf,diffusion,m)
     struct All_variables *E;
     int el,m;
     struct Shape_function *PG;
     struct Shape_function_dx *GNx;
     float VV[4][9];
     double rtf[4][9];
     double diffusion;

{
    int i,j;
    int *ienm;

    double uc1,uc2,uc3;
    double u1,u2,u3,sint[9];
    double uxse,ueta,ufai,xse,eta,fai,adiff;

    double prod1,unorm,twodiff;

    ienm=E->ien[m][el].node;

    twodiff = 2.0*diffusion;

    uc1 =  uc2 = uc3 = 0.0;

    for(i=1;i<=ENODES3D;i++) {
      uc1 +=  E->N.ppt[GNPINDEX(i,1)]*VV[1][i];
      uc2 +=  E->N.ppt[GNPINDEX(i,1)]*VV[2][i];
      uc3 +=  E->N.ppt[GNPINDEX(i,1)]*VV[3][i];
      }

    uxse = fabs(uc1*E->eco[m][el].size[1]);
    ueta = fabs(uc2*E->eco[m][el].size[2]);
    ufai = fabs(uc3*E->eco[m][el].size[3]);

    xse = (uxse>twodiff)? (1.0-twodiff/uxse):0.0;
    eta = (ueta>twodiff)? (1.0-twodiff/ueta):0.0;
    fai = (ufai>twodiff)? (1.0-twodiff/ufai):0.0;


    unorm = uc1*uc1 + uc2*uc2 + uc3*uc3;

    adiff = (unorm>0.000001)?( (uxse*xse+ueta*eta+ufai*fai)/(2.0*unorm) ):0.0;

    for(i=1;i<=VPOINTS3D;i++)
       sint[i] = rtf[3][i]/sin(rtf[1][i]);

    for(i=1;i<=VPOINTS3D;i++) {
       u1 = u2 = u3 = 0.0;
       for(j=1;j<=ENODES3D;j++)  /* this line heavily used */ {
		u1 += VV[1][j] * E->N.vpt[GNVINDEX(j,i)];
		u2 += VV[2][j] * E->N.vpt[GNVINDEX(j,i)];
	   	u3 += VV[3][j] * E->N.vpt[GNVINDEX(j,i)];
	    }

       for(j=1;j<=ENODES3D;j++) {
            prod1 = (u1 * GNx->vpt[GNVXINDEX(0,j,i)]*rtf[3][i] +
                     u2 * GNx->vpt[GNVXINDEX(1,j,i)]*sint[i] +
                     u3 * GNx->vpt[GNVXINDEX(2,j,i)] ) ;

	    PG->vpt[GNVINDEX(j,i)] = E->N.vpt[GNVINDEX(j,i)] + adiff * prod1;
	    }
       }

   return;
 }



/* ==========================================
   Residual force vector from heat-transport.
   Used to correct the Tdot term.
   =========================================  */

void element_residual(E,el,PG,GNx,dOmega,VV,field,fielddot,Q0,Eres,rtf,diff,BC,FLAGS,m)
     struct All_variables *E;
     int el,m;
     struct Shape_function PG;
     struct Shape_function_dA dOmega;
     struct Shape_function_dx GNx;
     float VV[4][9];
     double **field,**fielddot;
     struct SOURCES Q0;
     double Eres[9];
     double rtf[4][9];
     double diff;
     float **BC;
     unsigned int **FLAGS;

{
    int i,j,a,k,node,nodes[5],d,aid,back_front,onedfns;
    double Q;
    double dT[9];
    double tx1[9],tx2[9],tx3[9],sint[9];
    double v1[9],v2[9],v3[9];
    double adv_dT,t2[4];
    double T,DT;

    register double prod,sfn;
    struct Shape_function1 GM;
    struct Shape_function1_dA dGamma;
    double temp;

    void get_global_1d_shape_fn();

    const int dims=E->mesh.nsd;
    const int dofs=E->mesh.dof;
    const int nno=E->lmesh.nno;
    const int lev=E->mesh.levmax;
    const int ends=enodes[dims];
    const int vpts=vpoints[dims];
    const int diffusion = (diff != 0.0);

    for(i=1;i<=vpts;i++)	{
      dT[i]=0.0;
      v1[i] = tx1[i]=  0.0;
      v2[i] = tx2[i]=  0.0;
      v3[i] = tx3[i]=  0.0;
      }

    for(i=1;i<=vpts;i++)
        sint[i] = rtf[3][i]/sin(rtf[1][i]);

    for(j=1;j<=ends;j++)       {
      node = E->ien[m][el].node[j];
      T = field[m][node];
      if(E->node[m][node] & (TBX | TBY | TBZ))
	    DT=0.0;
      else
	    DT = fielddot[m][node];

      for(i=1;i<=vpts;i++)  {
		  dT[i] += DT * E->N.vpt[GNVINDEX(j,i)];
		  tx1[i] += GNx.vpt[GNVXINDEX(0,j,i)] * T * rtf[3][i];
		  tx2[i] += GNx.vpt[GNVXINDEX(1,j,i)] * T * sint[i];
	 	  tx3[i] += GNx.vpt[GNVXINDEX(2,j,i)] * T;
		  sfn = E->N.vpt[GNVINDEX(j,i)];
		  v1[i] += VV[1][j] * sfn;
		  v2[i] += VV[2][j] * sfn;
		  v3[i] += VV[3][j] * sfn;
	      }
      }

/*    Q=0.0;
    for(i=0;i<Q0.number;i++)
	  Q += Q0.Q[i] * exp(-Q0.lambda[i] * (E->monitor.elapsed_time+Q0.t_offset));
*/

    /* heat production */
    Q = E->control.Q0;

    /* should we add a compositional contribution? */
    if(E->control.tracer_enriched){
      /* XXX: change Q and Q0 to be a vector of ncomp elements */
      for(j=0;j<E->composition.ncomp;j++) {

        /* Q = Q0 for C = 0, Q = Q0ER for C = 1, and linearly in
           between  */
        Q *= (1.0 - E->composition.comp_el[m][j][el]);
        Q += E->composition.comp_el[m][j][el] * E->control.Q0ER;
      }
    }


    /* construct residual from this information */


    if(diffusion){
      for(j=1;j<=ends;j++) {
	Eres[j]=0.0;
	for(i=1;i<=vpts;i++)
	  Eres[j] -=
	    PG.vpt[GNVINDEX(j,i)] * dOmega.vpt[i]
	    * (dT[i] - Q + v1[i]*tx1[i] + v2[i]*tx2[i] + v3[i]*tx3[i])
 	    + diff*dOmega.vpt[i] * (GNx.vpt[GNVXINDEX(0,j,i)]*tx1[i]*rtf[3][i] +
				    GNx.vpt[GNVXINDEX(1,j,i)]*tx2[i]*sint[i] +
				    GNx.vpt[GNVXINDEX(2,j,i)]*tx3[i] );
      }
    }

    else { /* no diffusion term */
      for(j=1;j<=ends;j++) {
	Eres[j]=0.0;
	for(i=1;i<=vpts;i++)
	  Eres[j] -= PG.vpt[GNVINDEX(j,i)] * dOmega.vpt[i] * (dT[i] - Q + v1[i] * tx1[i] + v2[i] * tx2[i] + v3[i] * tx3[i]);
      }
    }

    /* See brooks etc: the diffusive term is excused upwinding for
       rectangular elements  */

    /* include BC's for fluxes at (nominally horizontal) edges (X-Y plane) */

    if(FLAGS!=NULL) {
      onedfns=0;
      for(a=1;a<=ends;a++)
	if (FLAGS[m][E->ien[m][el].node[a]] & FBZ) {
	  if (!onedfns++) get_global_1d_shape_fn(E,el,&GM,&dGamma,1,m);

	  nodes[1] = loc[loc[a].node_nebrs[0][0]].node_nebrs[2][0];
	  nodes[2] = loc[loc[a].node_nebrs[0][1]].node_nebrs[2][0];
	  nodes[4] = loc[loc[a].node_nebrs[0][0]].node_nebrs[2][1];
	  nodes[3] = loc[loc[a].node_nebrs[0][1]].node_nebrs[2][1];

	  for(aid=0,j=1;j<=onedvpoints[E->mesh.nsd];j++)
	    if (a==nodes[j])
	      aid = j;
	  if(aid==0)
	    printf("%d: mixed up in pg-flux int: looking for %d\n",el,a);

	  if (loc[a].plus[1] != 0)
	    back_front = 0;
	  else back_front = 1;

	  for(j=1;j<=onedvpoints[dims];j++)
	    for(k=1;k<=onedvpoints[dims];k++)
	      Eres[a] += dGamma.vpt[GMVGAMMA(back_front,j)] *
		E->M.vpt[GMVINDEX(aid,j)] * g_1d[j].weight[dims-1] *
		BC[2][E->ien[m][el].node[a]] * E->M.vpt[GMVINDEX(k,j)];
	}
    }

    return;
}




/* =====================================================
   Obtain largest possible timestep (no melt considered)
   =====================================================  */


void std_timestep(E)
     struct All_variables *E;
{
    int i,d,n,nel,el,node,m;

    float global_fmin();
    void velo_from_element();

    float adv_timestep;
    float ts,uc1,uc2,uc3,uc,size,step,VV[4][9];

    const int dims=E->mesh.nsd;
    const int dofs=E->mesh.dof;
    const int nno=E->lmesh.nno;
    const int lev=E->mesh.levmax;
    const int ends=enodes[dims];
    const int sphere_key = 1;

    nel=E->lmesh.nel;

    if(E->advection.fixed_timestep != 0.0) {
      E->advection.timestep = E->advection.fixed_timestep;
      return;
    }

    adv_timestep = 1.0e8;
    for(m=1;m<=E->sphere.caps_per_proc;m++)
      for(el=1;el<=nel;el++) {

	velo_from_element(E,VV,m,el,sphere_key);

	uc=uc1=uc2=uc3=0.0;
	for(i=1;i<=ENODES3D;i++) {
	  uc1 += E->N.ppt[GNPINDEX(i,1)]*VV[1][i];
	  uc2 += E->N.ppt[GNPINDEX(i,1)]*VV[2][i];
	  uc3 += E->N.ppt[GNPINDEX(i,1)]*VV[3][i];
        }
	uc = fabs(uc1)/E->eco[m][el].size[1] + fabs(uc2)/E->eco[m][el].size[2] + fabs(uc3)/E->eco[m][el].size[3];

	step = (0.5/uc);
	adv_timestep = min(adv_timestep,step);
      }

    adv_timestep = E->advection.dt_reduced * adv_timestep;

    adv_timestep = 1.0e-32 + min(E->advection.fine_tune_dt*adv_timestep,
				 E->advection.diff_timestep);

    E->advection.timestep = global_fmin(E,adv_timestep);

/*     if (E->parallel.me==0) */
/*       fprintf(stderr, "adv_timestep=%g diff_timestep=%g\n",adv_timestep,E->advection.diff_timestep); */

    return;
  }


void filter(struct All_variables *E)
{
	double Tsum0,Tmin,Tmax,Tsum1,TDIST,TDIST1;
	int m,i,TNUM,TNUM1;
	double Tmax1,Tmin1;
	int lev;

	Tsum0= Tsum1= 0.0;
	Tmin= Tmax= 0.0;
	Tmin1= Tmax1= 0.0;
	TNUM= TNUM1= 0;
	TDIST= TDIST1= 0.0;

	lev=E->mesh.levmax;

	for(m=1;m<=E->sphere.caps_per_proc;m++)
		for(i=1;i<=E->lmesh.nno;i++)  {

			/* compute sum(T) before filtering, skipping nodes
			   that's shared by another processor */
		  	if(!(E->NODE[lev][m][i] & SKIP))
				Tsum0 +=E->T[m][i];

			  /* remove overshoot. This is crude!!!  */
			  if(E->T[m][i]<Tmin)  Tmin=E->T[m][i];
			  if(E->T[m][i]<0.0) E->T[m][i]=0.0;
			  if(E->T[m][i]>Tmax) Tmax=E->T[m][i];
			  if(E->T[m][i]>1.0) E->T[m][i]=1.0;

		}

	/* find global max/min of temperature */
	MPI_Allreduce(&Tmin,&Tmin1,1,MPI_DOUBLE,MPI_MIN,E->parallel.world);
	MPI_Allreduce(&Tmax,&Tmax1,1,MPI_DOUBLE,MPI_MAX,E->parallel.world);

	for(m=1;m<=E->sphere.caps_per_proc;m++)
		for(i=1;i<=E->lmesh.nno;i++)  {
			/* remvoe undershoot. This is crude!!! */
			if(E->T[m][i]<=abs(Tmin1))   E->T[m][i]=0.0;
			if(E->T[m][i]>=(2-Tmax1))   E->T[m][i]=1.0;

			/* sum(T) after filtering */
			if (!(E->NODE[lev][m][i] & SKIP))  {
				Tsum1+=E->T[m][i];
				if(E->T[m][i]!=0.0 && E->T[m][i]!=1.0)  TNUM++;
			}

		}

	/* find the difference of sum(T) before/after the filtering */
	TDIST=Tsum0-Tsum1;
	MPI_Allreduce(&TDIST,&TDIST1,1,MPI_DOUBLE,MPI_SUM,E->parallel.world);
	MPI_Allreduce(&TNUM,&TNUM1,1,MPI_INT,MPI_SUM,E->parallel.world);
	TDIST=TDIST1/TNUM1;

	/* keep sum(T) the same before/after the filtering by distributing
	   the difference back to nodes */
	for(m=1;m<=E->sphere.caps_per_proc;m++)
		for(i=1;i<=E->lmesh.nno;i++)   {
			if(E->T[m][i]!=0.0 && E->T[m][i]!=1.0)
				E->T[m][i] +=TDIST;
		}

	return;
}


static void process_visc_heating(struct All_variables *E, int m,
                                 double *heating, double *total_heating)
{
    int e, i;
    double visc, temp;
    float *strain_sqr;
    const int vpts = vpoints[E->mesh.nsd];

    strain_sqr = (float*) malloc((E->lmesh.nel+1)*sizeof(float));
    temp = E->control.disptn_number / E->control.Atemp / vpts;

    strain_rate_2_inv(E, m, strain_sqr, 0);

    for(e=1; e<=E->lmesh.nel; e++) {
        visc = 0.0;
        for(i = 1; i <= vpts; i++)
            visc += E->EVi[m][(e-1)*vpts + i];

        heating[e] = temp * visc * strain_sqr[e];
    }

    free(strain_sqr);


    /* sum up */
    *total_heating = 0;
    for(e=1; e<=E->lmesh.nel; e++)
        *total_heating += heating[e] * E->eco[m][e].area;

    return;
}


static void process_adi_heating(struct All_variables *E, int m,
                                double *heating, double *total_heating)
{
    int e, ez, i, j;
    double temp, temp2;
    const int ends = enodes[E->mesh.nsd];

    temp2 = E->control.disptn_number / ends;
    for(e=1; e<=E->lmesh.nel; e++) {
        ez = (e - 1) % E->lmesh.elz + 1;

        temp = 0.0;
        for(i=1; i<=ends; i++) {
            j = E->ien[m][e].node[i];
            temp += E->sphere.cap[m].V[3][j]
                * (E->T[m][j] + E->data.surf_temp);
        }

        /* XXX: missing gravity */
        heating[e] = temp * temp2 * 0.25
            * (E->refstate.expansivity[ez] + E->refstate.expansivity[ez + 1])
            * (E->refstate.rho[ez] + E->refstate.rho[ez + 1]);
    }

    /* sum up */
    *total_heating = 0;
    for(e=1; e<=E->lmesh.nel; e++)
        *total_heating += heating[e] * E->eco[m][e].area;

    return;
}


static void latent_heating(struct All_variables *E, int m,
                           double *heating_latent, double *heating_adi,
                           float **B, float Ra, float clapeyron,
                           float depth, float transT, float width)
{
    double temp1, temp2, temp3;
    int e, i, j;
    const int ends = enodes[E->mesh.nsd];

    temp1 = 2.0 * width * clapeyron * Ra / E->control.Atemp;

    for(e=1; e<=E->lmesh.nel; e++) {
        temp2 = 0;
        temp3 = 0;
        for(i=1; i<=ends; i++) {
            j = E->ien[m][e].node[i];
            temp2 += temp1 * (1.0 - B[m][j]) * B[m][j]
                * E->sphere.cap[m].V[3][j] * (E->T[m][j] + E->data.surf_temp)
                * E->control.disptn_number;
            temp3 += temp1 * clapeyron * (1.0 - B[m][j])
                * B[m][j] * (E->T[m][j] + E->data.surf_temp)
                * E->control.disptn_number;
        }
        temp2 = temp2 / ends;
        temp3 = temp3 / ends;
        heating_adi[e] += temp2;
        heating_latent[e] += temp3;
    }
    return;
}


static void process_latent_heating(struct All_variables *E, int m,
                                   double *heating_latent, double *heating_adi)
{
    int e;

    if(E->control.Ra_410 != 0 || E->control.Ra_670 != 0.0 ||
       E->control.Ra_cmb != 0) {
        for(e=1; e<=E->lmesh.nel; e++)
            heating_latent[e] = 1.0;
    }

    if(E->control.Ra_410 != 0.0) {
        latent_heating(E, m, heating_latent, heating_adi,
                       E->Fas410, E->control.Ra_410,
                       E->control.clapeyron410, E->viscosity.z410,
                       E->control.transT410, E->control.width410);

    }

    if(E->control.Ra_670 != 0.0) {
        latent_heating(E, m, heating_latent, heating_adi,
                       E->Fas670, E->control.Ra_670,
                       E->control.clapeyron670, E->viscosity.zlm,
                       E->control.transT670, E->control.width670);
    }

    if(E->control.Ra_cmb != 0.0) {
        latent_heating(E, m, heating_latent, heating_adi,
                       E->Fascmb, E->control.Ra_cmb,
                       E->control.clapeyroncmb, E->viscosity.zcmb,
                       E->control.transTcmb, E->control.widthcmb);
    }


    if(E->control.Ra_410 != 0 || E->control.Ra_670 != 0.0 ||
       E->control.Ra_cmb != 0) {
        for(e=1; e<=E->lmesh.nel; e++)
            heating_latent[e] = 1.0 / heating_latent[e];
    }

    return;
}



void process_heating(struct All_variables *E)
{
    int m;
    double temp1, temp2;
    double total_visc_heating[NCS], total_adi_heating[NCS];
    FILE *fp;
    char filename[250];

    const int dims = E->mesh.nsd;
    const int ends = enodes[dims];
    const int vpts = vpoints[dims];


    for(m=1; m<=E->sphere.caps_per_proc; m++) {
        process_visc_heating(E, m, E->heating_visc[m], &(total_visc_heating[m]));
        process_adi_heating(E, m, E->heating_adi[m], &(total_adi_heating[m]));
        process_latent_heating(E, m, E->heating_latent[m], E->heating_adi[m]);
    }

    /* compute total amount of visc/adi heating over all processors */
    MPI_Allreduce(&(total_visc_heating[1]), &temp1, E->sphere.caps_per_proc,
                  MPI_DOUBLE, MPI_SUM, E->parallel.world);
    MPI_Allreduce(&(total_adi_heating[1]), &temp2, E->sphere.caps_per_proc,
                  MPI_DOUBLE, MPI_SUM, E->parallel.world);

    if(E->parallel.me == 0)
        fprintf(E->fp, "Total_heating(adi, visc): %g %g\n", temp2, temp1);

    return;
}
