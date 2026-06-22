#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <GL/glut.h>
#include "traffic.h"
#include "shared_state.h"


static SharedState *g_state = NULL;
static int   g_W = 860, g_H = 680;
static int   g_frame = 0;
static float g_time  = 0.f;          /* seconds since start, for animations */

/* ── Car ─────────────────────────────────────────────────────── */
typedef struct {
    float x, y;          /* current position (smoothly interpolated) */
    float tx, ty;        /* target position                          */
    float vx, vy;        /* velocity (for easing)                    */
    float r, g2, b;      /* colour                                   */
    int   dir;
    int   active;
} Car;
#define MAX_CARS 28
static Car g_cars[MAX_CARS];



/* ── Emergency banner alpha (0..1) ──────────────────────────── */
static float g_emg_alpha = 0.f;

/* ── PI ──────────────────────────────────────────────────────── */
#define PI 3.14159265f

/* ═══════════════════════════════════════════════════════════════
 *  Low-level helpers
 * ═══════════════════════════════════════════════════════════════ */

static void quad(float x,float y,float w,float h){
    glBegin(GL_QUADS);
    glVertex2f(x,y); glVertex2f(x+w,y);
    glVertex2f(x+w,y+h); glVertex2f(x,y+h);
    glEnd();
}

/* Rounded rectangle — approximated with GL_POLYGON */
static void rounded_rect(float x,float y,float w,float h,float cr){
    int seg=10;
    glBegin(GL_POLYGON);
    /* Each corner: quarter-circle of radius cr */
    for(int c=0;c<4;c++){
        float ox = (c==0||c==3) ? x+cr    : x+w-cr;
        float oy = (c==0||c==1) ? y+cr    : y+h-cr;
        float a0 = ((float)c+1)*PI/2.f + PI/2.f;
        for(int i=0;i<=seg;i++){
            float a = a0 + (float)i*PI/(2.f*seg);
            glVertex2f(ox+cr*cosf(a), oy+cr*sinf(a));
        }
    }
    glEnd();
}

/* Filled circle */
static void circle_f(float cx,float cy,float r,int n){
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx,cy);
    for(int i=0;i<=n;i++){
        float a=(float)i*2.f*PI/n;
        glVertex2f(cx+r*cosf(a),cy+r*sinf(a));
    }
    glEnd();
}

/* Arc (line) from a0 to a1 radians, anticlockwise */
static void arc_line(float cx,float cy,float r,float a0,float a1,
                     float lw,int n){
    glLineWidth(lw);
    glBegin(GL_LINE_STRIP);
    for(int i=0;i<=n;i++){
        float t=(float)i/(float)n;
        float a=a0+(a1-a0)*t;
        glVertex2f(cx+r*cosf(a),cy+r*sinf(a));
    }
    glEnd();
}

static void str12(float x,float y,const char*s){
    glRasterPos2f(x,y);
    for(;*s;s++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12,(int)*s);
}
static void str18(float x,float y,const char*s){
    glRasterPos2f(x,y);
    for(;*s;s++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18,(int)*s);
}
/* Linear interpolation */
static float lerp(float a,float b,float t){ return a+(b-a)*t; }

/* ═══════════════════════════════════════════════════════════════
 *  Traffic light — 3-bulb with animated glow
 * ═══════════════════════════════════════════════════════════════ */
