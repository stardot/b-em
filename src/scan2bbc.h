/*PC Scancode to BBC col+row table*/

unsigned char scan2bbc[128]=            // IBM          SCAN CODE              BBC
{
    0xAA,0x70,0x30,0x31,                // .ESC12           0,1,2,3
    0x11,0x12,0x13,0x34,                // 3456             4,5,6,7
    0x24,0x15,0x26,0x27,                // 7890             8,9,10,11
    0x17,0x18,0x59,                     // -=DEL            12,13,14

    0x60,0x10,0x21,0x22,                // TABqwe           15,16,17,18        TABqwe
    0x33,0x23,0x44,0x35,                // rtyu             19,20,21,22        rtyu
    0x25,0x36,0x37,0x47,0x38,0x49,      // iop[]RET         23,24,25,26,27,28  iop@[RET (miss _)

    0x01,0x41,0x51,0x32,                // CTRLasd          29,30,31,32        CTRLasd
    0x43,0x53,0x54,0x45,                // fghj             33,34,35,36        fghj
    0x46,0x56,0x57,0x48,0x28,           // kl;'~            37,38,39,40,41     kl+*]

    0x00,0x58,0x61,0x42,0x52,           // LSH\zxc          42,43,44,45,46
    0x63,0x64,0x55,0x65,                // vbnm             47,48,49,50
    0x66,0x67,0x68,0x00,0x5b,           // <>?RSH.          51,52,53,54,55
    0x50,0x62,0x40,                     // ALT,SPC,CAPS     56,57,58           SHIFTLOCK,SPC,CAPs
    0x71,0x72,                          // f1,f2            59,60              f0,f1
    0x73,0x14,                          // f3,f4            61,62
    0x74,0x75,                          // f5,f6            63,64
    0x16,0x76,                          // f7,f8            65,66
    0x77,0x20,0xAA,                     // f9,f10           67,68,69
    0xAA,                               //                  70
    0xAA,0x39,0xAA,0x3b,                // hme upA pgu min  71,72,73,74
    0x19,0xaa,0x79,0x3a,                // lft mid rgt plu  74,75,76,77
    0x69,0x29,0xaa,                     // end dna pgd      78,79,80
    0xAA,0x59,                          // ins del ...      81,82
    0x4c,0xaa,                          //                  83,84
    0x78,                               //                  85
    0x28,0xaa,                          // f11,f12          86,88              --,BRK
    0xaa,0xaa,0xaa,0xaa,                //                  89,90,91,92
    0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa, //                  93-99
    0x6a,0x6b,0x7c,0x6c,0x7a,           //                  100-104
    0x7b,0x1a,0x1b,0x2a,0x2b,           //                  105-109
    0xaa,0x3c,0xaa,0x5a,0x4a,           //                  110-114
    0x4c,0x5c,0xaa,0xaa,0xaa,           //                  115-119
    0xaa,0xaa,0xaa,0xaa,0xaa,           //                  120-124
    0xaa,0xaa,0xaa                      //                  120-127
};
