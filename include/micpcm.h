/*
 * micpcm.h -- microphone samples into the int32 frames the recorder
 * takes (5207).
 *
 * Three small conversions, each the kind that is wrong quietly: an
 * in-place unpack that overwrites a sample before reading it, a partial
 * frame dropped at a read boundary so a stereo file swaps sides from
 * there on, a sum that rounds one way for positive samples and the other
 * for negative. Header-only and inline for pcmfold.h's reason, and
 * host-tested in test/micpcmtest.c, which audio_out.c and uac.c,
 * full of ESP-IDF includes, could never be.
 *
 * Every output sample is an int32 holding a sign-extended value of the
 * source's width. flacenc takes that as it is.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef MICPCM_H
#define MICPCM_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * One slot out of n TDM frames of `slots` little-endian int16 each, in
 * place: frames[i] = slot `slot` of raw frame i.
 *
 * IN PLACE, FORWARD. A raw frame is slots * 2 bytes and an output frame
 * is 4, so for slots >= 2 output i (bytes 4i..4i+3) never reaches a raw
 * frame not yet read (bytes slots*2*i onward, which is >= 4i + 4 for
 * every later frame). slots < 2 would overrun its own input and is
 * refused: 0 is returned. memcpy, not an int16_t pointer into the int32
 * array, which would break strict aliasing.
 */
static inline size_t micpcm_tdm_slot(int32_t *frames, size_t n, unsigned slots, unsigned slot)
{
    if (slots < 2 || slot >= slots) return 0;
    const uint8_t *raw = (const uint8_t *)frames;
    for (size_t i = 0; i < n; i++) {
        int16_t s;
        memcpy(&s, raw + (i * slots + slot) * sizeof(int16_t), sizeof(s));
        frames[i] = s;
    }
    return n;
}

/*
 * 5212: two slots out of n TDM frames of `slots` little-endian int16
 * each, as interleaved stereo, in place: frames[2i] = slot a of raw frame
 * i, frames[2i + 1] = slot b, each shifted left by `shift` (8 carries 16
 * bits to 24-bit scale).
 *
 * IN PLACE, FORWARD. Output frame i is 8 bytes at 8i; raw frame i is
 * slots * 2 bytes at slots * 2 * i. For slots >= 4 output i never
 * reaches past raw frame i, whose two samples are read before either is
 * written. slots < 4, a or b out of range, or a shift over 15 return 0.
 */
static inline size_t micpcm_tdm_pair(int32_t *frames, size_t n, unsigned slots,
                                     unsigned a, unsigned b, unsigned shift)
{
    if (slots < 4 || a >= slots || b >= slots || shift > 15) return 0;
    const uint8_t *raw = (const uint8_t *)frames;
    for (size_t i = 0; i < n; i++) {
        int16_t sa, sb;
        memcpy(&sa, raw + (i * slots + a) * sizeof(int16_t), sizeof(sa));
        memcpy(&sb, raw + (i * slots + b) * sizeof(int16_t), sizeof(sb));
        frames[2 * i]     = (int32_t)sa * (1 << shift);
        frames[2 * i + 1] = (int32_t)sb * (1 << shift);
    }
    return n;
}

/*
 * Interleaved little-endian int16 in `raw`: `carry` bytes left over from
 * the last call, then `got` new ones. Whole frames of `ch` channels are
 * widened into out; the bytes of a partial frame at the end are moved to
 * the front of raw and their count stored in *carry_out, for the next
 * call to put the rest after. Returns the frames written: out must hold
 * ((carry + got) / (ch * 2)) * ch int32. ch 0 returns 0 and keeps
 * nothing.
 */
/*
 * The same for any USB audio subframe: `sub` bytes a sample, little-
 * endian, two's complement (UAC 1.0 Type I PCM).
 *
 *   2   int16, kept as it is: a 16-bit value.
 *   3   24 bits packed, sign-extended from bit 23: a 24-bit value.
 *   4   a 32-bit word whose top 24 bits are the sample -- the spec has a
 *       sample left-justified in its subframe, so a 24-bit device in
 *       4-byte subframes and a 32-bit one both come out at 24-bit scale,
 *       by an arithmetic shift right of 8.
 *
 * So every output is a sign-extended value of 16 or 24 bits, and which
 * is the width the caller opened. Any other `sub`, or ch 0, returns 0
 * and keeps nothing. Partial frames carry as micpcm_s16_take() says.
 */
