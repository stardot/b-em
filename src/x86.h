#ifndef __INC_X86_H
#define __INC_X86_H

uint32_t oldpc;

#define setznp168 setznp16

#define getr8(r)   ((r&4)?regs[r&3].b.h:regs[r&3].b.l)

#define setr8(r,v) if (r&4) regs[r&3].b.h=v; \
                   else     regs[r&3].b.l=v;

#define fetchea()   { rmdat=readmembl(cs+pc); pc++;  \
                    reg=(rmdat>>3)&7;             \
                    mod=rmdat>>6;                 \
                    rm=rmdat&7;                   \
                    if (mod!=3) fetcheal(); }


#define JMP 1
#define CALL 2
#define IRET 3

#define EAX regs[0].l
#define ECX regs[1].l
#define EDX regs[2].l
#define EBX regs[3].l
#define ESP regs[4].l
#define EBP regs[5].l
#define ESI regs[6].l
#define EDI regs[7].l
#define AX regs[0].w
#define CX regs[1].w
#define DX regs[2].w
#define BX regs[3].w
#define SP regs[4].w
#define BP regs[5].w
#define SI regs[6].w
#define DI regs[7].w
#define AL regs[0].b.l
#define AH regs[0].b.h
#define CL regs[1].b.l
#define CH regs[1].b.h
#define DL regs[2].b.l
#define DH regs[2].b.h
#define BL regs[3].b.l
#define BH regs[3].b.h

typedef union
{
        uint32_t l;
        uint16_t w;
        struct
        {
                uint8_t l,h;
        } b;
} x86reg;

static x86reg regs[8];
static uint16_t flags;
static uint32_t oldds,oldss,x86pc;

typedef struct
{
        uint32_t base;
        uint16_t limit;
        uint8_t access;
        uint16_t seg;
} x86seg;

x86seg _cs,_ds,_es,_ss,_fs,_gs;

/*Segments -
  _cs,_ds,_es,_ss are the segment structures
  CS,DS,ES,SS is the 16-bit data
  cs,ds,es,ss are defines to the bases*/
//uint16_t CS,DS,ES,SS;
#define CS _cs.seg
#define DS _ds.seg
#define ES _es.seg
#define SS _ss.seg
#define FS _fs.seg
#define GS _gs.seg
#define cs _cs.base
#define ds _ds.base
#define es _es.base
#define ss _ss.base
#define fs _fs.base
#define gs _gs.base

#define C_FLAG  0x0001
#define P_FLAG  0x0004
#define A_FLAG  0x0010
#define Z_FLAG  0x0040
#define N_FLAG  0x0080
#define I_FLAG  0x0200
#define D_FLAG  0x0400
#define V_FLAG  0x0800
#define NT_FLAG 0x4000
#define VM_FLAG 0x0002 /*In EFLAGS*/

#endif
