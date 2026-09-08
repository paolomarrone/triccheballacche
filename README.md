# triccheballacche

Una base minimale per una DAW scriptabile: partiture in Janet, audio in C.
Plugin Tibia mono a 44,1 kHz, tracce con effetti in serie e mix stereo.

```sh
make
make test
make prog
./build/daw examples/hello.janet build/hello.wav
./build/daw examples/automation.janet build/automation.wav
./build/host examples/synth_mono/plugin.so --wav build/demo.wav
./build/host examples/tibia_test/plugin.so --input
make keys
```

Servono compilatore C, make, git e curl. Su Termux:
`pkg install clang make git curl libandroid-spawn`.
Il build scarica in `.deps/` Brickworks v1.2.0, Janet v1.41.2 e miniaudio 0.11.25
(se non trova `../miniaudio.h`). Janet è collegato staticamente: non occorre installare
Janet o jpm. I build successivi sono offline. Due costanti di `bw_sqrtf` vengono
rese unsigned per evitare shift indefiniti nella dipendenza Brickworks.

## API: plugin e tracce sono distinti

```janet
(def synth (daw/plugin "examples/synth_mono/plugin.so" {:vcf_cutoff 900}))
(def filter (daw/plugin "examples/tibia_test/plugin.so" {:cutoff 2000}))
(def track (daw/track synth {:effects [filter] :gain 0.5 :pan -0.2}))

(daw/note synth 0 4 60)
(daw/param synth 1 :vcf_cutoff 6000)
(daw/param filter 2 :cutoff 500)
(daw/param track 3 :gain 0.2)
(daw/end 5)
```

Salva la partitura e lancia `./build/daw partitura.janet output.wav`.
Non serve ricompilare. Tutti i tempi sono secondi assoluti, arrotondati al campione.
Tempo, battute, accordi, rampe e pattern sono funzioni della libreria Janet `lib/music.janet`.

| Operazione | Significato |
| --- | --- |
| `daw/plugin path &opt params` | Crea un'istanza e restituisce il suo handle. Parametri iniziali in una tabella. |
| `daw/track source &opt options` | Collega un generatore e gli effetti; restituisce l'handle del mixer della traccia. |
| `daw/master &opt options` | Configura il master opzionale, una volta sola; restituisce il suo handle. |
| `daw/note plugin start duration pitch &opt velocity` | Nota MIDI 0–127, velocity 1–127 (default 100). |
| `daw/param node time parameter value` | Automatizza un parametro del plugin, della traccia o del master. |
| `daw/info node` | Elenca nome, unità, minimo, massimo, default e vincolo intero dei parametri. |
| `daw/end seconds &opt options` | Chiude la partitura e imposta durata e formato. La CLI esporta dopo il successo dello script. |

Opzioni traccia: `:gain` 0–4 (default 1), `:pan` −1–1 (default 0),
`:effects [fx1 fx2 ...]` nell'ordine di elaborazione. Il master accetta `:gain` e
`:effects`; gli effetti master hanno due istanze indipendenti, una per canale.
Senza `daw/master` il master è semplicemente unitario.

Ogni plugin appartiene a una sola catena. Crea un'altra istanza se ti serve altrove.
Un plugin non collegato è un errore a `daw/end`, non una traccia scartata in silenzio.
Non ci sono ancora bus, mandate, sidechain o collegamenti arbitrari.

I parametri accettano keyword descrittive o indici numerici. Il C verifica nomi,
range e valori interi prima di inviarli al DSP; le opzioni sconosciute sono errori.
`(pp (daw/info synth))` permette di scoprire i controlli.
Per il synth `:volume` è il volume; la velocity MIDI non ne controlla l'ampiezza.
Gain e pan della traccia sono invece indipendenti dallo strumento e automatizzabili.

A parità di campione: parametri → note-off → note-on; tra controlli vale l'ordine
di inserimento. Le curve Janet generano eventi discreti: nessuna interpolazione
implicita del mixer, e lo smussamento dei parametri DSP dipende dal plugin.
`examples/automation.janet` mostra un minuto di automazione su effetto e panorama.

