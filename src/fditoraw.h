typedef struct
{
        FILE *f;
        unsigned char header[512];
        int heads,tracks;
        unsigned char trackdata[90][2][2];
        int tracksize[90][2];
        int trackstart[90][2];
        int hd;
} FDI;

int fdi2raw_loadtrack(FDI *fdi, unsigned short *buffer, unsigned short *timing, int tracknum, int *tracklen, int *indexoffset, int *mr, int density);
FDI *fditoraw_open(FILE *f);
