/*
 *  liquid2D.cpp
 */

#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "liquid2D.h"
#include "utility.h"
#include "interp.h"
#include "levelset2D.h"
#include "solver.h"
#include "write_bmp.h"

#define	GRAVITY		9.8
#define	DT			0.015
#define DIST		(0.1)
#define REDIST		1

namespace {
	static int gn;
	static char subcell = 1;
	static char show_velocity = 0;
	static char show_dist = 0;
	static char show_grid = 0;
	static char show_region = 1;
	static char interpMethd = 1;
	static char do_redistance = 1;
	static char do_volumeCorrection = 1;
	static char solver_mode = 2;
	static FLOAT maxdist = DIST;
	static FLOAT volume0 = 0.0;
	static FLOAT y_volume0 = 0.0;
	static FLOAT volume_error = 0.0;
	static FLOAT vdiv = 0.0;
	
	static FLOAT ***u = NULL;		// Access Bracket u[DIM][X][Y] ( Staggered Grid )
	static FLOAT **p = NULL;		// Equivalent to p[N][N]
	static FLOAT **d = NULL;		// Equivalent to d[N][N]
	static FLOAT **A = NULL;		// Level Set Field
	
	static int reset_count = 0;
	static int reset_num = 3;
	static char pressed = 0;
	static FLOAT mousep[2];
}

using namespace std;

#if defined(__APPLE__) || defined(MACOSX)
#include <GLUT/glut.h>
#include <OpenGL/gl.h>
#include <sys/time.h>
#elif defined(WIN32)
#include "glut.h"
#include <windows.h>
#else
#include <GL/gl.h>
#include <GL/glut.h>
#include <sys/time.h>
#endif

static bool sphere( FLOAT x, FLOAT y ) {
	switch (reset_count) {
		case 0:
			return x < 0.4 && y < 0.6;
		case 1:
			return hypot(x-0.5,y-0.8) < 0.15 || y < 0.2;
		case 2:
			return y < 0.1;
	}
	return 0.0;
}

void liquid2D::init( int n ) {
	gn = n;
	if( ! p ) p = alloc2D<FLOAT>(gn);
	if( ! d ) d = alloc2D<FLOAT>(gn);
	if( ! A ) A = alloc2D<FLOAT>(gn);
	if( ! u ) {
		u = new FLOAT **[3];
		u[0] = alloc2D<FLOAT>(gn+1);
		u[1] = alloc2D<FLOAT>(gn+1);
	}
	// Clear Variables
	FOR_EVERY_X_FLOW(gn) {
		u[0][i][j] = 0.0;
	} END_FOR;
	FOR_EVERY_Y_FLOW(gn) {
		u[1][i][j] = 0.0;
	} END_FOR;
	
	// Initialize LevelSet
	levelset2D::init(gn);
	
	// Build Simple LevelSet
	if( do_redistance ) maxdist = DIST;
	else maxdist = 1.0;
		
	levelset2D::buildLevelset(sphere,maxdist);
	levelset2D::setVisibility( show_grid, show_dist, show_region );
	interp::setInterpMethod(interpMethd);
	
	// Compute Initial Volume
	volume0 = levelset2D::getVolume();
	y_volume0 = 0.0;
	
	// Remove Images
	// system( "rm -rf *.bmp" );
}

void drawBitmapString( const char *string) {
	while (*string) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *string++);
}

static bool write_frame() {
	
	int width = glutGet(GLUT_WINDOW_WIDTH);
	int height = glutGet(GLUT_WINDOW_HEIGHT);
	unsigned char *buffer = new unsigned char[width*height*4];
	
	glFlush();
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, buffer );
	
	char name[256];
	static int counter = 0;
	sprintf( name, "frame_%d.bmp", counter++ );
	write_bmp( name, buffer, width, height*0.75, true );
	
	delete [] buffer;
	return true;
}