## Libreria musicale Janet

`lib/music.janet` contiene solo calcoli musicali e funzioni sincrone: nessun plugin,
stato di sessione, tempo globale o riferimento a `daw/*`. La partitura sceglie gli
strumenti e passa alle sequenze e alle curve una funzione che riceve tempo e valore.
Il C continua a ricevere soltanto note e parametri con tempi in secondi.

Da una partitura nella radice del repository:

```janet
(import ./lib/music)

(def bpm 154)
(def step (music/seconds bpm 0.5)) # Un ottavo; BPM sempre riferiti ai quarti.
(def bass (daw/plugin "examples/synth_mono/plugin.so" {:vcf_cutoff 900}))
(daw/track bass)
(def phrase [40 40 47 nil 40 50 44]) # nil occupa un passo senza emettere una nota.
(music/sequence 0 step phrase
  (fn [t pitch] (daw/note bass t (* step 0.8) pitch)))
(music/curve 0 1.8 90 |(music/lerp 900 3000 $)
  (fn [t cutoff] (daw/param bass t :vcf_cutoff cutoff)))
(daw/end 2)
```

Gli import relativi sono risolti dalla directory della partitura: negli esempi
si usa `../lib/music`, nel brano prog `../../lib/music`. I percorsi dei plugin
restano relativi alla directory da cui si esegue l'host.

| Funzione | Risultato o comportamento |
| --- | --- |
| `music/seconds bpm beats` | Converte quarti in secondi; accetta frazioni e offset negativi. |
| `music/bars bpm count &opt numerator denominator` | Durata di `count` battute in secondi, metro 4/4 di default. Due battute di 7/8 a 120 BPM durano 3,5 secondi. |
| `music/degree root intervals n` | Grado di scala a partire da zero; ripete gli intervalli ogni ottava, anche per gradi negativi. |
| `music/chord root intervals` | Array di altezze MIDI, preservando ordine e disposizione degli intervalli. |
| `music/sequence start step values emit` | Chiama `emit(time, value)` per ogni valore diverso da `nil`; restituisce `start + step * length(values)`. |
| `music/lerp a b x` | Interpolazione lineare: `a` a zero, `b` a uno; nessun limite implicito a `x`. |
| `music/curve start duration steps shape emit` | Genera `steps + 1` eventi, estremi inclusi; chiama `emit(time, shape(x))` con `x` da zero a uno. Restituisce il tempo finale. |

Una sequenza può contenere note, nomi di percussioni o accordi: il significato del
valore appartiene alla funzione passata dalla partitura. Il tempo restituito permette
di concatenare sequenze e comprende anche le pause finali. La durata delle note è
scelta dalla partitura e può superare la durata del passo.

`music/chord` calcola altezze; non crea voci. Per gli accordi con `synth_mono` si usano
istanze distinte, come in `examples/prog/polpo.janet`. Per esempio,
`(music/chord 60 [0 3 7])` dà `@[60 63 67]`, mentre
`(music/degree 60 [0 2 3 5 7 8 11] -1)` dà `59`.

Le curve emettono controlli discreti. `steps` è un intero positivo, `duration` è in
secondi; la forma può essere una normale funzione, per esempio `|(* $ $)`.
L'ultimo controllo è a `start + duration`: deve precedere `daw/end`, perché il
campione di fine export è escluso. Per cambiare BPM tra sezioni si passa un nuovo
valore alle conversioni e si sommano esplicitamente le durate.

## Rendering neutro, elaborazione esplicita

```text
sorgente → effetti mono → gain/pan → somma stereo → effetti master → gain master → WAV
```

Il default è WAV float32 stereo: niente saturazione, filtro DC, fade, normalizzazione
o clipping automatico. I valori oltre ±1 vengono conservati nel file; attenzione
al livello quando lo si riproduce.

