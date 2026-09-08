# Plugin di esempio

Questi progetti si compilano separatamente da triccheballacche. Ogni directory
contiene il DSP e un Makefile; il risultato locale è `build/plugin.so`.

```sh
make -C plugins/echo       # Un solo plugin, dalla radice del repository.
make -C plugins           # Tutti i plugin, con un comando esplicito.
make -C plugins clean     # Pulisce solo i binari dei plugin.
```

`plugin.mk` condivide le poche regole di compilazione. I progetti richiedono un
compilatore C, make e l'header `tibia.h`; non richiedono Janet, miniaudio o i
sorgenti dell'host. `TIBIA` indica la directory dell'header, di default `../../tibia`.
La raccolta `plugins/` può essere copiata altrove e compilata con
`make -C /percorso/plugins TIBIA=/percorso/tibia`.

| Progetto | Ruolo | Dipendenze DSP |
| --- | --- | --- |
| `synth_mono` | Synth monofonico, 38 parametri | Brickworks v1.2.0 |
| `drums` | Percussioni sintetiche, 32 voci | Nessuna |
| `shape` | Waveshaper e filtri | Nessuna |
| `echo` | Delay a tre tap | Nessuna |
| `tibia_test` | Gain, filtro, delay e bypass | Nessuna |

Il synth usa git per scaricare Brickworks in `synth_mono/.deps/brickworks` al primo build;
`BRICKWORKS` permette di usare un checkout esistente. Due costanti di `bw_sqrtf`
vengono rese unsigned per evitare shift indefiniti nella dipendenza.

L'host carica soltanto il `.so` e ne legge i metadati tramite `tibia_get_info`:
tipo sorgente/effetto, ingresso MIDI e descrizioni dei parametri. Il contratto
comune è in `tibia/tibia.h`, nella radice del repository. I plugin di questa
raccolta usano porte audio mono; l'host li esegue a 44,1 kHz.

`synth_mono/product.json` conserva il descrittore del progetto originale.
Il build attuale usa `parameters.h` per metadati e default: il JSON non viene
letto né genera automaticamente l'header. Gli altri plugin dichiarano i
metadati direttamente in `plugin.c`. I preset sono tabelle nelle partiture Janet.

Il `.so` compilato può essere copiato o rinominato e caricato passando il suo
percorso a `build/host` o `daw/plugin`. I sorgenti, i Makefile e il JSON non
servono durante il rendering. I sorgenti conservano le rispettive licenze.