static void render() {
	// Display LevelSet
	levelset2D::display(true);
	
	if( show_velocity ) {
		FLOAT s = 2.0;
		glColor4d(1.0,1.0,0.0,0.8);
		FOR_EVERY_CELL(gn) {
			if( A[i][j] < 0.0 ) {
				FLOAT h = 1.0/gn;
				FLOAT p[2] = {i*h+h/2.0,j*h+h/2.0};
				FLOAT v[2] = {0.5*u[0][i][j]+0.5*u[0][i+1][j],0.5*u[1][i][j]+0.5*u[1][i][j+1]};
				glBegin(GL_LINES);
				glVertex2d(p[0],p[1]);
				glVertex2d(p[0]+s*DT*v[0],p[1]+s*DT*v[1]);
				glEnd();
			}
		} END_FOR;
	}
	
	int winsize[2] = { glutGet(GLUT_WINDOW_WIDTH), glutGet(GLUT_WINDOW_HEIGHT) };
	double dw = 1.0/(double)winsize[0];
	double dh = 1.0/(double)winsize[1];
	int peny = 20;

	// Display Usage
	glColor4d( 1.0, 1.0, 1.0, 1.0 );
	for( int j=0; j<10; j++ ) {
		glRasterPos2d(20*dw, 1.0-(j+1)*peny*dh);
		switch(j) {
			case 0:
				drawBitmapString("Push \"p\" to change the accuracy of boundary projection");
				if( subcell ) drawBitmapString( " ( current: 2nd order.)");
				else drawBitmapString( " ( current: 1st order.)");
				break;
			case 1:
				drawBitmapString("Push \"d\" to toggle distance field.");
				break;
			case 2:
				drawBitmapString("Push \"f\" to toggle liquid field.");
				break;
			case 3:
				drawBitmapString("Push \"g\" to toggle grid points.");
				break;
			case 4:
				drawBitmapString("Push \"v\" to toggle velocity.");
				break;
			case 5:
				drawBitmapString("Push \"r\" to reset.");
				break;
			case 6:
				drawBitmapString("Push \"i\" to toggle interpolation method.");
				if( interpMethd ) drawBitmapString( " ( current: Catmull-Rom Spline.)");
				else drawBitmapString( " ( current: Linear.)");
				break;
			case 7:
				drawBitmapString("Push \"a\" to toggle redistance.");
				break;
			case 8:
				drawBitmapString("Push \"c\" to toggle volume correction.");
				if( do_volumeCorrection ) drawBitmapString( " ( current: Enabled.)");
				else drawBitmapString( " ( current: Disabled.)");
				break;
			case 9:
				//solver_mode
				drawBitmapString("Push \"s\" to toggle pressure solver.");
				if( solver_mode == 0 ) drawBitmapString( " ( current: CG Method.)");
				else if( solver_mode == 1 ) drawBitmapString( " ( current: ICCG.)");
				else if( solver_mode == 2 ) drawBitmapString( " ( current: MICCG.)");
				break;
		}
	}
	
	if( pressed ) {
		glPointSize(10);
		glColor4f(0.0,0.0,0.0,1.0);
		glBegin(GL_POINTS);
		glVertex2f( mousep[0], mousep[1] );
		glEnd();
		glPointSize(5);
		glColor4f(0.75,1.0,0.5,1.0);
		glBegin(GL_POINTS);
		glVertex2f( mousep[0], mousep[1] );
		glEnd();
		glPointSize(1);
	}
	
	// write_frame();
}

static void markLiquid() {
	levelset2D::getLevelSet(A);
}

static void addGravity() {
	OPENMP_FOR FOR_EVERY_Y_FLOW(gn) {
		if( j>0 && j<gn-1 && (A[i][j]<0.0 || A[i][j-1]<0.0)) u[1][i][j] += -DT*GRAVITY;
		else u[1][i][j] = 0.0;
	} END_FOR;
	
	OPENMP_FOR FOR_EVERY_X_FLOW(gn) {
		if( i>0 && i<gn-1 && (A[i][j]<0.0 || A[i-1][j]<0.0)) u[0][i][j] += 0.0;
		else u[0][i][j] = 0.0;
	} END_FOR;
}

