/*  drawer.c 
    part of swftools

    A generic structure for providing vector drawing.
    (Helper routines, spline approximation, simple text drawers)

    Copyright (C) 2003 Matthias Kramm <kramm@quiss.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <math.h>
#include <ctype.h>
#include "drawer.h"

void draw_conicTo(drawer_t*draw, FPOINT*  c, FPOINT*  to)
{
    FPOINT* pos = &draw->pos;
    FPOINT c1,c2;
    c1.x = (pos->x + 2 * c->x) / 3;
    c1.y = (pos->y + 2 * c->y) / 3;
    c2.x = (2 * c->x + to->x) / 3;
    c2.y = (2 * c->y + to->y) / 3;
    draw_cubicTo(draw, &c1,&c2,to);

    draw->pos = *to;
}

struct SPLINEPOINT
{
    double x,y;
};

struct qspline
{
    struct SPLINEPOINT start;
    struct SPLINEPOINT control;
    struct SPLINEPOINT end;
};

struct cspline
{
    struct SPLINEPOINT start;
    struct SPLINEPOINT control1;
    struct SPLINEPOINT control2;
    struct SPLINEPOINT end;
};

static inline struct SPLINEPOINT cspline_getpoint(const struct cspline*s, double t)
{
    struct SPLINEPOINT p;
    double tt = t*t;
    double ttt = tt*t;
    double mt = (1-t);
    double mtmt = mt*(1-t);
    double mtmtmt = mtmt*(1-t);
    p.x= s->end.x*ttt + 3*s->control2.x*tt*mt
	    + 3*s->control1.x*t*mtmt + s->start.x*mtmtmt;
    p.y= s->end.y*ttt + 3*s->control2.y*tt*mt
	    + 3*s->control1.y*t*mtmt + s->start.y*mtmtmt;
    return p;
}
static struct SPLINEPOINT qspline_getpoint(const struct qspline*s, double t)
{
    struct SPLINEPOINT p;
    p.x= s->end.x*t*t + 2*s->control.x*t*(1-t) + s->start.x*(1-t)*(1-t);
    p.y= s->end.y*t*t + 2*s->control.y*t*(1-t) + s->start.y*(1-t)*(1-t);
    return p;
}

static int approximate3(const struct cspline*s, struct qspline*q, int size, double quality2)
{
//    unsigned int gran = 0;
    unsigned int istep = 0x80000000;
    unsigned int istart = 0;
    int num = 0;
    int level = 0;
    
    while(istart<0x80000000)
    {
	unsigned int iend = istart + istep;
	double start = istart/(double)0x80000000;
	double end = iend/(double)0x80000000;
	struct qspline test;
	double pos,qpos;
	char left = 0,recurse=0;
	int t;
	int probes = 15;

	/* create simple approximation: a qspline which run's through the
	   qspline point at 0.5 */
	test.start = cspline_getpoint(s, start);
	test.control = cspline_getpoint(s, (start+end)/2);
	test.end = cspline_getpoint(s, end);
	/* fix the control point:
	   move it so that the new spline does runs through it */
	test.control.x = -(test.end.x + test.start.x)/2 + 2*(test.control.x);
	test.control.y = -(test.end.y + test.start.y)/2 + 2*(test.control.y);

	/* depending on where we are in the spline, we either try to match
	   the left or right tangent */
	if(start<0.5) 
	    left=1;
	/* get derivative */
	pos = left?start:end;
	qpos = pos*pos;
	test.control.x = s->end.x*(3*qpos) + 3*s->control2.x*(2*pos-3*qpos) + 
		    3*s->control1.x*(1-4*pos+3*qpos) + s->start.x*(-3+6*pos-3*qpos);
	test.control.y = s->end.y*(3*qpos) + 3*s->control2.y*(2*pos-3*qpos) + 
		    3*s->control1.y*(1-4*pos+3*qpos) + s->start.y*(-3+6*pos-3*qpos);
	if(left) {
	    test.control.x *= (end-start)/2;
	    test.control.y *= (end-start)/2;
	    test.control.x += test.start.x;
	    test.control.y += test.start.y;
	} else {
	    test.control.x *= -(end-start)/2;
	    test.control.y *= -(end-start)/2;
	    test.control.x += test.end.x;
	    test.control.y += test.end.y;
	}

#define PROBES
#ifdef PROBES
	/* measure the spline's accurancy, by taking a number of probes */
	for(t=0;t<probes;t++) {
	    struct SPLINEPOINT qr1,qr2,cr1,cr2;
	    double pos = 0.5/(probes*2)*(t*2+1);
	    double dx,dy;
	    double dist1,dist2;
	    qr1 = qspline_getpoint(&test, pos);
	    cr1 = cspline_getpoint(s, start+pos*(end-start));

	    dx = qr1.x - cr1.x;
	    dy = qr1.y - cr1.y;
	    dist1 = dx*dx+dy*dy;

	    if(dist1>quality2) {
		recurse=1;break;
	    }
	    qr2 = qspline_getpoint(&test, (1-pos));
	    cr2 = cspline_getpoint(s, start+(1-pos)*(end-start));

	    dx = qr2.x - cr2.x;
	    dy = qr2.y - cr2.y;
	    dist2 = dx*dx+dy*dy;

	    if(dist2>quality2) {
		recurse=1;break;
	    }
	}
#else // quadratic error: *much* faster!

	/* convert control point representation to 
	   d*x^3 + c*x^2 + b*x + a */
	double dx,dy;
	dx= s->end.x  - s->control2.x*3 + s->control1.x*3 - s->start.x;
	dy= s->end.y  - s->control2.y*3 + s->control1.y*3 - s->start.y;
	
	/* we need to do this for the subspline between [start,end], not [0,1] 
	   as a transformation of t->a*t+b does nothing to highest coefficient
	   of the spline except multiply it with a^3, we just need to modify
	   d here. */
	{double m = end-start;
	 dx*=m*m*m;
	 dy*=m*m*m;
	}
	
	/* use the integral over (f(x)-g(x))^2 between 0 and 1
	   to measure the approximation quality. 
	   (it boils down to const*d^2)
	 */
	recurse = (dx*dx + dy*dy > quality2);
#endif

	if(recurse && istep>1 && size-level > num) {
	    istep >>= 1;
	    level++;
	} else {
	    *q++ = test;
	    num++;
	    istart += istep;
	    while(!(istart & istep)) {
		level--;
		istep <<= 1;
	    }
	}
    }
    return num;
}

void draw_cubicTo(drawer_t*draw, FPOINT*  control1, FPOINT* control2, FPOINT*  to)
{
    struct qspline q[128];
    struct cspline c;
    //double quality = 80;
    double maxerror = 1;//(500-(quality*5)>1?500-(quality*5):1)/20.0;
    int t,num;

    c.start.x = draw->pos.x;
    c.start.y = draw->pos.y;
    c.control1.x = control1->x;
    c.control1.y = control1->y;
    c.control2.x = control2->x;
    c.control2.y = control2->y;
    c.end.x = to->x;
    c.end.y = to->y;
    
    num = approximate3(&c, q, 128, maxerror*maxerror);

    for(t=0;t<num;t++) {
	FPOINT mid;
	FPOINT to;
	mid.x = q[t].control.x;
	mid.y = q[t].control.y;
	to.x = q[t].end.x;
	to.y = q[t].end.y;
	draw->splineTo(draw, &mid, &to);
    }
}