Opzioni di `daw/end`:

```janet
(daw/end 30 {:format :pcm16 :normalize 0.94})
```

`:format` è `:float` (default) o `:pcm16`; PCM16 limita i campioni a ±1.
`:normalize` imposta il picco, da 0 a 1; 0 (default) la disabilita.
La normalizzazione usa un file temporaneo, non un buffer dell'intero brano.

Plugin inclusi oltre al synth e all'effetto di test:

- `examples/shape/plugin.so`: waveshaper, drive/level e filtri DC/lowpass a coefficienti espliciti.
- `examples/echo/plugin.so`: tre tap regolabili in millisecondi, livelli indipendenti e segnale dry.
- `examples/drums/plugin.so`: 32 voci; note MIDI 0–6 = kick, snare, hat, open-hat, crash, tom-high, tom-low.
  La velocity regola il singolo colpo; `:gain` regola l'intera istanza e `:seed` i colpi successivi.
  I suoni decadono naturalmente e ignorano il note-off; a voci esaurite viene sostituita la più vecchia.

`examples/prog/polpo.janet` sceglie esplicitamente strumenti, effetti, master e fade.
`make prog` produce il nuovo WAV di 30 secondi in `build/il_polpo_a_sette_gomiti.wav`.
Il [WAV storico](examples/prog/il_polpo_a_sette_gomiti.wav) resta incluso e invariato.
Il nuovo render non è bit-identico: percussioni e delay ora usano istanze e catene
indipendenti. `make test-prog` verifica che due nuovi render siano identici.

## Struttura e limiti

`engine.c/h` gestisce istanze e scheduler, senza CLI né Janet.
`session.c/h` contiene lo stato esplicito della sessione, le catene e il mixer.
`daw.c` collega Janet e l'export; `main.c` è il piccolo host audio autonomo.
`lib/music.janet` fornisce le funzioni musicali; le partiture in `examples/` scelgono
arrangiamento, strumenti ed effetti.

Gli eventi crescono durante la preparazione e vengono ordinati una volta sola.
Janet viene chiuso prima del rendering; il motore non alloca memoria mentre processa
i blocchi. Il mix occupa memoria indipendente dalla durata, più lo stato dei plugin
e gli eventi. Limiti attuali: un export per esecuzione, 32 tracce, 128 nodi totali
(plugin e mixer), 8 effetti per catena, 64 parametri per plugin, 3600 secondi.
Mono e 44,1 kHz sono un contratto esplicito, non formati negoziati.

Lo scripting richiede `tibia_get_info`, una piccola estensione locale descritta in
`tibia/tibia.h`. I plugin forniscono tipo sorgente/effetto, capacità MIDI e metadati
dei parametri; i default del synth sono condivisi con la sua tabella dei parametri.
L'host autonomo continua ad accettare i plugin mono precedenti senza metadati.
Script e plugin devono essere fidati: nessuna sandbox o isolamento dei crash nativi.
Le chiamate `daw/*` costruiscono la sessione in modo sincrono; thread e task asincroni
che modificano la sessione non sono supportati.

`make test` copre scheduler, DSP, API, errori, catene, neutralità, automazione del
mixer/master, indipendenza stereo, crescita degli eventi e formati WAV.
Include `music_test.janet`, che prova le funzioni musicali senza dipendere da `daw/*`,
e una curva della libreria collegata al synth tramite l'API reale.
La vecchia API sperimentale cambia: `instrument` diventa `plugin` + `track`,
`export` diventa `end`; `color`, `send` e `drum` non sono più casi speciali dell'host.

`make run` suona la demo di otto secondi; `--input` elabora il microfono (usare cuffie).
`make keys` avvia il synth autonomo a 16 voci, 48 kHz stereo:
`a w s e d f t g y h u j k`, `q` per uscire. Questi programmi non richiedono il renderer Janet.
`sj.h` e `trash/` non partecipano al build. I sorgenti e le dipendenze conservano le rispettive licenze.