static void computeVolumeError() {
	FLOAT curVolume = levelset2D::getVolume();
	if( ! volume0 || ! do_volumeCorrection || ! curVolume ) {
		vdiv = 0.0;
		return;
	}
	volume_error = volume0-curVolume;
	
	FLOAT x = (curVolume - volume0)/volume0;
	y_volume0 += x*DT;
	
	FLOAT kp = 2.3 / (25.0 * DT);
	FLOAT ki = kp*kp/16.0;
	vdiv = -(kp * x + ki * y_volume0) / (x + 1.0);
}

static void comp_divergence() {
	FLOAT h = 1.0/gn;
	FOR_EVERY_CELL(gn) {
		FLOAT div = A[i][j]<0.0 ? (u[0][i+1][j]-u[0][i][j]) + (u[1][i][j+1]-u[1][i][j]) : 0.0;
		d[i][j] = div/h - vdiv;
	} END_FOR;
}

static void compute_pressure() {
	// Clear Pressure
	FOR_EVERY_CELL(gn) {
		p[i][j] = 0.0;
	} END_FOR;
	
	// Solve Ap = d ( p = Pressure, d = Divergence )
	solver::solve( A, p, d, gn, subcell, solver_mode );
}

static void subtract_pressure() {
	FLOAT h = 1.0/gn;
	OPENMP_FOR FOR_EVERY_X_FLOW(gn) {
		if( i>0 && i<gn ) {
			FLOAT pf = p[i][j];
			FLOAT pb = p[i-1][j];
			if( subcell && A[i][j] * A[i-1][j] < 0.0 ) {
				pf = A[i][j] < 0.0 ? p[i][j] : A[i][j]/fmin(1.0e-3,A[i-1][j])*p[i-1][j];
				pb = A[i-1][j] < 0.0 ? p[i-1][j] : A[i-1][j]/fmin(1.0e-6,A[i][j])*p[i][j];
			}
			u[0][i][j] -= (pf-pb)/h;
		}
	} END_FOR;
	
	OPENMP_FOR FOR_EVERY_Y_FLOW(gn) {
		if( j>0 && j<gn ) {
			FLOAT pf = p[i][j];
			FLOAT pb = p[i][j-1];
			if( subcell && A[i][j] * A[i][j-1] < 0.0 ) {
				pf = A[i][j] < 0.0 ? p[i][j] : A[i][j]/fmin(1.0e-3,A[i][j-1])*p[i][j-1];
				pb = A[i][j-1] < 0.0 ? p[i][j-1] : A[i][j-1]/fmin(1.0e-6,A[i][j])*p[i][j];
			}
			u[1][i][j] -= (pf-pb)/h;
		}
	} END_FOR;
}

static void enforce_boundary() {
	OPENMP_FOR FOR_EVERY_X_FLOW(gn) {
		if( i==0 || i==gn ) u[0][i][j] = 0.0;
	} END_FOR;
	
	OPENMP_FOR FOR_EVERY_Y_FLOW(gn) {
		if( j==0 || j==gn ) u[1][i][j] = 0.0;
	} END_FOR;
}

static FLOAT ***gu = NULL;

// Clamped Fluid Flow Fetch
static FLOAT u_ref( int dir, int i, int j ) {
	if( dir == 0 )
		return gu[0][max(0,min(gn,i))][max(0,min(gn-1,j))];
	else
		return gu[1][max(0,min(gn-1,i))][max(0,min(gn,j))];
}

static void semiLagrangian( FLOAT **d, FLOAT **d0, int width, int height, FLOAT ***u ) {
	OPENMP_FOR for( int n=0; n<width*height; n++ ) {
		int i = n%width;
		int j = n/width;
		d[i][j] = interp::interp( d0, i-gn*u[0][i][j]*DT, j-gn*u[1][i][j]*DT, width, height );
	}
}

