#include <math.h>
#include <allegro.h>
#include "b-em.h"

#include "pal.h"
#include "video.h"
#include "video_render.h"

#define NCoef 1
fixed vision_iir(fixed NewSample) {
    static fixed x; //input samples

    x = (x + NewSample) >> 1;
//    x = NewSample;

    return x;
}


#undef NCoef
#define NCoef 2
static inline fixed chroma_iir(fixed NewSample) {
    static fixed y[NCoef+1]; //output samples
    static fixed x[NCoef+1]; //input samples

    x[2] = x[1];
    x[1] = x[0];
    x[0] = NewSample;
    y[2] = y[1];
    y[1] = y[0];


    //Calculate the new output
    y[0]  = fmul(49429, x[0]);
    y[0] -= fmul(12112, y[1]);
    y[0] -= fmul(49429, x[2]);
    y[0] -= fmul(21779, y[2]);

    return y[0];
}


//3.609
#define WT_INC ((4433618.75 / 16000000.0) * (2 * 3.14))

fixed sint[1024], cost[1024];
fixed sint2[1024], cost2[1024];
fixed cols[256][4];

fixed colx[256][16];

void pal_init()
{
        int c;
        float wt = 0.0;
        int r, g, b;
        for (c = 0; c < 1024; c++)
        {
                sint[c] = ftofix(sin(wt));
                cost[c] = ftofix(cos(wt));
                sint2[c] = fmul(40000, fmul(16622, sint[c]));
                cost2[c] = fmul(40000, fmul(9339, cost[c]));
                wt += (2.0 * 3.14) / 1024.0;
        }
        for (c = 0; c < 256; c++)
        {
                r = (c & 1) ? 255 : 0;
                g = (c & 2) ? 255 : 0;
                b = (c & 4) ? 255 : 0;

                cols[c][0] = ftofix( 0.299 * r + 0.587 * g + 0.114 * b);
                cols[c][1] = ftofix(-0.147 * r - 0.289 * g + 0.436 * b);
                cols[c][2] = ftofix( 0.615 * r - 0.515 * g - 0.100 * b);

                colx[c][0]  = fmul( 19595, c << 16);
                colx[c][1]  = fmul( 38470, c << 16);
                colx[c][2]  = fmul(  7471, c << 16);
                colx[c][4]  = fmul( -9634, c << 16);
                colx[c][5]  = fmul(-18940, c << 16);
                colx[c][6]  = fmul( 28574, c << 16);
                colx[c][8]  = fmul( 40305, c << 16);
                colx[c][9]  = fmul(-33751, c << 16);
                colx[c][10] = fmul( -6553, c << 16);
        }
}

void pal_convert(BITMAP *inb, int x1, int y1, int x2, int y2, int yoff)
{
        int x, y;
        int r, g, b;
        int pixel;
        fixed Y, U, V;
        fixed signal;
        static int wt;
        int old_wt;
        int col;

        fixed u_old[2][1536], v_old[2][1536];
        fixed u_filt[4], v_filt[4];
        fixed *uo[2], *vo[2];

        fixed sr[1536], sg[1536], sb[1536];

        int dx1 = (x1 * 922) / 832;
        int dx2 = (x2 * 922) / 832;

        int sx;
        int dx = 59139;

        int c = (crtc[0] + 1) * ((ula_ctrl & 0x10) ? 8 : 16);

        c = ((c * 256) * 922) / 832;

        if (x1 > 1535) x1 = 1535;
        if (x2 > 1536) x2 = 1536;
        if (y1 > 799) y1 = 799;
        if (y2 > 800) y2 = 800;

        for (x = x1; x < x2; x++)
            u_old[0][x] = u_old[1][x] = v_old[0][x] = v_old[1][x] = 0;
        for (x = 0; x < 4; x++)
            u_filt[x] = v_filt[x] = 0;

//        bem_debugf("PAL %i-%i %i-%i\n",x1,x2,dx1,dx2);


        for (y = y1; y < y2; y += yoff)
        {
                uo[0] = u_old[y&1];
                vo[0] = v_old[y&1];
                uo[1] = u_old[(y&1)^1];
                vo[1] = v_old[(y&1)^1];

                sx = x1 << 16;
                for (x = dx1; x < dx2; x++)
                {
                        pixel = inb->line[y][sx >> 16];
                        col = (0x10000 - (sx & 0xFFFF)) >> 8;
                        sr[x] = pal[pixel].r * col;
                        sg[x] = pal[pixel].g * col;
                        sb[x] = pal[pixel].b * col;

                        pixel = inb->line[y][(sx >> 16) + 1];
                        col = (sx & 0xFFFF) >> 8;
                        sr[x] += pal[pixel].r * col;
                        sg[x] += pal[pixel].g * col;
                        sb[x] += pal[pixel].b * col;

                        sx += dx;
                }

                old_wt = wt;

                for (x = dx1; x < dx2; x++)
                {
                        r = sr[x] >> 6;
                        g = sg[x] >> 6;
                        b = sb[x] >> 6;

                        Y = colx[r][0] + colx[g][1] + colx[b][2];
                        U = colx[r][4] + colx[g][5] + colx[b][6];
                        V = colx[r][8] + colx[g][9] + colx[b][10];

                        signal = (Y << 1) + chroma_iir(fmul(U, sint[wt]) + fmul(V, cost[wt]));

                        u_filt[x & 3] = fmul(signal, sint2[wt]);
                        v_filt[x & 3] = fmul(signal, cost2[wt]);
                        U = u_filt[0] + u_filt[1] + u_filt[2] + u_filt[3];
                        V = v_filt[0] + v_filt[1] + v_filt[2] + v_filt[3];
                        uo[0][x] = U;
                        vo[0][x] = V;
                        U += uo[1][x];
                        V += vo[1][x];

                        wt += 256;
                        wt &= 1023;

/*                        r = fixtoi(Y + fmul(9339, V));
                        g = fixtoi(Y - fmul(3244,U) - fmul(4760, V));
                        b = fixtoi(Y + fmul(16622,U));*/
                        r = fixtoi(Y + V);
                        g = fixtoi(Y - fmul(12790,  U) - fmul(33403, V));
                        b = fixtoi(Y + U);

                        if (r > 255) r = 255;
                        if (r < 0)   r = 0;
                        if (g > 255) g = 255;
                        if (g < 0)   g = 0;
                        if (b > 255) b = 255;
                        if (b < 0)   b = 0;

                        ((uint32_t *)b32->line[y])[x] = b | (g << 8) | (r << 16);
                }

                wt = old_wt - c;
//                wt += 769;
//                wt += (c - (256 * (dx2 - dx1)));
                wt &= 1023;
        }

        /*cheat*/
        wt -= (c * (312 - ((y2 - y1) / yoff)));
        if (crtc[8] & 1) wt -= (c >> 1);
        wt &= 1023;
}