static void draw_light(float cx, float cy, int state_val) {
    float R = 11.f;   /* bulb radius — smaller */
    float gap = R*2.4f;

    /* Housing — rounded rect */
    glColor3f(0.10f,0.10f,0.13f);
    rounded_rect(cx-R-6, cy-R-6-gap*2, (R+6)*2, (R+6)*2+gap*4+4, 5.f);

    /* Border */
    glColor3f(0.22f,0.22f,0.28f);
    glLineWidth(1.5f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(cx-R-6,cy-R-6-gap*2); glVertex2f(cx+R+6,cy-R-6-gap*2);
    glVertex2f(cx+R+6,cy+R+6+gap*2); glVertex2f(cx-R-6,cy+R+6+gap*2);
    glEnd();

    /* Slot positions: RED top, YELLOW mid, GREEN bottom */
    float sy[3] = { cy+gap*2, cy, cy-gap*2 };
    int   lit[3] = {
        state_val==LIGHT_RED,
        state_val==LIGHT_YELLOW,
        state_val==LIGHT_GREEN
    };
    /* Target colours for each slot */
    float tc[3][3] = {
        {0.95f,0.12f,0.12f},   /* red    */
        {0.98f,0.82f,0.08f},   /* yellow */
        {0.08f,0.92f,0.20f}    /* green  */
    };

    for(int s=0;s<3;s++){
        /* Dark socket */
        glColor3f(0.05f,0.05f,0.06f);
        circle_f(cx,sy[s],R,24);

        if(lit[s]){
            /* Animated pulse — ±8% brightness using sin */
            float pulse = 1.f + 0.08f*sinf(g_time*4.f);
            glColor3f(fminf(tc[s][0]*pulse,1.f),
                      fminf(tc[s][1]*pulse,1.f),
                      fminf(tc[s][2]*pulse,1.f));
            circle_f(cx,sy[s],R*0.78f,24);

            /* Soft glow ring (blended) */
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(tc[s][0],tc[s][1],tc[s][2],0.28f+0.08f*sinf(g_time*4.f));
            circle_f(cx,sy[s],R*1.25f,24);
            glDisable(GL_BLEND);

            /* Inner highlight (white top-left) */
            glColor3f(1.f,1.f,1.f);
            circle_f(cx-R*0.22f, sy[s]+R*0.22f, R*0.18f, 12);
        } else {
            /* Dim unlit bulb */
            glColor3f(tc[s][0]*0.12f,tc[s][1]*0.12f,tc[s][2]*0.12f);
            circle_f(cx,sy[s],R*0.70f,24);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Arc-style phase timer (ring in top-right corner)
 * ═══════════════════════════════════════════════════════════════ */
static void draw_phase_timer(float cx,float cy,float R,
                              int phase,int sl,int pt){
    float frac = (pt>0) ? (float)sl/(float)pt : 0.f;

    /* Phase colour */
    float cr=0.20f,cg=0.72f,cb=0.25f;
    if(phase==PHASE_NS_YELLOW||phase==PHASE_EW_YELLOW){cr=0.92f;cg=0.72f;cb=0.06f;}
    if(phase==PHASE_ALL_RED_1||phase==PHASE_ALL_RED_2){cr=0.80f;cg=0.12f;cb=0.12f;}
    if(phase==PHASE_PEDESTRIAN){cr=0.08f;cg=0.52f;cb=0.92f;}
    if(phase==PHASE_EMERGENCY) {cr=0.95f;cg=0.06f;cb=0.06f;}

    /* Background ring */
    glColor3f(0.14f,0.14f,0.20f);
    arc_line(cx,cy,R, -PI/2.f, -PI/2.f+2.f*PI, 8.f, 60);

    /* Progress arc — goes clockwise from top, shrinks as time runs out */
    if(frac>0.f){
        glColor3f(cr,cg,cb);
        arc_line(cx,cy,R, -PI/2.f, -PI/2.f+2.f*PI*frac, 8.f, 60);
    }

    /* Centre: seconds remaining */
    glColor3f(0.88f,0.88f,0.92f);
    char buf[8]; snprintf(buf,sizeof buf,"%d",sl);
    str18(cx - (sl>=10?10.f:5.f), cy-7.f, buf);

    /* Phase name below ring */
    glColor3f(0.50f,0.50f,0.58f);
    str12(cx-30.f, cy-R-16.f, PHASE_NAME[phase]);
}

/* ═══════════════════════════════════════════════════════════════
 *  Cars
 * ═══════════════════════════════════════════════════════════════ */
static void maybe_spawn(int dir,int veh){
    if(veh==0) return;
    int cnt=0;
    for(int i=0;i<MAX_CARS;i++)
        if(g_cars[i].active&&g_cars[i].dir==dir) cnt++;
    int cap=veh<5?veh:5;
    if(cnt>=cap) return;

    for(int i=0;i<MAX_CARS;i++){
        if(g_cars[i].active) continue;
        float cx=g_W*0.5f,cy=g_H*0.5f;
        float gap=32.f*(float)cnt;
        float x,y,tx,ty;
        switch(dir){
            case DIR_NORTH: x=cx-19;y=g_H-22-gap; tx=cx-19;ty=cy+56; break;
            case DIR_SOUTH: x=cx+19;y=22+gap;      tx=cx+19;ty=cy-56; break;
            case DIR_EAST:  x=g_W-22-gap;y=cy+19;  tx=cx+56;ty=cy+19; break;
            default:        x=22+gap;y=cy-19;       tx=cx-56;ty=cy-19; break;
        }
        float spd = 1.4f+(float)(rand()%10)*0.12f;
        static const float cols[8][3]={
            {0.92f,0.20f,0.20f},{0.20f,0.52f,0.92f},
            {0.20f,0.88f,0.36f},{0.95f,0.78f,0.10f},
            {0.82f,0.20f,0.88f},{0.95f,0.52f,0.10f},
            {0.10f,0.88f,0.88f},{0.88f,0.52f,0.20f}
        };
        int ci=rand()%8;
        g_cars[i]=(Car){x,y,tx,ty,0,0,
                        cols[ci][0],cols[ci][1],cols[ci][2],dir,1};
        (void)spd;
        break;
    }
}

static void update_cars(void){
    if(!g_state) return;

    int ls[DIR_COUNT],veh[DIR_COUNT];
    for(int d=0;d<DIR_COUNT;d++){
        ls[d] =g_state->light_state[d];
        veh[d]=g_state->vehicles_waiting[d];
    }

    if(g_frame%18==0)
        for(int d=0;d<DIR_COUNT;d++) maybe_spawn(d,veh[d]);

    float gcx=g_W*0.5f,gcy=g_H*0.5f,cross=52.f;

    for(int i=0;i<MAX_CARS;i++){
        if(!g_cars[i].active) continue;
        int dir=g_cars[i].dir;
        int green=(ls[dir]==LIGHT_GREEN);

        /* Update target when light changes */
        if(green){
            switch(dir){
                case DIR_NORTH: g_cars[i].tx=gcx-19;g_cars[i].ty=gcy-cross-22; break;
                case DIR_SOUTH: g_cars[i].tx=gcx+19;g_cars[i].ty=gcy+cross+22; break;
                case DIR_EAST:  g_cars[i].tx=gcx-cross-22;g_cars[i].ty=gcy+19; break;
                default:        g_cars[i].tx=gcx+cross+22;g_cars[i].ty=gcy-19; break;
            }
        }

        float dx=g_cars[i].tx-g_cars[i].x;
        float dy=g_cars[i].ty-g_cars[i].y;
        float dist=sqrtf(dx*dx+dy*dy);

        if(dist<1.5f){
            if(green){ g_cars[i].active=0; continue; }
            g_cars[i].vx*=0.8f; g_cars[i].vy*=0.8f;
        } else {
            /* Eased velocity: accelerate toward target, slow near it */
            float ease = fminf(dist/60.f, 1.f);  /* 0..1 */
            float max_spd = 2.2f * ease + 0.3f;
            float nx=dx/dist, ny=dy/dist;
            g_cars[i].vx = lerp(g_cars[i].vx, nx*max_spd, 0.18f);
            g_cars[i].vy = lerp(g_cars[i].vy, ny*max_spd, 0.18f);
        }

        g_cars[i].x += g_cars[i].vx;
        g_cars[i].y += g_cars[i].vy;

        if(g_cars[i].x<-60||g_cars[i].x>g_W+60||
           g_cars[i].y<-60||g_cars[i].y>g_H+60)
            g_cars[i].active=0;
    }
}

static void draw_car(float x,float y,int dir,float cr,float cg,float cb){
    int ns=(dir==DIR_NORTH||dir==DIR_SOUTH);
    float bw=ns?13.f:22.f, bh=ns?22.f:13.f;

    /* Drop shadow */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.f,0.f,0.f,0.28f);
    quad(x-bw*.5f+3,y-bh*.5f-3,bw,bh);
    glDisable(GL_BLEND);

    /* Body */
    glColor3f(cr,cg,cb);
    quad(x-bw*.5f,y-bh*.5f,bw,bh);

    /* Roof highlight */
    glColor3f(fminf(cr+0.28f,1.f),fminf(cg+0.28f,1.f),fminf(cb+0.28f,1.f));
    float hw=bw*0.5f, hh=bh*0.35f;
    float hox=(dir==DIR_EAST)?bw*0.1f:(dir==DIR_WEST)?-bw*0.1f:0.f;
    float hoy=(dir==DIR_NORTH)?bh*0.1f:(dir==DIR_SOUTH)?-bh*0.1f:0.f;
    quad(x-hw*0.5f+hox,y-hh*0.5f+hoy,hw,hh);

    /* Wheels */
    glColor3f(0.10f,0.10f,0.12f);
    circle_f(x-bw*.38f,y-bh*.40f,2.8f,8);
    circle_f(x+bw*.38f,y-bh*.40f,2.8f,8);
    circle_f(x-bw*.38f,y+bh*.40f,2.8f,8);
    circle_f(x+bw*.38f,y+bh*.40f,2.8f,8);

    /* Headlights / tail lights */
    if(dir==DIR_NORTH){
        glColor3f(1.f,0.95f,0.70f);
        circle_f(x-bw*.3f,y+bh*.5f-1,1.8f,8);
        circle_f(x+bw*.3f,y+bh*.5f-1,1.8f,8);
        glColor3f(0.95f,0.10f,0.10f);
        circle_f(x-bw*.3f,y-bh*.5f+1,1.5f,8);
        circle_f(x+bw*.3f,y-bh*.5f+1,1.5f,8);
    } else if(dir==DIR_SOUTH){
        glColor3f(1.f,0.95f,0.70f);
        circle_f(x-bw*.3f,y-bh*.5f+1,1.8f,8);
        circle_f(x+bw*.3f,y-bh*.5f+1,1.8f,8);
        glColor3f(0.95f,0.10f,0.10f);
        circle_f(x-bw*.3f,y+bh*.5f-1,1.5f,8);
        circle_f(x+bw*.3f,y+bh*.5f-1,1.5f,8);
    } else if(dir==DIR_EAST){
        glColor3f(1.f,0.95f,0.70f);
        circle_f(x-bw*.5f+1,y-bh*.3f,1.8f,8);
        circle_f(x-bw*.5f+1,y+bh*.3f,1.8f,8);
    } else {
        glColor3f(1.f,0.95f,0.70f);
        circle_f(x+bw*.5f-1,y-bh*.3f,1.8f,8);
        circle_f(x+bw*.5f-1,y+bh*.3f,1.8f,8);
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Main display callback
 * ═══════════════════════════════════════════════════════════════ */
static void on_display(void){
    g_W=glutGet(GLUT_WINDOW_WIDTH);
    g_H=glutGet(GLUT_WINDOW_HEIGHT);

    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();
    glOrtho(0,g_W,0,g_H,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();

    /* Enable smooth lines */
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT,GL_NICEST);

    /* Background — dark asphalt */
    glColor3f(0.13f,0.14f,0.13f);
    quad(0,0,g_W,g_H);

    if(!g_state){glutSwapBuffers();return;}

    /* Snapshot shared state */
    int ls[DIR_COUNT],veh[DIR_COUNT],ped_req[DIR_COUNT];
    int phase,sl,pt,ped_active,ped_dir,emg_active,emg_dir,sv;
    for(int d=0;d<DIR_COUNT;d++){
        ls[d]=g_state->light_state[d];
        veh[d]=g_state->vehicles_waiting[d];
        ped_req[d]=g_state->ped_request[d];
    }
    phase     =g_state->current_phase;
    sl        =g_state->phase_seconds_left;
    pt        =g_state->phase_total>0?g_state->phase_total:1;
    ped_active=g_state->ped_active;
    ped_dir   =g_state->ped_direction;
    emg_active=g_state->emergency_active;
    emg_dir   =g_state->emergency_direction;
    sv        =g_state->safety_violations;

    float cx=g_W*.5f,cy=g_H*.5f,road=40.f;

    /* ── Sidewalk blocks (city blocks at corners) ── */
    glColor3f(0.22f,0.22f,0.20f);
    quad(0,           cy+road, cx-road,     g_H-(cy+road));  /* NW */
    quad(cx+road,     cy+road, g_W-(cx+road),g_H-(cy+road)); /* NE */
    quad(0,           0,       cx-road,     cy-road);        /* SW */
    quad(cx+road,     0,       g_W-(cx+road),cy-road);       /* SE */

    /* Sidewalk edge lines */
    glColor3f(0.28f,0.28f,0.26f);
    glLineWidth(1.f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(cx-road,cy+road); glVertex2f(cx+road,cy+road);
    glVertex2f(cx+road,cy-road); glVertex2f(cx-road,cy-road);
    glEnd();

    /* ── Roads ── */
    glColor3f(0.24f,0.26f,0.24f);
    quad(cx-road,0,road*2,g_H);
    quad(0,cy-road,g_W,road*2);

    /* ── Intersection box ── */
    glColor3f(0.20f,0.22f,0.20f);
    quad(cx-road,cy-road,road*2,road*2);

    /* ── Dashed centre lines ── */
    glColor3f(0.68f,0.64f,0.46f);
    glLineWidth(2.f);
    glEnable(GL_LINE_STIPPLE);
    glLineStipple(2,0x0F0F);
    glBegin(GL_LINES);
    glVertex2f(cx,0);      glVertex2f(cx,cy-road);
    glVertex2f(cx,cy+road);glVertex2f(cx,g_H);
    glVertex2f(0,cy);      glVertex2f(cx-road,cy);
    glVertex2f(cx+road,cy);glVertex2f(g_W,cy);
    glEnd();
    glDisable(GL_LINE_STIPPLE);

    /* ── Stop lines ── */
    glColor3f(0.88f,0.88f,0.88f);
    glLineWidth(3.f);
    glBegin(GL_LINES);
    /* North approach: left lane enters from right side of N-S road */
    glVertex2f(cx-road,cy+road); glVertex2f(cx,cy+road);
    /* South approach */
    glVertex2f(cx,cy-road);      glVertex2f(cx+road,cy-road);
    /* East approach */
    glVertex2f(cx+road,cy);      glVertex2f(cx+road,cy+road);
    /* West approach */
    glVertex2f(cx-road,cy-road); glVertex2f(cx-road,cy);
    glEnd();

    /* ── Animated pedestrian crossing ── */
    if(ped_active&&ped_dir>=0){
        /* Flashing stripes */
        float alpha=0.55f+0.35f*sinf(g_time*3.f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(0.90f,0.90f,0.90f,alpha);
        if(ped_dir==DIR_NORTH||ped_dir==DIR_SOUTH){
            for(int k=0;k<5;k++) quad(cx-road+(float)k*9,cy+road+2,6,18);
        } else {
            for(int k=0;k<5;k++) quad(cx+road+2,cy-road+(float)k*9,18,6);
        }
        glDisable(GL_BLEND);

        /* CROSSING label */
        glColor3f(0.12f,0.92f,0.38f);
        if(ped_dir==DIR_NORTH||ped_dir==DIR_SOUTH)
            str12(cx-20,cy+(ped_dir==DIR_NORTH?road+26:-road-10),"CROSSING");
        else
            str12(cx+(ped_dir==DIR_EAST?road+4:-road-52),cy+4,"CROSSING");
    }

    /* ── Cars ── */
    for(int i=0;i<MAX_CARS;i++){
        if(!g_cars[i].active) continue;
        draw_car(g_cars[i].x,g_cars[i].y,g_cars[i].dir,
                 g_cars[i].r,g_cars[i].g2,g_cars[i].b);
    }

    /* ── Traffic lights (with animated glow) ── */
    float lpd=road+70.f;
    draw_light(cx,      cy+lpd, ls[DIR_NORTH]);
    draw_light(cx,      cy-lpd, ls[DIR_SOUTH]);
    draw_light(cx+lpd,  cy,     ls[DIR_EAST]);
    draw_light(cx-lpd,  cy,     ls[DIR_WEST]);

    /* Direction labels */
    glColor3f(0.65f,0.65f,0.70f);
    str12(cx-5,  cy+lpd+44, "N");
    str12(cx-5,  cy-lpd-16, "S");
    str12(cx+lpd+28,cy-5,   "E");
    str12(cx-lpd-26,cy-5,   "W");

    /* ── Pedestrian request buttons ── */
    for(int d=0;d<DIR_COUNT;d++){
        if(!ped_req[d]) continue;
        float bx=cx,by=cy;
        if(d==DIR_NORTH){bx=cx-road-48;by=cy+lpd;}
        if(d==DIR_SOUTH){bx=cx+road+14;by=cy-lpd;}
        if(d==DIR_EAST) {bx=cx+lpd;    by=cy+road+32;}
        if(d==DIR_WEST) {bx=cx-lpd;    by=cy-road-32;}
        /* Pulsing button */
        float pulse=0.85f+0.15f*sinf(g_time*5.f);
        glColor3f(0.95f*pulse,0.72f*pulse,0.08f*pulse);
        circle_f(bx,by,8.f,14);
        glColor3f(0.08f,0.08f,0.08f);
        str12(bx+11,by-4,"PED");
    }

    /* ── Animated emergency banner ── */
    /* Smooth fade in/out */
    if(emg_active&&emg_dir>=0){
        g_emg_alpha=lerp(g_emg_alpha,1.f,0.08f);
    } else {
        g_emg_alpha=lerp(g_emg_alpha,0.f,0.06f);
    }
    if(g_emg_alpha>0.01f){
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        /* Pulsing red band */
        float p=0.85f+0.15f*sinf(g_time*6.f);
        glColor4f(0.88f*p,0.06f,0.06f,g_emg_alpha);
        quad(0,(float)g_H-52,(float)g_W,44);
        glColor4f(1.f,1.f,1.f,g_emg_alpha);
        glDisable(GL_BLEND);
        if(emg_dir>=0){
            char eb[80];
            snprintf(eb,sizeof eb,
                     "⚠  EMERGENCY: %s direction  —  all others RED",
                     DIR_NAME[emg_dir]);
            str18((float)g_W*.5f-210,(float)g_H-24,eb);
        }
    }

    /* ── Info panel (bottom-left) ── */
    float px=8,py=8,pw=260,ph=110;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.08f,0.08f,0.12f,0.88f);
    rounded_rect(px,py,pw,ph,8.f);
    glDisable(GL_BLEND);
    glColor3f(0.22f,0.22f,0.30f);
    glLineWidth(1.f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(px,py);glVertex2f(px+pw,py);
    glVertex2f(px+pw,py+ph);glVertex2f(px,py+ph);glEnd();

    glColor3f(0.85f,0.85f,0.90f);
    char lb[48];
    snprintf(lb,sizeof lb,"Phase: %s",PHASE_NAME[phase]);
    str12(px+10,py+ph-20,lb);

    glColor3f(0.55f,0.72f,0.88f);
    snprintf(lb,sizeof lb,"Cars  N:%-2d S:%-2d E:%-2d W:%-2d",
             veh[0],veh[1],veh[2],veh[3]);
    str12(px+10,py+ph-38,lb);

    /* Ped requests */
    glColor3f(0.88f,0.72f,0.20f);
    char peds[48]="Ped:";
    int any_ped=0;
    for(int d=0;d<DIR_COUNT;d++)
        if(ped_req[d]){
            char tmp[8]; snprintf(tmp,sizeof tmp," %s",DIR_NAME[d]);
            strcat(peds,tmp); any_ped=1;
        }
    if(!any_ped) strcat(peds," none");
    str12(px+10,py+ph-56,peds);

    if(sv>0){
        glColor3f(0.95f,0.18f,0.18f);
        snprintf(lb,sizeof lb,"Safety violations: %d",sv);
        str12(px+10,py+ph-74,lb);
    }

    /* Timer: seconds left */
    glColor3f(0.45f,0.45f,0.52f);
    snprintf(lb,sizeof lb,"Timer: %ds / %ds",sl,pt);
    str12(px+10,py+10,lb);

    /* ── Arc timer (top-right) ── */
    float tr_cx=g_W-70.f, tr_cy=g_H-70.f;
    draw_phase_timer(tr_cx,tr_cy,42.f,phase,sl,pt);

    /* ── Title ── */
    glColor3f(0.55f,0.55f,0.62f);
    str18((float)g_W*.5f-155,12.f,"Real-Time Traffic Light Control");

    glDisable(GL_LINE_SMOOTH);
    glutSwapBuffers();
}

/* ═══════════════════════════════════════════════════════════════
 *  Timer — 60 fps
 * ═══════════════════════════════════════════════════════════════ */
static void on_timer(int v){
    (void)v;
    g_frame++;
    g_time += 0.016f;   /* ~60fps → 16ms per frame */
    update_cars();
    glutPostRedisplay();
    glutTimerFunc(16,on_timer,0);   /* 16ms ≈ 60fps */
}

/* ═══════════════════════════════════════════════════════════════
 *  Entry point
 * ═══════════════════════════════════════════════════════════════ */
void run_display(int shmid,int semid){
    (void)semid;
    g_state=shm_attach(shmid);
    memset(g_cars,0,sizeof g_cars);
    srand(42);

    int argc=0;
    glutInit(&argc,NULL);
    glutInitDisplayMode(GLUT_DOUBLE|GLUT_RGB);
    glutInitWindowSize(g_W,g_H);
    glutInitWindowPosition(50,30);
    glutCreateWindow("Traffic Light Control System");

    glClearColor(0.13f,0.14f,0.13f,1.f);

    glutDisplayFunc(on_display);
    glutTimerFunc(16,on_timer,0);
    glutMainLoop();
}