// Semi-Lagrangian Advection Method
static void advect_semiLagrangian( FLOAT ***u, FLOAT **out[2] ) {
	gu = u;

	// Compute Fluid Velocity At Each Staggered Faces
	static FLOAT **ux[2] = { alloc2D<FLOAT>(gn+1), alloc2D<FLOAT>(gn+1) };
	static FLOAT **uy[2] = { alloc2D<FLOAT>(gn+1), alloc2D<FLOAT>(gn+1) };
	
	OPENMP_FOR FOR_EVERY_X_FLOW(gn) {
		ux[0][i][j] = u[0][i][j];
		ux[1][i][j] = (u_ref(1,i-1,j)+u_ref(1,i,j)+u_ref(1,i-1,j+1)+u_ref(1,i,j+1))/4.0;
	} END_FOR;
	
	OPENMP_FOR FOR_EVERY_Y_FLOW(gn) {
		uy[0][i][j] = (u_ref(0,i,j-1)+u_ref(0,i,j)+u_ref(0,i+1,j)+u_ref(0,i+1,j-1))/4.0;
		uy[1][i][j] = u[1][i][j];
	} END_FOR;
	
	// BackTrace X Flow
	semiLagrangian( out[0], u[0], gn+1, gn, ux );
		
	// BackTrace Y Flow
	semiLagrangian( out[1], u[1], gn, gn+1, uy );
}

static void advect_fluid() {	
	static FLOAT **u_swap[2] = { NULL, NULL };
	if( ! u_swap[0] ) {
		u_swap[0] = alloc2D<FLOAT>(gn+1);
		u_swap[1] = alloc2D<FLOAT>(gn+1);
	}
	
	advect_semiLagrangian( u, u_swap );

	FOR_EVERY_X_FLOW(gn) {
		u[0][i][j] = u_swap[0][i][j];
	} END_FOR;
	FOR_EVERY_Y_FLOW(gn) {
		u[1][i][j] = u_swap[1][i][j];
	} END_FOR;
}

static void extrapolateVelocity() {
	static char **region = alloc2D<char>(gn);
	static FLOAT **q = alloc2D<FLOAT>(gn);

	// Map To LevelSet (X Direction)
	OPENMP_FOR FOR_EVERY_CELL(gn) {
		if( i<gn-1 && A[i][j]<0.0 ) {
			region[i][j] = 1;
			q[i][j] = (u[0][i][j]+u[0][i+1][j])*0.5;
		}
		else {
			region[i][j] = 0;
			q[i][j] = 0.0;
		}
	} END_FOR;
	
	// Extrapolate
	levelset2D::extrapolate( q, region );
	
	// Map Back (X Direction)
	OPENMP_FOR FOR_EVERY_X_FLOW(gn) {
		if( i>0 && i<gn && (A[i][j]>0.0 || A[i-1][j]>0.0) ) u[0][i][j] = (q[i][j]+q[i-1][j])*0.5;
	} END_FOR;
	
	// Map To LevelSet (Y Direction)
	OPENMP_FOR FOR_EVERY_CELL(gn) {
		if( j<gn-1 && A[i][j]<0.0 ) {
			region[i][j] = 1;
			q[i][j] = (u[1][i][j]+u[1][i][j+1])*0.5;
		} else {
			region[i][j] = 0;
			q[i][j] = 0.0;
		}
	} END_FOR;
	
	// Extrapolate
	levelset2D::extrapolate( q, region );
	
	// Map Back (Y Direction)
	OPENMP_FOR FOR_EVERY_Y_FLOW(gn) {
		if( j>0 && j<gn && (A[i][j]>0.0 || A[i][j-1]>0.0) ) u[1][i][j] = (q[i][j]+q[i][j-1])*0.5;
	} END_FOR;
}

static void flow( FLOAT x, FLOAT y, FLOAT &uu, FLOAT &vv, FLOAT &dt ) {
	x = (gn-1)*fmin(1.0,fmax(0.0,x))+0.5;
	y = (gn-1)*fmin(1.0,fmax(0.0,y))+0.5;
	int i = x;
	int j = y;
	uu = (1.0-(x-i))*u[0][i][j] + (x-i)*u[0][i+1][j];
	vv = (1.0-(y-j))*u[1][i][j] + (y-j)*u[1][i][j+1];
	dt = DT;
}

static void setMaxDistOfLevelSet() {
#if 0
	FLOAT max_vel = 0.0;
	FOR_EVERY_CELL(gn) {
		FLOAT xv = (u[0][i][j]+u[0][i+1][j])*0.5;
		FLOAT xu = (u[1][i][j]+u[1][i+1][j])*0.5;
		FLOAT vel = hypotf(xv,xu);
		if( vel > max_vel ) max_vel = vel;
	} END_FOR;
	maxdist = fmax(DIST, 1.5*DT*max_vel);
#endif
}

