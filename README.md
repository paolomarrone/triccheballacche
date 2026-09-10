# triccheballacche

Una base minimale per una DAW scriptabile: partiture in Janet, audio in C.
Plugin Perone mono e stereo a 44,1 kHz, tracce con effetti in serie e mix stereo.

```sh
make                    # Compila solo host e renderer Janet.
make test               # Test del nucleo con fixture locali.
make -C plugins         # Compilazione separata dei plugin di esempio.
make test-plugins       # Integrazione con i sei plugin già compilati.
make prog
./build/daw examples/hello.janet build/hello.wav
./build/daw examples/automation.janet build/automation.wav
./build/host plugins/synth_mono/build/plugin.perone --wav build/demo.wav
./build/host plugins/tibia_test/build/plugin.perone --input
make keys
```

Servono compilatore C, make, git e curl. Su Termux:
`pkg install clang make git curl libandroid-spawn`.
Il build dell'host scarica in `.deps/` Janet v1.41.2, miniaudio 0.11.25
(se non trova `../miniaudio.h`) e il solo [modulo JSON di Spork](https://github.com/janet-lang/spork/blob/0667f96b74de52747ffe5e19e185563ebf53816b/src/json.c),
a una revisione fissata nel Makefile. Janet e JSON sono collegati staticamente;
l'API Janet della DAW e il lettore Perone sono incorporati nei binari. Non occorre installare
Janet, Spork o jpm. I build successivi sono offline.

## Build separati: host e plugin

Il Makefile principale produce `build/host` e `build/daw`. `build/host` e
`daw/plugin` ricevono il percorso di una directory `.perone` già compilata:

```text
nome.perone/
  product.json
  x86_64-linux/
    <bundleName>.so
```

Janet legge il JSON, il loader C carica soltanto la libreria della piattaforma
corrente tramite `perone_get_api(PERONE_ABI_VERSION)`. Il nome del binario viene da
`product.bundleName`; la directory del bundle può essere rinominata o spostata.
`PERONE_PLATFORM` nel Makefile permette di selezionare la piattaforma di destinazione
per la compilazione incrociata, come in Tibia. Il default è `uname -m` più
`uname -s` in minuscolo, per esempio `x86_64-linux` o `aarch64-linux`.

I progetti di esempio in `plugins/<nome>/` hanno build separati:
`make -C plugins/synth_mono` produce `plugins/synth_mono/build/plugin.perone`.
`make -C plugins` compila tutta la raccolta. Servono Node.js, `dot` e `../tibia`
con i target `perone`/`perone-make`. Synth e filtro `fx_svf` usano gli esempi
originali di `../brickworks`; i percorsi sono configurabili con `TIBIA` e
`BRICKWORKS`. Il [README dei plugin](plugins/README.md) descrive il build.
Sorgenti DSP e generatore non servono per caricare un bundle già compilato.
Le vecchie partiture devono sostituire i percorsi `build/plugin.so` con
`build/plugin.perone` dopo aver ricompilato i plugin.

Gli esempi già compilati in Brickworks sono utilizzabili direttamente:

```sh
./build/host ../brickworks/build/perone/synthpp_mono/build/bw_example_synthpp_mono.perone --wav build/synth.wav
./build/daw examples/brickworks.janet build/brickworks.wav
./build/daw examples/rame.janet build/rame.wav
./build/daw examples/patterns.janet build/patterns.wav
make test-brickworks    # Carica e processa tutti i bundle presenti, senza ricompilarli.
```

`BRICKWORKS_PERONE` configura il percorso della raccolta per `make test-brickworks`;
gli esempi Janet leggono la variabile d'ambiente omonima. Il percorso
predefinito è `../brickworks/build/perone`. `brickworks.janet` dimostra synth
polifonico, compressore, pan mono→stereo e riverbero, usando anche gli esempi C++.

[Rame](examples/rame.janet) è un pezzo electro di circa 57 secondi: 24 battute a
112 BPM, con intro, groove, pausa centrale e ripresa. Le sette parti usano solo
bundle Brickworks: accordi, basso, arpeggio, melodia e tre synth per la batteria.
La partitura automatizza filtri, risonanza, pulse width, distorsione, chorus,
pan e riverberi; la cassa ha una discesa d'intonazione a ogni colpo. Esporta un
WAV stereo PCM16 normalizzato a 0,94, con sei secondi per la coda finale.
`opening` e `piece` costruiscono l'arrangiamento come pattern immutabili; ogni
sezione unisce note e automazioni con tempi relativi. Le durate delle sezioni
determinano l'inizio delle successive e la posizione dell'accordo finale.
[patterns.janet](examples/patterns.janet) mostra in pochi passaggi ripetizione,
inversione, trasposizione e dilatazione dello stesso motivo, con una curva del filtro.

`make test` compila le fixture Perone locali (sorgente stereo MIDI ed effetto mono)
e verifica il nucleo senza richiedere i progetti in `plugins/`, Tibia o Brickworks.
`make test-plugins` e `make prog` richiedono i bundle di esempio già presenti e
segnalano come compilarli se mancano. Il build dell'host e i test non compilano
plugin di produzione.
`make clean` pulisce i programmi dell'host; `make -C plugins clean` pulisce i plugin.
`make keys` compila e avvia il synth da terminale autonomo in `examples/termux_synth/`.

```text
*.c, *.h           host, sessione e loader
perone.h           copia dell’ABI Perone definita in Tibia
plugins/<nome>/    sorgenti, metadati e build autonomo del plugin
lib/               metadati Perone, API della DAW e funzioni musicali Janet
test/              test C e Janet, fixture del plugin Perone
examples/          partiture e demo da terminale
build/             binari dell'host e render
```

Il codice C usa tab da 4 colonne, Janet due spazi. `.editorconfig` e `.clang-format`
fissano spaziature e indentazione; le funzioni sono separate da una riga vuota.
`make format` uniforma i sorgenti C locali, `make format-check` ne verifica lo stile
con clang-format 21. Dipendenze, file generati e la copia upstream di `perone.h`
sono esclusi. Il formatter serve solo per questi due comandi.

## API: plugin e tracce sono distinti

```janet
(def synth (daw/plugin "plugins/synth_mono/build/plugin.perone" {:vcf_cutoff 900}))
(def filter (daw/plugin "plugins/tibia_test/build/plugin.perone" {:cutoff 2000}))
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
| `daw/info node` | Descrizioni immutabili dei parametri, inclusi mapping ed etichette dei valori enumerati. |
| `daw/product node` | Metadati completi del prodotto letti dal JSON; `nil` per i mixer. |
| `daw/schedule start bpm pattern` | Emette un pattern in quarti a partire da `start` secondi e restituisce la fine nominale in secondi. |
| `daw/end seconds &opt options` | Chiude la partitura e imposta durata e formato. La CLI esporta dopo il successo dello script. |

Opzioni traccia: `:gain` 0–4 (default 1), `:pan` −1–1 (default 0),
`:effects [fx1 fx2 ...]` nell'ordine di elaborazione. Il master accetta `:gain` e
`:effects`. Un effetto stereo usa una sola istanza che elabora entrambi i canali;
un effetto mono 1→1 su un segnale stereo usa due istanze indipendenti, anche sulle tracce.
Senza `daw/master` il master è semplicemente unitario.

Le catene seguono i canali dichiarati dai plugin. Un segnale mono viene duplicato
su L/R, alla stessa ampiezza, quando entra in un effetto stereo 2→2. Un plugin
1→2 può generare stereo da una sorgente mono. Una conversione stereo→mono richiede
un plugin con ingresso stereo e uscita mono; un effetto 1→2 non può ricevere
direttamente un segnale stereo. Il master produce sempre due canali e duplica
l'eventuale uscita mono della sua catena.

`:pan` conserva il panning a potenza costante per le catene che terminano in mono
(centro: circa −3 dB per canale). Per le catene stereo regola il bilanciamento
lineare: centro unitario su entrambi i canali, estremi con il canale opposto muto.
Non incrocia né somma i canali stereo. Gain e pan seguono gli effetti della traccia.

Ogni plugin appartiene a una sola catena. Crea un'altra istanza se ti serve altrove.
Un plugin non collegato è un errore a `daw/end`, non una traccia scartata in silenzio.
Non ci sono ancora bus, mandate, sidechain o collegamenti arbitrari.

I parametri accettano keyword descrittive o indici numerici. Janet verifica nomi,
range e valori interi prima di inviarli al C; le opzioni sconosciute sono errori.
`(pp (daw/info synth))` permette di scoprire i controlli.
Per il synth `:volume` è il volume; la velocity MIDI non ne controlla l'ampiezza.
Gain e pan della traccia sono invece indipendenti dallo strumento e automatizzabili.

A parità di campione: parametri → note-off → note-on; tra controlli vale l'ordine
di inserimento. Le curve Janet generano eventi discreti: nessuna interpolazione
implicita del mixer, e lo smussamento dei parametri DSP dipende dal plugin.
`examples/automation.janet` mostra un minuto di automazione su effetto e panorama.

## Tempo, pitch e pattern Janet

`lib/music.janet` contiene cinque funzioni matematiche, senza stato di sessione:

| Funzione | Risultato |
| --- | --- |
| `music/seconds bpm beats` | Converte quarti in secondi; accetta frazioni e offset negativi. |
| `music/bars bpm count &opt numerator denominator` | Durata di `count` battute in secondi, metro 4/4 di default. Due battute di 7/8 a 120 BPM durano 3,5 secondi. |
| `music/degree root intervals n` | Grado di scala a partire da zero, ripetendo gli intervalli ogni ottava anche per gradi negativi. |
| `music/chord root intervals` | Altezze MIDI nell'ordine dato. Non alloca voci o plugin. |
| `music/lerp a b x` | Interpolazione lineare; `x` non viene limitato a 0–1. |

`lib/pattern.janet` costruisce e trasforma dati musicali finiti. Un pattern è una
struttura con durata nominale `:length` e una sequenza di eventi `[inizio fine valore]`:

```janet
{:length 4
 :events [[0 0.75 60] [1 1.5 64] [3 3.5 67]]}
```

Tutti i tempi del pattern sono **quarti relativi**, senza BPM. Qui la pausa finale
fino al quarto beat appartiene alla frase. Le note possono proseguire oltre la
lunghezza dichiarata; sono ammessi anche anticipi negativi. Un evento con inizio e
fine uguali rappresenta un punto, utile per i controlli. La durata nominale può
essere zero, anche con eventi; una pausa è `(p/events durata [])`.

Le funzioni restituiscono nuove strutture senza chiamare `daw/*`. `events` copia e
congela ricorsivamente sequenze, dizionari e buffer, compresi i valori degli eventi.
Il valore resta generico: numero, accordo, comando o altro dato Janet. Per valori
opachi come closure e abstract valgono i limiti di `freeze` di Janet: il loro stato
interno non viene congelato. L'ordine di inserimento degli eventi viene conservato.

| Operazione | Significato |
| --- | --- |
| `p/events length items` | Valida e congela gli eventi. Tempi finiti, `length >= 0`, `inizio <= fine`. |
| `p/steps step values` | Intervalli contigui di `step` quarti; `nil` occupa un passo di pausa. Durata totale `step * length(values)`. |
| `p/curve length steps shape` | `steps + 1` punti `[t t shape(x)]` per `x=0..1`, estremi inclusi. Lunghezza positiva. |
| `p/serial patterns` | Somma le durate e sposta ogni pattern dopo il precedente, conservando anticipi e prolungamenti. |
| `p/parallel patterns` | Sovrappone a zero e usa la durata maggiore, nell'ordine della lista. |
| `p/map f pattern` | Trasforma soltanto i valori. Un valore `nil` restituito da `f` resta un evento. |
| `p/stretch factor pattern` | Moltiplica tempi e durata per un fattore positivo. `0.5` dimezza la durata. |
| `p/reverse pattern` | Inverte `[a b]` in `[length-b length-a]`, mantenendo valori e ordine di inserimento. |

`serial` e `parallel` su una lista vuota restituiscono un pattern vuoto di durata
zero. Non tagliano gli eventi ai confini della frase. `reverse` può trasformare un
prolungamento in un anticipo, e porta un controllo a tempo zero sulla fine della
frase. Trasforma gli intervalli e i punti musicali, senza invertire audio o stato DSP.
Le curve sono controlli discreti: la risoluzione è esplicita e lo smussamento dipende
dal plugin. Gli estremi coincidenti seguono l'ordine della composizione.

Da una partitura nella radice del repository, con `synth` già collegato a una traccia:

```janet
(import ./lib/music)
(import ./lib/pattern :as p)

(def motif (p/steps 0.5 [60 nil 64 67]))
(def theme (p/serial [motif (p/reverse motif) (p/map |(+ $ 12) motif)]))
(def cutoff (p/curve (theme :length) 96 |(music/lerp 400 4000 $)))
(def score
  (p/parallel [(p/map |[:note synth $ 100] theme)
               (p/map |[:param synth :vcf_cutoff $] cutoff)]))
(def end (daw/schedule 0 112 score))
(daw/end (+ end 2))
```

`daw/schedule start bpm pattern` converte i quarti in secondi assoluti ed emette
immediatamente gli eventi. Accetta due comandi, entrambi con quattro elementi:

- `[:note node pitch velocity]`: l'intervallo deve avere durata positiva. Stessi
  limiti di `daw/note`, compresa la velocity MIDI 1–127.
- `[:param node parameter value]`: l'evento deve essere un punto. Nomi, range e
  valori interi sono verificati attraverso gli stessi metadati di `daw/param`.

Una nota deve durare almeno un campione dopo la conversione. Inizio, fine nominale
ed eventi devono rientrare nel limite dell'host di 3600 secondi; gli anticipi devono
quindi essere posizionati abbastanza avanti. Il valore restituito è la fine nominale,
che può precedere un note-off: la partitura sceglie `daw/end` includendo note e code.
Un controllo alla fine nominale richiede un export più lungo di almeno un campione.
Le operazioni non ripristinano parametri né allocano copie dei plugin; ripetere un
pattern sullo stesso strumento ne continua lo stato DSP. Gli errori interrompono lo
script; se catturati, gli eventi già emessi prima dell'errore restano nella sessione.

Il C mantiene parametri → note-off → note-on a parità di campione e, tra controlli,
l'ordine di inserimento. L'ultimo valore per lo stesso parametro prevale. I tempi
sono numeri Janet e vengono arrotondati al campione solo dall'host: cambiare l'ordine
di calcolo dei tempi può spostare di un campione un evento esattamente tra due campioni.

Gli import sono relativi alla partitura: `../lib/pattern` negli esempi e
`../../lib/pattern` nel brano prog. I percorsi dei plugin restano relativi alla
directory da cui si esegue l'host. L'esempio prog incorpora i cambi di tempo nelle
posizioni degli eventi e programma il risultato a 60 BPM, un quarto per secondo.

I vecchi `music/sequence`, `music/curve`, `music/serial` e `music/parallel` a callback
sono stati sostituiti dai pattern. Le funzioni che prima emettevano eventi e
restituivano una fine ora costruiscono e restituiscono un pattern. La programmazione
nella sessione avviene con `daw/schedule`; `daw/note` e `daw/param` restano disponibili
come API a basso livello. Non c'è ripetizione infinita o allineamento implicito dei
valori tra pattern con ritmi diversi: ripetizione e alternanza si costruiscono con
le normali funzioni Janet e `p/serial`.

## Rendering neutro, elaborazione esplicita

```text
sorgente mono/stereo → effetti → gain/pan → somma stereo → effetti master → gain master → WAV
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

L'export della DAW scrive un WAV temporaneo nella directory di destinazione e
sostituisce il file richiesto soltanto dopo la chiusura riuscita. Se il rendering
o la scrittura falliscono, il WAV precedente resta intatto; se non esisteva,
non viene pubblicato un file incompleto.

Plugin inclusi oltre al synth e all'effetto di test:

- `plugins/shape/build/plugin.perone`: waveshaper, drive/level e filtri DC/lowpass a coefficienti espliciti.
- `plugins/echo/build/plugin.perone`: tre tap regolabili in millisecondi, livelli indipendenti e segnale dry.
- `plugins/drums/build/plugin.perone`: 32 voci; note MIDI 0–6 = kick, snare, hat, open-hat, crash, tom-high, tom-low.
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
`daw.c` collega le chiamate native della sessione e l'export; `main.c` è la demo audio.
`script.c` prepara Janet e trasferisce configurazioni numeriche al motore C.
`lib/perone.janet` legge i bundle, interpreta bus, default e parametri;
`lib/daw.janet` espone l'API delle partiture e conserva i metadati completi.
`lib/music.janet` fornisce le funzioni musicali; le partiture in `examples/` scelgono
arrangiamento, strumenti ed effetti.

Il layout passato da Janet al C usa campi nominati per canali principali, bus MIDI
e posizioni degli ingressi. `PluginConfig` conserva i valori iniziali del plugin,
inclusi gli override, separati dai default originali esposti da `daw/info`.
La sessione usa quella configurazione per preparare entrambe le istanze degli
effetti mono su stereo; i mixer conservano soltanto i propri valori di gain e pan.
`daw/end` chiude la preparazione: da quel momento i parametri cambiano attraverso
gli eventi già programmati.

Gli eventi crescono durante la preparazione e vengono ordinati una volta sola.
Janet viene chiuso prima del rendering; il motore non alloca memoria mentre processa
i blocchi. Il mix occupa memoria indipendente dalla durata, più lo stato dei plugin
e gli eventi. Limiti attuali: un export per esecuzione, 32 tracce, 128 nodi totali
(plugin e mixer), 8 effetti per catena, 64 parametri per plugin, 3600 secondi.
L'host accetta un bus audio principale di uscita mono/stereo, al massimo un bus
principale di ingresso mono/stereo e un ingresso MIDI, a 44,1 kHz. Le sidechain
opzionali rimangono scollegate: il DSP riceve `NULL` nelle loro posizioni originali.
Sono supportati fino a 8 canali di ingresso complessivi, inclusi quelli scollegati.
CV, sidechain obbligatorie e bus principali aggiuntivi vengono rifiutati prima
che il DSP venga caricato. Transport sincronizzato e messaggistica non sono ancora
supportati dall'host e vengono rifiutati se richiesti nel JSON. Il salvataggio dello
stato personalizzato non è esposto dall'API delle partiture.

Il contratto è Perone ABI v2, copiato senza modifiche in `perone.h`.
L'host gestisce `alloc/init`, applica i valori iniziali, imposta il sample rate,
fornisce la memoria richiesta da `mem_req/mem_set` e chiama `reset`.
Alla chiusura chiama `fini`, libera la memoria DSP e poi l'istanza con `free`.
Il motore riceve soltanto layout, default numerici e indici dei parametri di uscita;
non contiene descrittori testuali e non legge JSON.

`daw/info` include input e output nell'ordine originale, con `:index`, `:name`
(ID simbolico), `:label`, `:direction`, `:map` e `:scale-points`, oltre a unità,
range e default. `daw/product` conserva anche tutti gli altri campi del prodotto.
I parametri di uscita sono descritti ma non possono essere impostati o automatizzati.
Le API precedenti, incluso `tibia_get_api`, non sono accettate.
Script e plugin devono essere fidati: nessuna sandbox o isolamento dei crash nativi.
Le chiamate `daw/*` costruiscono la sessione in modo sincrono; thread e task asincroni
che modificano la sessione non sono supportati.

`make test` copre scheduler, ciclo di vita Perone, API, errori, catene, neutralità, automazione del
mixer/master, separazione dei canali, effetti mono su stereo, effetti stereo
con interazione L/R, conversioni dei canali, crescita degli eventi e formati WAV.
Include `test/music.janet`, che prova le funzioni musicali senza dipendere da `daw/*`,
e una curva della libreria collegata alla fixture tramite l'API reale. Verifica
anche la conservazione del WAV precedente e la pulizia dei temporanei dopo errori
di rendering, scrittura, finalizzazione e sostituzione del file.
`make test-plugins` conserva le regressioni dei DSP reali: synth e pitch bend,
inviluppo indipendente dai blocchi, filtro, delay, percussioni e waveshaper,
oltre ai metadati Brickworks usati da una partitura Janet.
La vecchia API sperimentale cambia: `instrument` diventa `plugin` + `track`,
`export` diventa `end`; `color`, `send` e `drum` non sono più casi speciali dell'host.

`make run` suona la demo di otto secondi usando il synth già compilato con
`make -C plugins/synth_mono`; `--input` elabora il microfono (usare cuffie).
L'host autonomo usa i canali di ingresso/uscita del plugin e salva WAV mono o stereo
secondo la sua uscita; il renderer Janet produce sempre WAV stereo.
`make keys` avvia il synth autonomo a 16 voci, 48 kHz stereo:
`a w s e d f t g y h u j k`, `q` per uscire. La demo audio usa Janet soltanto per
leggere il bundle; il synth da terminale non dipende da Janet.
I sorgenti e le dipendenze conservano le rispettive licenze.
