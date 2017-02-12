void model_init();
char *model_get();

typedef struct
{
        char name[32];
        int I8271,WD1770;
        int x65c02;
        int bplus;
        int master;
        int swram;
        int modela;
        int os01;
        int compact;
        char os[32];
        char romdir[32];
        char cmos[32];
        void (*romsetup)();
        int tube;
} MODEL;

extern MODEL models[17];

typedef struct
{
        char name[32];
        void (*init)();
        void (*reset)();
} TUBE;

extern TUBE tubes[7];

extern int curmodel, curtube, oldmodel, selecttube;
extern int I8271, WD1770, BPLUS, x65c02, MASTER, MODELA, OS01, compactcmos;
