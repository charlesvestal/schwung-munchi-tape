/* wav_io.h -- WAV read/write for the card worker (never the audio thread).
 *
 * TAPE's card format is 48 kHz / 16-bit / stereo with a 44-byte header, and
 * the firmware refused anything else (it seeked raw from byte 44). Munchi is
 * more forgiving about what a user drops on the card -- 8/16/24/32-bit PCM or
 * 32-bit float, mono or stereo, any rate, any chunk layout -- and converts it
 * on load to what the engine plays: int16 stereo interleaved at 48 kHz.
 * Everything it WRITES is TAPE's exact format, so a Munchi card is a CHOMPI
 * card.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace munchi
{

/** Loads `path`. On success *out is malloc'd int16 stereo interleaved at
 *  48 kHz and *frames its length. Returns 0, or -1 with `err` filled. */
int wav_load_48k_stereo(const char *path, int16_t **out, size_t *frames,
                        char *err, size_t err_len);

/** Writes int16 stereo interleaved 48 kHz as a canonical 44-byte-header WAV,
 *  via a temp file and rename so a reader never sees a half-written slot. */
int wav_write_48k_stereo(const char *path, const int16_t *pcm, size_t frames);

} // namespace munchi
