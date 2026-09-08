# triccheballacche

Una base minimale per una DAW scriptabile: partiture in Janet, audio in C.
L'host carica plugin Tibia mono a 44,1 kHz; il renderer scriptabile li mixa in stereo.

```sh
make                 # host, renderer Janet, plugin e synth Termux
make test            # test DSP e scheduler, senza dispositivo audio
make run             # sequenza synth di 8 secondi; Ctrl-C interrompe
make keys            # synth polifonico da tastiera; q esce
make prog            # compone/renderizza 30 secondi di prog rock sintetico
./build/daw examples/hello.janet build/hello.wav
./build/host examples/synth_mono/plugin.so --wav build/demo.wav
./build/host examples/tibia_test/plugin.so --input
```

Servono compilatore C, make, git e curl. Il primo build scarica Brickworks
v1.2.0 da https://github.com/Orastron/brickworks in `.deps/`; riutilizza
`../miniaudio.h`, oppure scarica miniaudio 0.11.25. Scarica e compila anche
[Janet 1.41.2](https://github.com/janet-lang/janet/tree/v1.41.2) in `.deps/janet`,
collegato staticamente: non occorrono Janet o jpm installati. I build successivi
sono offline. Su Termux: `pkg install clang make git curl libandroid-spawn`.
Il build corregge due costanti degli shift in `bw_sqrtf` di Brickworks rendendole
unsigned, per evitare overflow indefinito degli interi con segno.

`examples/termux_synth/src/` contiene il synth autonomo e i suoi test: 16 voci,
48 kHz stereo, backend audio automatico e coda atomica delle note. I tasti sono
`a w s e d f t g y h u j k`; ogni pressione genera una nota di durata fissa.
`./build/termux_synth` senza argomenti esegue la demo. `make test` verifica anche
la coda, le voci e il suboscillatore, senza aprire il dispositivo audio.

`make prog` genera `build/il_polpo_a_sette_gomiti.wav` (stereo, 44,1 kHz, 16 bit).
Il [WAV pronto da ascoltare](examples/prog/il_polpo_a_sette_gomiti.wav) è incluso nel repository.
La composizione in `examples/prog/polpo.janet` usa otto istanze del synth e batteria
procedurale: 7/8 → 9/8 → 5/4 → assolo in 7/8 → 11/8 → cadenza in 5/8.
Basso, due voci distorte, tastiere, assolo e controcanto sono mixati con panorama
stereo, saturazione e delay. Dura esattamente 30 secondi, inclusa la coda finale.
`make test-prog` confronta il risultato byte per byte con il WAV incluso;
compilatori o librerie matematiche diversi possono cambiare gli ultimi bit.

## Janet: cinque operazioni

```janet
(def basso (daw/instrument "examples/synth_mono/plugin.so"
  {:pan -0.2 :params [7 2 26 900]}))
(def beat (/ 60 154))
(daw/note basso 0 (* beat 0.5) 40)
(daw/export 2)
```

Salva la partitura e lancia `./build/daw partitura.janet output.wav`.
Puoi modificarla e rilanciarla senza ricompilare. Tutti i tempi sono in secondi;
tempo, battute, accordi e pattern sono normali funzioni Janet, non un altro DSL.

| Operazione | Argomenti |
| --- | --- |
| `daw/instrument` | percorso plugin, opzioni facoltative; restituisce l'ID traccia |
| `daw/note` | traccia, inizio, durata, nota MIDI 0–127, velocity facoltativa 1–127 (default 100) |
| `daw/param` | traccia, istante, indice parametro, valore |
| `daw/drum` | tipo, istante, intensità 0–1 |
| `daw/export` | durata totale, inclusa la coda; chiude la partitura |

Opzioni strumento: `:pan` da −1 a 1 (default 0), `:gain` da 0 a 4 (default 1),
`:send` al delay da 0 a 1 (default 0), `:color` `:clean`, `:bass` o `:guitar`
(default `:clean`), `:params` coppie `[indice valore ...]` applicate prima del reset.
Indici, unità e intervalli dei parametri dipendono dal plugin; per il synth vedi
`examples/synth_mono/src/product.json`. Il synth ignora la velocity come ampiezza:
usa il parametro 0 per il volume, come fa la funzione `note` del Polpo.

Batteria: `:kick`, `:snare`, `:hat`, `:open-hat`, `:crash`, `:tom-high`, `:tom-low`.
Il mix conserva il master del Polpo: saturazione, filtro DC, fade finale di 0,65 s
e normalizzazione del picco a 0,94. Esporta WAV PCM16 stereo a 44,1 kHz.

Il C copia gli eventi; Janet viene chiuso prima del rendering. A parità di campione
l'ordine è parametri → note-off → note-on; tra parametri vale l'ordine di inserimento.
`daw/export` non scrive subito: un errore successivo nello script impedisce il render.
Gli errori di script e i limiti producono un errore, senza aprire l'output audio.

Per ora: un export offline per esecuzione, massimo 16 tracce, 2048 eventi per traccia
(una nota ne usa due), 8192 colpi e 600 secondi. Il mix sta interamente in RAM
(circa 0,7 MiB/s). Niente trasporto live, registrazione o GUI. Script e plugin devono
essere fidati: Janet non è in sandbox e i parametri devono rispettare i limiti DSP.
`make test` copre anche l'API Janet, i limiti, gli errori e il WAV dell'esempio breve.

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
