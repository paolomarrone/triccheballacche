# triccheballacche

Una base minimale per una DAW scriptabile: partiture in Janet, audio in C.
Plugin Perone mono e stereo, tracce con effetti in serie e mix stereo.
44,1 kHz di default, sample rate configurabile per sessione; nucleo C e Janet condivisi fra host nativo e Wasm.

```sh
make                    # Compila host e DAW Janet.
make test               # Test del nucleo con fixture locali.
make -C plugins         # Compilazione separata dei plugin di esempio.
make test-plugins       # Integrazione con i sei plugin già compilati.
make prog
./build/daw examples/hello.janet build/hello.wav
./build/daw --play examples/hello.janet
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
`TARGET_OS` (`uname -s`) in minuscolo, per esempio `x86_64-linux` o `aarch64-linux`.

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

[Denti di vetro](examples/denti.janet) è una miniatura IDM originale di circa 63 secondi:
carillon inarmonici, accordi morbidi, basso su 13 passi e break a 168–186 BPM,
con raffiche di 5, 7, 9 e 13 colpi, tagli improvvisi e un finto finale. Due A-SID
filtrano rispettivamente il synth acido e le percussioni metalliche: cutoff,
quantità e velocità LFO seguono automazioni indipendenti. Le otto sezioni usano
pattern ordinari; i cambi di tempo avvengono prima della concatenazione.
Servono i quattro bundle locali di Polpo (`synth_mono`, `drums`, `shape`, `echo`)
e `../asid/plugin/perone/build/asid.perone`, configurabile con `ASID_PERONE`.

```sh
./build/editor examples/denti.janet  # Attivare GUI per le due interfacce A-SID.
./build/daw examples/denti.janet build/denti.wav 48000
```

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
con clang-format 21. Dipendenze, file generati e le copie upstream `perone.h` e `perone_ui.h`
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
| `daw/end seconds &opt options` | Chiude la partitura e imposta durata e opzioni di export. Riproduzione ed export iniziano dopo il successo dello script. |

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

Opzioni di export di `daw/end`:

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

La stessa partitura può essere riprodotta direttamente sul dispositivo audio:

```sh
./build/daw --play examples/hello.janet           # 44100 Hz
./build/daw --play examples/hello.janet 48000
```

`--play` prepara la sessione Janet e la riproduce in streaming tramite miniaudio,
con la stessa callback C usata dal browser. Termina alla fine del brano; Ctrl-C o
SIGTERM interrompono la riproduzione e liberano prima il dispositivo, poi la sessione.
L'ultimo blocco viene completato con silenzio e il player lascia scorrere la coda
del dispositivo prima di segnalare il completamento. La durata musicale resta quella
di `daw/end`, comprese le code degli effetti scelte dalla partitura.
L'uscita del player è il mix float32 stereo. `:format` e `:normalize` riguardano
l'export e vengono ignorati durante la riproduzione, sia nativa sia web: la stessa
partitura può essere ascoltata ed esportata senza modifiche. I livelli in riproduzione
dipendono dai gain della partitura; il WAV normalizzato può avere un livello diverso.

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

## Editor desktop

L'editor usa [WebUI](https://github.com/webui-dev/webui) per collegare HTML/CSS/JS
al motore C nativo. Il browser mostra l'interfaccia; Janet prepara la sessione,
miniaudio riproduce e i plugin vengono caricati dai bundle nativi `.perone`.
Non servono Node.js o una build JavaScript per usare l'editor.

```sh
make editor
# Oppure:
make build/editor
./build/editor examples/prog/polpo.janet
./build/editor --serve examples/gui.janet
# --serve stampa l'URL locale da aprire senza lanciare un browser.
make test-editor
```

Eseguire dalla radice del repository. Il build opzionale scarica WebUI 2.4.2
alla revisione fissata nel Makefile in `.deps/webui` e lo compila con GCC;
`WEBUI` può indicare un altro checkout con `include/` e `dist/`. Servono le
dipendenze del player nativo e X11, come per `daw-ui`; la prova corrente è Linux.
I plugin restano compilati separatamente. Il server ascolta solo in locale e
serve la cartella `editor/`; apertura e salvataggio dei file passano dal backend.

Il campo del percorso accetta nomi relativi al repository o assoluti. Apri legge la
partitura; Salva la scrive, preservando il file precedente se la scrittura fallisce.
I file sono testo UTF-8, fino a 8 MiB. Esegui usa il buffer corrente senza salvarlo
implicitamente e riparte dall'inizio. Percorso originale e import relativi sono
conservati anche per una partitura non ancora salvata. Il testo resta modificabile
durante l'ascolto; le modifiche si applicano alla successiva esecuzione.
Ctrl+Invio esegue, Ctrl+S salva ed Esc ferma; su macOS vale anche il tasto Command.
Errori Janet di parsing, compilazione ed esecuzione vengono mostrati nell'editor.

GUI mostra o nasconde le interfacce originali dei bundle della sessione.
Le istanze vengono create prima della riproduzione; nascondere o chiudere una loro
finestra conserva DSP e scambio dei controlli. Stop, fine del pezzo o chiusura
dell'editor liberano player, GUI e sessione in quest'ordine. Il contatore mostra
il tempo renderizzato, senza compensazione della latenza del dispositivo.

Le righe associate agli eventi in ascolto si illuminano direttamente nell'editor,
anche dentro le funzioni che generano note e automazioni. Non servono marcatori:
si usa lo stesso `lib/trace.janet` della prova web. I numeri di riga e le bande seguono
lo scorrimento, senza spostare cursore o selezione. Le note restano evidenziate per
la durata programmata, con un impulso minimo di 80 ms per note brevi e controlli.
Il riferimento è il tempo renderizzato, quindi l'allineamento all'ascolto è approssimativo.
Se testo o percorso differiscono dall'ultima esecuzione, il tracking si sospende;
rieseguire applica le modifiche, ripristinare esattamente il buffer lo riattiva.
Stop, fine del pezzo ed errori cancellano le evidenziazioni. Le origini nei file
importati restano nel rapporto; la vista mostra quelle del file aperto.

`posix/editor.c` gestisce file, comandi WebUI e ciclo di vita sul thread principale;
i callback WebUI attendono la risposta tramite un singolo posto protetto da mutex.
Questa attesa non coinvolge il thread audio. `prepare_score` condivide preparazione
da file e da buffer con la CLI e restituisce diagnostiche e, su richiesta, un rapporto
JSON di provenienza di proprietà del chiamante. Il rapporto viene trasferito una
sola volta per esecuzione; durante l'ascolto si legge solo lo stato del player.
Janet è già chiuso e il thread audio non esegue introspezione. Il tracciamento resta
sperimentale, con gli stessi limiti su tail call e origini ambigue della prova web.
Il frontend mantiene una textarea con JavaScript senza framework e disegna solo i
numeri e le bande visibili. La colorazione della sintassi resta assente: il supporto
Janet pronto trovato per [CodeMirror 6](https://github.com/ianthehenry/codemirror-lang-janet)
richiede dipendenze che per ora non introduciamo. La preparazione Janet è ancora
sincrona: il prototipo non interrompe uno script durante la valutazione e non
sostituisce la musica in corso.

## GUI native Perone

`make gui` compila il player desktop opzionale e riproduce `examples/gui.janet`,
aprendo le GUI originali di Tibia e A-SID. Serve Linux/X11, anche tramite XWayland,
con le librerie di sviluppo X11 per compilare l'host. I plugin devono essere già
compilati separatamente, incluse le librerie `*-ui.so`:

```sh
make gui
# Oppure, per una partitura diversa:
make build/daw-ui
./build/daw-ui examples/gui.janet
make test-ui
```

L'esempio dura 65 secondi: un synth Brickworks passa attraverso l'effetto di test
Tibia e il filtro A-SID. Lo score automatizza gain, cutoff e tremolo di Tibia e
cutoff, quantità e velocità LFO di A-SID, con cicli di 8, 16 e 32 secondi e 32 punti
al secondo. I controlli si muovono automaticamente già dall'inizio. Le modifiche
manuali vengono sovrascritte dal punto successivo dell'automazione: per provare
liberamente un controllo, rimuovere la relativa curva dalla partitura.
I parametri di uscita aggiornano la GUI e il pulsante reset di Tibia
scambia messaggi con il DSP. Chiudere una finestra o premere Ctrl-C termina la prova.
La partitura resta esportabile con la CLI ordinaria.

I bundle usati sono `plugins/synth_mono/build/plugin.perone`,
`../tibia/out/perone/c/build/tibia-test.perone` e
`../asid/plugin/perone/build/asid.perone`. `TIBIA_PERONE` e `ASID_PERONE` possono
scegliere altri percorsi; per esempio, la variante C++ di Tibia è in
`../tibia/out/perone/cxx/build/tibia-test.perone`.

`perone_ui.h` è il contratto UI ABI v1 copiato da Tibia. `posix/ui.c` legge i
metadati tramite Janet, carica la libreria UI separata e gestisce finestre, idle e
callback sul thread grafico. Apre una GUI per nodo quando il bundle contiene la
libreria; gli effetti mono duplicati su stereo condividono la stessa GUI.
Il widget viene mostrato e ridimensionato dopo la notifica di creazione del server
X11, anche quando il plugin usa una connessione separata.
`posix/ui_main.c` usa il player miniaudio comune. X11 è una dipendenza degli
eseguibili desktop opzionali `daw-ui` ed `editor`; il DSP continua a usare Perone ABI v2.

Il protocollo dei controlli vive nel loader nativo: la GUI richiede modifiche e legge
valori e messaggi attraverso poche operazioni, senza gestire direttamente atomiche
o code. Le modifiche dei parametri vengono accorpate all'ultimo valore e applicate
dal thread audio in ordine di indice prima del blocco successivo, insieme alle
eventuali copie L/R. Seguono i messaggi UI→DSP, in ordine FIFO: non esiste un ordine
unico tra parametri e messaggi. Un messaggio non può quindi fare da separatore tra
due modifiche dello stesso parametro. Gli eventi già programmati nello score
possono poi sovrascrivere i valori della GUI, anche al medesimo campione.

Il thread audio pubblica i valori e conferma ogni richiesta soltanto dopo averla
applicata; una richiesta più recente rimane in attesa. La GUI legge i valori senza
chiamare il DSP e senza rimandare al controllo un valore precedente alla modifica.
Per un effetto duplicato, valori di uscita e messaggi visualizzati provengono
dall'istanza sinistra. I tre callback di inizio, modifica e fine gesto applicano
tutti il valore ricevuto; i gesti non vengono ancora registrati nella partitura.
La GUI arrotonda i parametri interi e limita i valori al range del JSON; indici
invalidi, parametri di uscita e valori non finiti sono errori. Negli script, invece,
i valori fuori range o non interi vengono rifiutati per segnalare l'errore nello score.

I messaggi usano due code con un produttore e un consumatore, 16 posti ciascuna,
allocate prima della riproduzione secondo i limiti del JSON, fino a 4096 byte per
messaggio. Il riempimento della coda o un messaggio fuori limite interrompe la prova
con un errore. Senza una GUI i messaggi in uscita vengono scartati. Il web mantiene
il limite a zero: le GUI native `.so` non si eseguono nel browser; questi bundle
attualmente non includono un modulo UI web.

`make test-ui` richiede un display X11 e i bundle già compilati di Tibia C/C++ e
A-SID. Verifica embedding, gesti reali del mouse, audio, sincronizzazione L/R,
feedback delle automazioni, messaggi, creazione differita del widget, ridimensionamento e chiusura. `make test`
verifica anche l'ordine dei controlli, le conferme concorrenti e le code tra thread,
senza richiedere X11 o plugin esterni.

## Portabilità e test web

`test/live.html` è un [prototipo di evidenziazione del codice durante l'ascolto](test/README.md).
Esegue Polpo senza marcatori: il prototipo osserva la costruzione delle liste di eventi
e le funzioni dei pattern durante la preparazione Janet, conservando anche le posizioni
dentro le funzioni che generano note e controlli. Offre un editor
essenziale, Esegui/Stop e una copia del codice attualmente in ascolto.
Il ponte C in `trace.c` viene registrato normalmente da `script_env`; il modulo
`lib/trace.janet` attiva l'osservazione su richiesta, prima di compilare lo score.
La pagina usa i runtime ordinari prodotti da `make web`.
`make test-live` verifica il prototipo in Chromium; richiede gli stessi quattro bundle
Perone wasm32 della prova Polpo descritta sotto. Il tracciamento rimane sperimentale:
le chiamate in coda possono perdere frame e pattern uguali possono avere origini ambigue.

Il codice comune (`engine.c`, `session.c`, `daw.c`, `script.c`, `trace.c`, `player.c`) non usa
direttamente API POSIX né contiene rami condizionali per piattaforma. Il Makefile
seleziona i sorgenti nativi in `posix/` oppure quelli Wasm in `web/`.
`posix/loader.c` conserva `dlopen` e `realpath`; `posix/export.c` gestisce il WAV,
inclusi file temporanei, seek e sostituzione della destinazione. Le CLI in `posix/`
gestiscono segnali e attesa. `PERONE_PLATFORM` e `PERONE_SUFFIX` selezionano directory e suffisso
del binario (`.so` sul backend nativo attuale, `.wasm` sul web).
`TARGET_OS=Darwin` esclude `-ldl`; Windows richiede ancora un backend nativo per loader,
export e attesa della CLI. Le build native dei plugin devono essere fornite per ciascuna
piattaforma. Sono verificati qui Linux e Wasm; gli altri sistemi non sono ancora certificati.
Anche i test nativi in `test/` e la demo `examples/termux_synth/` usano API POSIX.

La CLI può scegliere il sample rate senza modificare la partitura:

```sh
./build/daw examples/patterns.janet build/patterns-48k.wav 48000
```

Per Wasm serve Emscripten (verificato con 6.0.9), e `patch` per il player; i test richiedono anche Node.js
(verificato con 24.18).
Janet viene compilato da sorgente con Emscripten, separatamente dalla libreria nativa.
I plugin restano moduli Perone wasm32 autonomi, compilati e distribuiti separatamente.
Il player web usa miniaudio con AudioWorklet; il runtime offline rimane indipendente
dal dispositivo audio. Nessuna build web collega l'export POSIX.

```sh
make web                         # Runtime offline daw.mjs e player miniaudio player.mjs in build/web/.
make test-web                    # Fixture indipendenti da Tibia/Brickworks e confronto PCM nativo/Wasm.
make test-browser                # Verifica anche l'uscita AudioWorklet in Chromium (CHROMIUM=/percorso opzionale).
# Se emcc non è nel PATH: make test-web EMCC=/percorso/emsdk/upstream/emscripten/emcc
node test/server.mjs
# Aprire http://localhost:8000/test/web.html e premere Esegui test.
```

`test/web.html` è un banco di prova senza CSS. Riproduce `test/schedule.janet` e
`test/playback.janet` tramite miniaudio, confrontando i campioni emessi dall'AudioWorklet
con il render offline a 44,1 e 48 kHz. Verifica MIDI, effetti mono su stereo, silenzio
nel blocco finale, arresto, riavvio ed errori. Include chiusura esterna del contesto,
timeout del worklet, cancellazione durante l'inizializzazione e ritentativi dopo errori.
Usa fixture e non richiede plugin esterni.
La lettura di `product.json`, i pattern, la validazione, la schedulazione e il mix vengono
eseguiti dal codice comune. `test-web` confronta anche un minuto di automazioni con il nativo.

`test/polpo.html` esegue invece la partitura originale `examples/prog/polpo.janet`,
con synth, waveshaper, echo e percussioni Perone: 34 istanze DSP, 30 secondi a 48 kHz.
Il pulsante riproduce il pezzo tramite miniaudio e confronta tutti i campioni emessi
dall'AudioWorklet con il mix offline Wasm, verificando anche il rilascio delle risorse.
Il volume di ascolto del test è ridotto dopo il punto di acquisizione del PCM.

```sh
make -C plugins synth_mono shape echo drums PERONE_PLATFORM=wasm32
make test-polpo-web               # Prova completa in Chromium; richiede i bundle già compilati.
node test/server.mjs
# Aprire http://localhost:8000/test/polpo.html e premere Riproduci e verifica Polpo.
```

Il build Wasm dei plugin locali usa Emscripten anche per libc/libm, mantenendo
moduli Perone autonomi. Se `emcc` non è nel `PATH`, passare `EMCC=/percorso/assoluto/emcc`
ai comandi `make`; il [README dei plugin](plugins/README.md) descrive il build separato.

Il player richiede HTTPS (oppure localhost), AudioWorklet e memoria condivisa.
Il server di test imposta `Cross-Origin-Opener-Policy: same-origin` e
`Cross-Origin-Embedder-Policy: require-corp`; un normale `python -m http.server`
senza questi header non basta. La riproduzione è verificata in Chromium; Firefox e
Safari restano da verificare. La build usa `MA_ENABLE_AUDIO_WORKLETS`, `AUDIO_WORKLET`,
`WASM_WORKERS`, `ASYNCIFY` e `-pthread` per le primitive di sincronizzazione di miniaudio.

`web/player.js` espone `createPlayerHost`, `preparePlayer(host, path, sampleRate)` e
`closePlayer(host)` per cancellare una preparazione o ritentarne la pulizia se fallisce.
I file vengono precaricati con `addFile`, come nel runtime offline. Janet prepara la
sessione prima dell'avvio; i moduli compilati e i parametri iniziali vengono poi passati
all'AudioWorklet, che ricrea le istanze DSP dopo aver liberato quelle della preparazione.
`releasePrepared` e `restorePrepared` usano configurazioni con campi nominati e conservano
gli identificatori C. La ricostruzione riguarda soltanto lo stato iniziale: dopo rendering
o MIDI viene rifiutata. Gli identificatori ceduti al worklet restano registrati sull'host
fino al rilascio della sessione; un identificatore sconosciuto è un errore.
La callback comune in `player.c` richiama `session_render`: il player produce i blocchi su
richiesta senza conservare il PCM dell'intero pezzo. `web/worklet.js` gestisce soltanto
preparazione e rilascio dei plugin; il processore audio è quello di miniaudio.

Il player restituito espone `start()`, `status` (0 in corso, 1 terminato, -1 errore) e
`close()`, da attendere prima di riusare l'host. Un host gestisce un player alla volta;
`close()` silenzia la callback, attende la sospensione, chiede il rilascio dei DSP e
chiude il contesto prima di liberare la memoria C condivisa. Le risposte del worklet
hanno un timeout di 10 secondi e gli errori di pulizia vengono riportati senza saltare
i rilasci successivi. Se la chiusura del contesto non è confermata, la memoria e il blocco
dell'host restano attivi: si può ritentare `close()` o `closePlayer(host)`. La cancellazione
durante la preparazione attende l'inizializzazione asincrona di miniaudio; da quando inizia
la chiusura, `start()` e `status` non sono più disponibili. `context` e `node` permettono
di collegare l'uscita ad altri nodi Web Audio. La normalizzazione del
picco si applica soltanto al rendering offline; il player riproduce il mix della sessione.
Le partiture sono ancora preparate in anticipo; questo non introduce live coding.

La copia generata `build/web/miniaudio.h` applica `web/miniaudio.patch` alla versione
0.11.25: conserva e libera correttamente lo stack allineato dell'AudioWorklet, anche
nei percorsi di errore. Il sorgente scaricato e la build nativa restano invariati.
La patch andrà rimossa quando la correzione sarà disponibile nella dipendenza.
`web/audio.js` corregge inoltre la deregistrazione del contesto in Emscripten 6.0.9:
evita di chiamare `suspend()` su un contesto già chiuso, rendendo sicuro l'ordine di rilascio.

`web/host.js` espone `createHost`, `addFile` e `renderScore`. Il chiamante precarica
script, import e bundle nel filesystem virtuale mantenendo i relativi percorsi;
`addFile` prepara anche i moduli `.wasm`. Janet continua a leggere e interpretare
il JSON. `web/worker.js` gestisce una sola esecuzione, cancellabile terminando il Worker.
Il risultato è PCM float stereo interleaved, con l'eventuale normalizzazione della
partitura; `:format` riguarda l'export WAV nativo. Solo questa API offline raccoglie
l'intero render in memoria; può continuare a essere eseguita in un normale Worker.

Il collegamento Wasm segue il trasferimento dei buffer del template `web` di Tibia,
usando però l'ABI Perone generica. Ogni istanza DSP usa una propria istanza WebAssembly;
i buffer vengono allocati nella memoria del plugin e copiati da/verso quella del motore.
Le callback delle directory usano funzioni Wasm tipizzate. Allocazioni e crescita della
tabella avvengono durante la preparazione; la crescita della memoria DSP durante il
processamento viene rifiutata. Le dimensioni iniziali delle memorie dipendono dai bundle
(ad esempio, quelli Brickworks attuali riservano circa 8 MiB ciascuno).

## Struttura e limiti

`engine.c/h` gestisce istanze e scheduler, senza CLI né Janet.
`session.c/h` contiene lo stato esplicito della sessione, le catene e il mixer.
`daw.c` collega Janet alla sessione; `posix/daw_main.c` è la CLI di export e riproduzione.
`posix/export.c` contiene la scrittura WAV e la pubblicazione del file.
`player.c/h` gestisce il dispositivo miniaudio, la callback e lo stato di riproduzione,
condivisi fra nativo e Wasm. Il player prende in prestito una sessione appena preparata
e chiusa con `session_end`: il chiamante deve liberare il player prima della sessione,
che durante la riproduzione è usata soltanto dalla callback audio.
`web/player_api.c` adatta lo score web al player e restituisce gli identificatori di
AudioContext e AudioWorklet; la gestione asincrona rimane in `web/player.js`.
`posix/main.c` è la demo audio nativa. `posix/loader.c` implementa il backend DSP POSIX;
`web/loader.c` e `web/perone.js` quello Wasm. Il motore chiama lo stesso piccolo
insieme di operazioni per apertura, chiusura, parametri, reset, MIDI e processamento.
Il contratto completo è in `loader.h`: `Engine` contiene configurazione e un puntatore
opaco `DSP`, oltre allo scheduler. `posix/module.h` è privato al loader/UI POSIX e alle fixture
che ne costruiscono istanze; memoria DSP, handle di libreria e API Perone non entrano
nel motore comune. L'apertura fallita libera le risorse del backend e non modifica l'engine.
`script.c` prepara Janet e trasferisce configurazioni numeriche al motore C.
`trace.c` registra il ponte per il tracciamento opzionale; `lib/trace.janet` conserva
la provenienza fuori dai valori musicali e produce il rapporto per l'editor.
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
gli eventi già programmati e, nel player desktop, i controlli delle GUI.

Gli eventi crescono durante la preparazione e vengono ordinati una volta sola.
Janet viene chiuso prima del rendering; il motore non alloca memoria mentre processa
i blocchi. Il mix occupa memoria indipendente dalla durata, più lo stato dei plugin
e gli eventi. Limiti attuali: un export per esecuzione, 32 tracce, 128 nodi totali
(plugin e mixer), 8 effetti per catena, 64 parametri per plugin, 3600 secondi.
L'host accetta un bus audio principale di uscita mono/stereo, al massimo un bus
principale di ingresso mono/stereo e un ingresso MIDI. Il sample rate viene fissato
prima della preparazione (`Session.sample_rate`, zero sceglie 44100), fra 1 e 384000 Hz;
non può essere cambiato durante la sessione. Le sidechain
opzionali rimangono scollegate: il DSP riceve `NULL` nelle loro posizioni originali.
Sono supportati fino a 8 canali di ingresso complessivi, inclusi quelli scollegati.
CV, sidechain obbligatorie e bus principali aggiuntivi vengono rifiutati prima
che il DSP venga caricato. Il transport sincronizzato obbligatorio viene rifiutato;
se dichiarato opzionale, il plugin usa il proprio comportamento senza transport.
Il backend nativo gestisce i messaggi della GUI entro i limiti descritti sopra;
il backend web rifiuta ancora bundle che dichiarano messaggistica. Il salvataggio dello
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
`test/player.c` confronta l'uscita della callback con il WAV a 44,1/48 kHz, usando
blocchi variabili e un dispositivo simulato: verifica silenzio finale, attesa della
coda, errori di inizializzazione/avvio/rendering, interruzione e CLI `--play`.
Le opzioni di export non impediscono l'ascolto e non alterano il PCM del player;
la stessa distinzione è verificata nell'AudioWorklet.
Il test usa Janet e DSP reali delle fixture e non richiede un dispositivo audio.
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
