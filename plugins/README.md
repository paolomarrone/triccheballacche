# Plugin di esempio

I plugin si generano con Tibia e si compilano separatamente dall'host.
Servono compilatore C, make, Node.js, il modulo `dot` e un checkout Tibia
con i target `shared` e `shared-make`. `synth_mono` e `fx_svf` richiedono anche
Brickworks con l'API corrente (`plugin_init` restituisce `int`).

Per default i repository sono affiancati: `../tibia` e `../brickworks`, rispetto
alla radice di triccheballacche. Nessuna dipendenza DSP viene scaricata o modificata.

```sh
make -C plugins/echo
make -C plugins
make -C plugins TIBIA=/percorso/tibia BRICKWORKS=/percorso/brickworks
make -C plugins clean
```

`TIBIA` indica il generatore, non la copia dell'header nel repository dell'host.
Il modulo `dot` deve essere risolvibile da Node (ad esempio `npm install dot`
nel checkout Tibia). I plugin non richiedono Janet, miniaudio o sorgenti dell'host.

| Progetto | Sorgenti DSP | Parametri |
| --- | --- | --- |
| `synth_mono` | `brickworks/examples/synth_mono/src/plugin.h`, originale | 38 input e meter `level` in output |
| `fx_svf` | `brickworks/examples/fx_svf/src/plugin.h`, originale | Filtro a variabili di stato |
| `drums` | `plugin.h` locale | Gain e seed delle percussioni |
| `shape` | `plugin.h` locale | Waveshaper e filtri |
| `echo` | `plugin.h` locale | Delay a tre tap |
| `tibia_test` | `plugin.h` locale | Gain, filtro, delay, bypass e output |

Ogni progetto fornisce `plugin.h` e `product.json`, direttamente o tramite il
percorso Brickworks. `plugin.mk` genera in `build/gen` l'API, il wrapper e il
Makefile; quest'ultimo produce `build/plugin.so` e segue le dipendenze degli header.
Il JSON è la fonte dei default e dei metadati: non ci sono tabelle manuali parallele.
I preset musicali restano nelle partiture Janet.

I test del synth verificano anche l'indipendenza dai blocchi e il pitch bend
440/220/880/440 Hz. Queste correzioni devono essere presenti in Brickworks:
il wrapper non applica patch DSP né altera i messaggi per compensare bug del synth.

Il target `shared` esporta solo `tibia_get_api(version)`. Il contratto è definito
in `tibia/templates/shared/tibia.h`; `triccheballacche/tibia/tibia.h` ne contiene
una copia identica per compilare l'host senza il generatore.
Il wrapper gestisce istanza, inizializzazione, default, memoria DSP e distruzione.

Il `.so` contiene sia i metadati C essenziali sia il descrittore JSON completo,
compresi mapping e scale points. I parametri input/output e i bus mantengono gli
indici dell'array JSON originale. L'host usa i metadati C durante il caricamento;
il JSON rimane disponibile nell'ABI per strumenti e interfacce future.
L'host accetta un bus di uscita mono/stereo, al massimo un bus di ingresso
mono/stereo e un ingresso MIDI, a 44,1 kHz. Il formato condiviso può descrivere
anche altri layout; sidechain, CV e bus aggiuntivi vengono rifiutati dall'host
prima di creare l'istanza. Transport, messaggistica e stato non sono esposti dalla
prima versione del target; i prodotti che richiedono transport/messaggistica o
MIDI output vengono rifiutati dal generatore.

Il `.so` può essere copiato o rinominato e passato a `build/host` o `daw/plugin`.
Sorgenti, generatori, Makefile e JSON esterni non servono durante il rendering.
I vecchi binari con simboli `tibia_new`/`tibia_init` vanno ricompilati: non c'è
compatibilità con l'ABI sperimentale precedente.
