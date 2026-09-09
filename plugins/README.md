# Plugin di esempio

I plugin si generano con Tibia e si compilano separatamente dall'host.
Servono compilatore C, make, Node.js, il modulo `dot` e un checkout Tibia
con i target `perone` e `perone-make`. `synth_mono` e `fx_svf` richiedono anche
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
Makefile; quest'ultimo produce il bundle `build/plugin.perone` e segue le dipendenze degli header.
Il JSON è la fonte dei default e dei metadati: non ci sono tabelle manuali parallele.
I preset musicali restano nelle partiture Janet.

I test del synth verificano anche l'indipendenza dai blocchi e il pitch bend
440/220/880/440 Hz. Queste correzioni devono essere presenti in Brickworks:
il wrapper non applica patch DSP né altera i messaggi per compensare bug del synth.

Il target `perone` esporta solo `perone_get_api(version)`. Il contratto è definito
in `tibia/templates/perone/perone.h`; `triccheballacche/perone.h` ne contiene
una copia identica per compilare l'host senza il generatore.
Perone espone il ciclo di vita `plugin_*`: è l'host a inizializzare il DSP,
applicare i default, fornire la memoria DSP e liberare le risorse.

Il bundle generato contiene:

```text
build/plugin.perone/
  product.json
  <architettura>-<sistema>/
    <bundleName>.so
```

Il JSON è esterno e va distribuito insieme al binario. Janet lo legge al caricamento
per interpretare bus, parametri, mapping e scale points. Il C riceve soltanto
configurazione numerica ed eventi; gli indici sono quelli degli array JSON originali.
`daw/info` espone i parametri e `daw/product` restituisce il prodotto completo.
Non servono sorgenti DSP, generatori o header interni durante l'esecuzione.

Passa la directory `.perone` a `build/host` o `daw/plugin`. Puoi copiarla o
rinominarla; il nome del binario al suo interno resta quello di `product.bundleName`.
`PERONE_PLATFORM` seleziona la piattaforma di destinazione sia nel build dei plugin
sia in quello dell'host; per default viene rilevata con `uname`.

L'host supporta sorgenti ed effetti mono/stereo, un ingresso MIDI e sidechain
opzionali scollegate. Le sidechain obbligatorie, i bus CV, i bus principali
aggiuntivi, il transport sincronizzato e la messaggistica non sono supportati.
Le funzionalità Perone disponibili possono essere più ampie di quelle dell'host.

Gli 80 esempi compilati in `../brickworks/build/perone` si possono caricare
direttamente, senza copiarli in questi progetti. `make test-brickworks` nella radice
di triccheballacche verifica tutti i bundle presenti. `examples/brickworks.janet`
mostra una catena con esempi C e C++ originali.
I vecchi `.so` con ABI Tibia/shared o Perone v1 vanno rigenerati e ricompilati.
