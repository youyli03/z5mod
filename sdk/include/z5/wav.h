/* z5wav.h - WAV loading from the SD card (Round 530)
 *
 * The firmware's read primitive (z5_readat, 0x1020B0DE) silently does nothing when a
 * single call asks for more than about 512 bytes: 4 KB and 32 KB reads return "fine"
 * but write no data (measured on hardware, Round 518-523). Everything here therefore
 * reads 512 bytes at a time.
 */
#ifndef Z5_WAV_H
#define Z5_WAV_H

#include "z5.h"

#define Z5_WAV_CHUNK  512u          /* maximum working size of one z5_readat call */

/* Load the PCM payload of a WAV file into `dst` (the media-pool ring).
 * `cap` is the destination size in bytes; the copied length is written to *out_len.
 * Returns 0 on success, -1 if the file cannot be opened, -2 if it is not a WAV. */
int z5_wav_load(const char *path, void *dst, u32 cap, u32 *out_len);

#endif