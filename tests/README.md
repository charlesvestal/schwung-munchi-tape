# Tests

`render.cpp` is an offline harness for the engine: it loads a card, runs a
script of key/knob/menu events in engine time, and writes the output WAV.

```bash
c++ -std=c++17 -O2 -Isrc/engine tests/render.cpp src/engine/munchi_engine.cpp \
    src/engine/wav_io.cpp src/engine/daisysp/*.cpp -o build-host/render
./scripts/fetch-card.sh build/card
./build-host/render build/card tests/scripts/keys_kit.txt out.wav
MUNCHI_WORKER=/tmp/scratch-card ./build-host/render build/card tests/scripts/record_save.txt out.wav
```

Key ids in scripts are CHOMPI `Hardware::SwId` values (src/engine/munchi_engine.h):
15 = C3, 18 = C4, 28 = C5, 33 = Play, 34 = Loop, 31 = Save (A#4).
