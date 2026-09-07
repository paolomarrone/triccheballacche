# triccheballacche

Host C minimale per plugin Tibia: un ingresso e un'uscita mono, 44,1 kHz.

```sh
make                 # host, entrambi i plugin e synth Termux
make test            # test DSP e scheduler, senza dispositivo audio
make run             # sequenza synth di 8 secondi; Ctrl-C interrompe
make keys            # synth polifonico da tastiera; q esce
./build/host examples/synth_mono/plugin.so --wav build/demo.wav
./build/host examples/tibia_test/plugin.so --input
```

Servono compilatore C, make, git e curl. Il primo build scarica Brickworks
v1.2.0 da https://github.com/Orastron/brickworks in `.deps/`; riutilizza
`../miniaudio.h`, oppure scarica miniaudio 0.11.25. I build successivi sono offline.
Su Termux: `pkg install clang make git curl`.
Il build corregge due costanti degli shift in `bw_sqrtf` di Brickworks rendendole
unsigned, per evitare overflow indefinito degli interi con segno.

`examples/termux_synth/src/` contiene il synth autonomo e i suoi test: 16 voci,
48 kHz stereo, backend audio automatico e coda atomica delle note. I tasti sono
`a w s e d f t g y h u j k`; ogni pressione genera una nota di durata fissa.
`./build/termux_synth` senza argomenti esegue la demo. `make test` verifica anche
la coda, le voci e il suboscillatore, senza aprire il dispositivo audio.

`--input` elabora l'ingresso audio predefinito in tempo reale: usare cuffie per
evitare il rientro dagli altoparlanti. Senza ingresso, gli effetti ricevono silenzio.
Il percorso del plugin è relativo alla directory corrente. Sono supportati i
plugin mono con l'interfaccia in `tibia/tibia.h`; MIDI è opzionale.

Ogni plugin imposta i propri parametri in `init`. L'host esegue
`new → init → set_sample_rate → mem_req/mem_set → reset → process` e libera tutto
dopo l'arresto audio. I default del synth rispecchiano `product.json`; il JSON
resta descrittivo e non viene letto a runtime. Il pitch bend copre ±1 ottava.
Gli eventi in `main.c` sono ordinati per campione; parametri e MIDI vengono
applicati prima del campione indicato, senza allocazioni o stampe nella callback.

`sj.h` e `trash/` non partecipano alla compilazione. I sorgenti degli esempi
conservano le proprie intestazioni di licenza; Brickworks include la sua `LICENSE`.