/*Floating point version. This would be faster than the above, if B-em didn't crash with -O3 -msse*/

#if 0
#include <allegro.h>
#include "b-em.h"
#include "video.h"

#define NCoef 1
float vision_iir(float NewSample) {
    static float x; //input samples
    float y;

    x = (x + NewSample) * 0.5;
//    x = NewSample;

    return x;
}


#undef NCoef
#define NCoef 2
static inline float chroma_iir(float NewSample) {
    static float y[NCoef+1]; //output samples
    static float x[NCoef+1]; //input samples

    x[2] = x[1];
    x[1] = x[0];
    x[0] = NewSample;
    y[2] = y[1];
    y[1] = y[0];


    //Calculate the new output
    y[0]  = 0.754226 * x[0];
    y[0] -= 0.184815 * y[1];
    y[0] -= 0.754226 * x[2];
    y[0] -= 0.332316 * y[2];

    return y[0];
}



#define WT_INC ((4433618.75 / 16000000.0) * (2 * 3.14))

float sint[832*2], cost[832*2];
float cols[256][4];
void pal_init()
{
        int c;
        float wt = 0.0;
        int r, g, b;
        for (c = 0; c < 832*2; c++)
        {
                sint[c] = sin(wt);
                cost[c] = cos(wt);
                wt += WT_INC;
        }
        for (c = 0; c < 256; c++)
        {
                r = pal[c].r << 2;
                g = pal[c].g << 2;
                b = pal[c].b << 2;

                cols[c][0] =  0.299 * r + 0.587 * g + 0.114 * b;
                cols[c][1] = -0.147 * r - 0.289 * g + 0.436 * b;
                cols[c][2] =  0.615 * r - 0.515 * g - 0.100 * b;
        }
}

void pal_convert(BITMAP *inb, int x1, int y1, int x2, int y2)
{
        int x, y;
        int r, g, b;
        int pixel;
        int c;
        float Y, U, V;
        float signal;
        static int wt;
        float u_old[2][1536], v_old[2][1536];
        float u_filt[4], v_filt[4];
        float *uo[2], *vo[2];

        for (x = x1; x < x2; x++)
            u_old[0][x] = u_old[1][x] = v_old[0][x] = v_old[1][x] = 0.0;
        for (x = 0; x < 4; x++)
            u_filt[x] = v_filt[x] = 0.0;

        for (y = y1; y < y2; y++)
        {
                uo[0] = u_old[y&1];
                vo[0] = v_old[y&1];
                uo[1] = u_old[(y&1)^1];
                vo[1] = v_old[(y&1)^1];
                for (x = x1; x < x2; x++)
                {
                        pixel = inb->line[y][x];

                        Y = vision_iir(cols[pixel][0]);
                        U = cols[pixel][1];
                        V = cols[pixel][2];

                        signal = Y + chroma_iir(U * sint[wt] + V * cost[wt]);

                        u_filt[x & 3] = signal * sint[wt];
                        v_filt[x & 3] = signal * cost[wt];
                        U = u_filt[0] + u_filt[1] + u_filt[2] + u_filt[3];
                        V = v_filt[0] + v_filt[1] + v_filt[2] + v_filt[3];
                        uo[0][x] = U;
                        vo[0][x] = V;
                        U += uo[1][x];
                        V += vo[1][x];

                        wt++;

                        r = Y + (1.140/8.0) * V;
                        g = Y - (0.396/8.0) * U - (0.581/8.0) * V;
                        b = Y + (2.029/8.0) * U;

                        if (r > 255) r = 255;
                        if (r < 0)   r = 0;
                        if (g > 255) g = 255;
                        if (g < 0)   g = 0;
                        if (b > 255) b = 255;
                        if (b < 0)   b = 0;

                        ((uint32_t *)b32->line[y])[x] = b | (g << 8) | (r << 16);
                }

                wt += (1024 - (x2 - x1));
                wt %= 832;
        }
}

#endif
