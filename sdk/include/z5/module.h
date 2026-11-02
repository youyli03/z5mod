/* z5mod - the module entry contract. */
#ifndef Z5_MODULE_H
#define Z5_MODULE_H

#include "z5.h"

#define Z5_MAGIC 0x5443355Au        /* 'Z5CT', the magic the loader stamps */

typedef struct {
    u32 magic;      /* +0x00 'Z5CT'                                  */
    void *block;    /* +0x04 the image the loader allocated           */
    u32 length;     /* +0x08 image length                            */
    u32 sequence;   /* +0x0C load sequence number                     */
    void *element;  /* +0x10 the row object the callback received     */
    u32 r1, r2, r3; /* +0x14 the other three incoming registers       */
} z5_ctx;

/* Called once per redraw. Return 0 to leave the hooked row undrawn - the
 * module must have drawn it itself - or non-zero to let the firmware draw it. */
int z5_main(z5_ctx *ctx);

#endif /* Z5_MODULE_H */