void liquid2D::display() {
	
	// Mark Liquid Domain
	markLiquid();

	// Visualize Everything
	render();
	
	// Add Gravity Force
	addGravity();
	
	// Compute Volume Error
	computeVolumeError();
	
	// Solve Fluid
	enforce_boundary();
	comp_divergence();
	compute_pressure();
	subtract_pressure();
	enforce_boundary();
	
	// Extrapolate Quantity
	extrapolateVelocity();
	
	// Advect Flow
	advect_fluid();
	
	// Advect
	levelset2D::advect(flow);
	
	// Redistancing
	if(do_redistance) {
		static int wait=0;
		if(wait++%REDIST==0) {
			setMaxDistOfLevelSet();
			levelset2D::redistance(maxdist);
		}
	}
}

void liquid2D::keyDown( char key ) {
	switch( key ) {
		case 'r':
			reset_count = (reset_count+1) % reset_num;
			levelset2D::buildLevelset(sphere,maxdist);
			volume0 = levelset2D::getVolume();
			y_volume0 = 0.0;
			FOR_EVERY_X_FLOW(gn) {
				u[0][i][j] = 0.0;
			} END_FOR;
			FOR_EVERY_Y_FLOW(gn) {
				u[1][i][j] = 0.0;
			} END_FOR;
			break;
		case 'v':
			show_velocity = ! show_velocity;
			break;
		case 'd':
			show_dist = ! show_dist;
			break;
		case 'g':
			show_grid = ! show_grid;
			break;
		case 'f':
			show_region = ! show_region;
			break;
		case 'p':
			subcell = ! subcell;
			break;
		case 'i':
			interpMethd = ! interpMethd;
			break;
		case 'a':
			do_redistance = ! do_redistance;
			if( do_redistance ) maxdist = DIST;
			else {
				maxdist = 1.0;
				levelset2D::redistance(maxdist);
			}
			break;
		case 'c':
			do_volumeCorrection = ! do_volumeCorrection;
			break;
		case 's':
			solver_mode = (solver_mode+1)%3;
			break;
	}
	levelset2D::setVisibility( show_grid, show_dist, show_region );
	interp::setInterpMethod(interpMethd);
}

static bool moved = false;
void liquid2D::mouse( FLOAT x, FLOAT y, int state ) {
	if( state ) {
		pressed = 1;
		mousep[0] = x;
		mousep[1] = y;
	} else pressed = 0;
	
	if( state == 0 && !moved ) {
		FLOAT b4volume = levelset2D::getVolume();
		
		FLOAT h = 0.1/gn;
		x = fmin(1.0-h,fmax(h,x));
		y = fmin(1.0-h,fmax(h,y));
		FOR_EVERY_CELL(gn) {
			FLOAT qx = i/(FLOAT)gn;
			FLOAT qy = j/(FLOAT)gn;
			if( hypot(x-qx,y-qy) < 0.1 ) {
				levelset2D::setLevelSet(i,j,-1.0);
				u[0][i][j] = u[0][i+1][j] = 0.0;
				u[1][i][j] = u[1][i][j+1] = 0.0;
			}
		} END_FOR;
		setMaxDistOfLevelSet();
		levelset2D::redistance(maxdist);
		
		FLOAT aftVolume = levelset2D::getVolume();
		volume0 += aftVolume-b4volume;
	} else if( state == 0 ) {
		moved = false;
	}
}

void liquid2D::motion( FLOAT x, FLOAT y, FLOAT dx, FLOAT dy ) {
	FLOAT h = 0.1/gn;
	mousep[0] = x;
	mousep[1] = y;
	x = gn*fmin(1.0-h,fmax(h,x));
	y = gn*fmin(1.0-h,fmax(h,y));
	int i = x;
	int j = y;
	FLOAT s = 500.0;
	u[0][i][j] += s*dx;
	u[0][i+1][j] += s*dx;
	
	u[1][i][j] += s*dy;
	u[1][i][j+1] += s*dy;
	moved = true;
}
