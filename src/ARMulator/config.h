#ifndef _ARMUL_CONFIG_H
#define _ARMUL_CONFIG_H

#define TARGET_O_ACCMODE          0003
#define TARGET_O_RDONLY             00
#define TARGET_O_WRONLY             01
#define TARGET_O_RDWR               02
#define TARGET_O_CREAT            0100 /* not fcntl */
#define TARGET_O_EXCL             0200 /* not fcntl */
#define TARGET_O_NOCTTY           0400 /* not fcntl */
#define TARGET_O_TRUNC           01000 /* not fcntl */
#define TARGET_O_APPEND          02000
#define TARGET_O_NONBLOCK        04000
#define TARGET_O_NDELAY        TARGET_O_NONBLOCK
#define TARGET_O_SYNC           010000
#define TARGET_FASYNC           020000 /* fcntl, for BSD compatibility */
#define TARGET_O_DIRECTORY      040000 /* must be a directory */
#define TARGET_O_NOFOLLOW      0100000 /* don't follow links */
#define TARGET_O_DIRECT        0200000 /* direct disk access hint */
#define TARGET_O_LARGEFILE     0400000

#endif