static inline size_t micpcm_take(uint8_t *raw, size_t carry, size_t got, unsigned ch,
                                 unsigned sub, int32_t *out, size_t *carry_out)
{
    if (!ch || (sub != 2 && sub != 3 && sub != 4)) { *carry_out = 0; return 0; }
    const size_t fb = (size_t)ch * sub;
    const size_t have = carry + got;
    const size_t n = have / fb;
    for (size_t i = 0; i < n * ch; i++) {
        const uint8_t *p = raw + i * sub;
        if (sub == 2) {
            int16_t s;
            memcpy(&s, p, sizeof(s));
            out[i] = s;
        } else if (sub == 3) {
            const uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
            out[i] = (int32_t)(u ^ 0x800000u) - 0x800000;      /* sign from bit 23 */
        } else {
            int32_t s;
            memcpy(&s, p, sizeof(s));
            out[i] = s >> 8;            /* arithmetic: GCC and Clang guarantee it */
        }
    }
    const size_t rest = have - n * fb;
    if (rest) memmove(raw, raw + n * fb, rest);
    *carry_out = rest;
    return n;
}

static inline size_t micpcm_s16_take(uint8_t *raw, size_t carry, size_t got, unsigned ch,
                                     int32_t *out, size_t *carry_out)
{
    return micpcm_take(raw, carry, got, ch, 2, out, carry_out);
}

/*
 * n stereo frames to n mono samples, in place: the mean of left and
 * right, rounded toward negative infinity the same way for every sign
 * (an arithmetic shift of the sum, which GCC and Clang both guarantee on
 * a signed int64). The sum is taken in int64 so no width a microphone
 * produces can overflow it. Output i is written after input 2i and 2i+1
 * are read, and 2i >= i, so forward is safe.
 */
/*
 * TDM slot alignment, found from the data.
 *
 * The capture channel is an I2S SLAVE on clocks the playback channel
 * already drives, so when it is enabled it starts taking 16-bit words
 * wherever the frame happens to be -- not necessarily at slot 0. Every
 * slot then arrives `shift` positions later in each four-word group
 * than the codec sent it, for the whole capture. A board showed every
 * shift from 0 to 3 across restarts of the same capture.
 *
 * The ES7210 as configured here never drives its slot 0: in every probe
 * line, once the first second's start-up noise is past, slot 0 reads
 * 0 nonzero samples in 48000. So the one raw position that stays silent
 * is where slot 0 landed, and that position IS the shift.
 *
 * micpcm_tdm_count() adds up, per raw position, how many samples in a
 * block were nonzero. micpcm_tdm_silent() names the position with no
 * more than `max_nz` of them, if exactly one has; -1 if none or several
 * did, which is not enough to decide on.
 */
static inline void micpcm_tdm_count(const int32_t *frames, size_t n, unsigned slots,
                                    uint32_t *nz)
{
    const uint8_t *raw = (const uint8_t *)frames;
    for (size_t i = 0; i < n; i++) {
        for (unsigned k = 0; k < slots; k++) {
            int16_t s;
            memcpy(&s, raw + (i * slots + k) * sizeof(int16_t), sizeof(s));
            if (s) nz[k]++;
        }
    }
}

static inline int micpcm_tdm_silent(const uint32_t *nz, unsigned slots, uint32_t max_nz)
{
    int found = -1;
    for (unsigned k = 0; k < slots; k++) {
        if (nz[k] > max_nz) continue;
        if (found >= 0) return -1;      /* two quiet positions: undecided */
        found = (int)k;
    }
    return found;
}

static inline void micpcm_mono(int32_t *frames, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        const int64_t sum = (int64_t)frames[2 * i] + (int64_t)frames[2 * i + 1];
        frames[i] = (int32_t)(sum >> 1);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* MICPCM_H */
