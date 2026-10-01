# Third-party work in Munchi Tape

Munchi Tape is a port of **CHOMPI TAPE 2.0** to Ableton Move. Everything it is built
from is MIT-licensed; the notices below travel with every copy.

| Component | Copyright | License | Where |
|---|---|---|---|
| CHOMPI TAPE 2.0 firmware (DSP engine, voice, looper, UI logic) | © CHOMPI Club | MIT | `src/engine/tape/` (originals of the two rewritten files in `src/engine/tape/upstream/`) |
| CHOMPI TAPE 2.0 factory card (84 samples, presets.json, options.json) | © CHOMPI Club | MIT | fetched at build time into `card/` from [CHOMPI-Club/CHOMPI](https://github.com/CHOMPI-Club/CHOMPI) @ `a73d7326` |
| DaisySP (ADSR, SVF, DC block, delay line, dsp utilities) | © Electrosmith, Corp. | MIT | `src/engine/daisysp/` (its `LICENSE` beside it) |
| `reverb.h`, `fx_engine.h`, `limiter.h` | © Émilie Gillet (Mutable Instruments) | MIT | `src/engine/tape/` — original notices kept in the files |
| 5x7 font | Schwung standalone example | MIT | `src/standalone/font.cpp` |

Authorship of the firmware, per the CHOMPI repository's own `THIRD_PARTY.md`
(copied to `docs/CHOMPI-THIRD_PARTY.md`): Electrosmith engineered the original
platform and the TAPE firmware through 1.0.9; TAPE 2.0 was written at Chase
Bliss after CHOMPI Club became part of Chase Bliss. CHOMPI Club releases it
under the MIT license.

## Trademarks

The CHOMPI name, logo, character and related marks are trademarks of CHOMPI
Club and are **not** covered by the MIT license (`docs/CHOMPI-TRADEMARKS.md`).
Munchi Tape is an independent port, not an official CHOMPI Club release; it is
named differently for that reason and uses no CHOMPI artwork.
