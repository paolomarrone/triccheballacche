# Prove di interfaccia

`make test-editor` verifica il frontend WebUI in Chromium e il motore audio nativo:
apertura di nomi e contenuti Unicode, import relativi da testo non salvato,
Esegui/Stop, tracking dei produttori e degli import, allineamento di righe e bande
durante lo scorrimento, sospensione dopo modifiche, salvataggio atomico, diagnostiche e ripresa dopo errori, chiamate
concorrenti e chiusura della pagina durante l'ascolto. Copre anche note e catene della
timeline, densità, selezione dell'origine, permanenza dopo Stop/errori, scorrimento delle
corsie, follow, tempi grandi, fine sconosciuta e risposte obsolete. Le richieste e il
canvas mantengono dimensioni limitate al cambiare dell'intervallo. Usa la fixture Perone locale;
richiede Chromium e un dispositivo audio, senza plugin di produzione.
`test/chromium.mjs` condivide avvio, DevTools e pulizia del browser con i test Wasm.
`make test` copre anche preparazione da buffer, diagnostiche e proiezione nativa senza
browser o dispositivo; confronta il PCM con e senza tracciamento a 44,1 e 48 kHz.
`score_view.c` verifica separatamente copia degli eventi, pairing di note sovrapposte,
query indicizzate e riepiloghi confrontati con una scansione completa, anche a tempi grandi.

`make gui` apre le GUI native Perone di Tibia e A-SID con `examples/gui.janet`.
`make test-ui` verifica le UI originali C/C++, i controlli sul DSP e i messaggi;
richiede un display X11 e i bundle compilati nei repository adiacenti. I dettagli
e i percorsi sono nel [README principale](../README.md#gui-native-perone).
Le prove del protocollo di controllo e delle code concorrenti sono in `loader.c`,
eseguite da `make test` anche senza display: ultimo valore dei parametri, ordine dei
messaggi, copie stereo, conferme durante un setter sospeso e riporto dei contatori.

## Editor web e provenienza Janet

La pagina `editor/index.html` usa lo stesso controller e la stessa timeline del
desktop. Il backend Wasm esegue Janet e audio nel browser; non serve un eseguibile
nativo per usare la pagina.

```sh
make -C plugins synth_mono shape echo drums PERONE_PLATFORM=wasm32
make web-editor
node test/server.mjs
# Aprire http://localhost:8000/editor/index.html
make test-editor-web
make test-trace
```

`test/editor-web.mjs` verifica Polpo in Chromium, audio, righe attive, snapshot dopo
Stop e fallimenti, navigazione a tempi grandi, ripresa, apertura dal filesystem della
sessione e download Unicode senza modificare il file sul server. Accetta un altro
score come argomento, purché le sue risorse siano nel catalogo. `WEB_CONTENT` in
`make web-editor` seleziona le directory da pubblicare; per esempio si può aggiungere
`../asid/plugin/perone/build` e provare `node test/editor-web.mjs examples/denti.janet`.

`make test-web` include `view-json.mjs`: confronta tutte le risposte della proiezione
C nativa e Wasm a 44,1/48 kHz, con densità, note che attraversano la finestra, origini
importate, revisioni obsolete e argomenti invalidi. Lo score audio viene liberato
prima delle query: la proiezione deve restare valida. I test del player verificano
anche il trasferimento della vista attraverso l'avvio e la chiusura dell'AudioContext.

Se Emscripten non è nel PATH, passare `EMCC=/percorso/assoluto/emcc` a make.
I runtime ordinari, nativi e web, includono il piccolo ponte `trace.c`, registrato
esplicitamente da `script_env`. `lib/trace.janet` attiva il tracciamento soltanto
quando viene chiamato `trace/install`: il solo import lascia intatte le funzioni
musicali e `array/push`. L’editor usa lo stesso player delle altre prove web.
`trace-host.mjs` seleziona le righe dal rapporto completo per verificare il tracciatore.
Entrambi gli editor usano l’indice C e richiedono soltanto i frame attivi.
`json/encode`, già fornito da Spork, permette di esportare il rapporto prima della
chiusura di Janet. Le librerie musicali e le partiture rimangono invariate.

## Come funziona

`trace-host.mjs` carica un piccolo script di avvio nel filesystem virtuale. Questo
installa `lib/trace.janet`, poi esegue il file originale con il suo percorso e gli stessi
import. Non aggiunge righe al sorgente dell'utente e non riscrive le espressioni.

`trace/install` riceve l'ambiente dello score e il percorso dello script di avvio,
da escludere dalle posizioni mostrate. Va chiamato una sola volta per preparazione,
prima di compilare lo score e i suoi import, e restituisce la funzione che legge
il rapporto. Una seconda installazione viene rifiutata prima di cambiare i binding.
La chiusura della VM, anche dopo errori, libera binding e tabelle: la preparazione
successiva parte da un ambiente nuovo.

Lo stesso avvio funziona con la CLI. Per esempio, in `build/trace-run.janet`:

```janet
(def entry (dyn :current-file))
(import ../lib/trace)
(def report (trace/install (curenv) entry))
(def daw/script "examples/prog/polpo.janet")
(dofile daw/script :env (curenv))
(spit "build/trace-polpo.json" (json/encode (report)))
```

Eseguire con `./build/daw build/trace-run.janet build/trace-polpo.wav`.

L'osservatore C delega alla funzione originale `array/push`, poi passa array, indice
iniziale e fiber chiamante all'osservatore Janet. Questo conserva la provenienza
dei valori con forma `[inizio fine valore]`, associandola agli indici dell'array.
Quando `p/events` riceve quell'array, trova quindi anche le chiamate interne a
`note`, `drum` e altre funzioni già terminate. Non dipende dai loro nomi.
Il passaggio C conserva il frame Janet del chiamante anche quando questo termina
con una chiamata in coda a `array/push`; una funzione Janet sostitutiva lo perderebbe.
L'osservazione vale anche per gli alias della funzione e i moduli importati dopo
l'installazione. Le operazioni interne al tracciatore usano l'originale già compilato.

`lib/trace.janet` sostituisce anche le funzioni esportate del modulo pattern nella cache Janet,
prima che lo score le importi. Gli originali continuano a produrre i dati musicali.
Le funzioni sostitutive acquisiscono `debug/stack (fiber/current)` e costruiscono
una seconda sequenza con gli stessi tempi e gli identificatori delle posizioni.
Le trasformazioni temporali usano le funzioni originali anche per questa sequenza;
le funzioni utente passate a `map` e `curve` vengono eseguite una volta sola.

`daw/schedule` esporta i tempi assoluti, le origini e l'ordine originale degli eventi
nel nodo (letto tramite `native/event-count`); le chiamate dirette a `daw/note`
e `daw/param` sono intercettate allo stesso modo. Il rapporto vive separatamente
dagli eventi audio e dai plugin. Niente introspezione durante il rendering.
Le tabelle del prototipo vengono conservate fino alla fine della preparazione.

Il rapporto contiene `:locations`, una lista di stack con file/riga/colonna, e
`:events`, con record `[inizio fine origini tipo nodo]` in secondi assoluti.
Le origini sono indici in `:locations`. Gli stack descrivono la costruzione degli
eventi: durante l'ascolto Janet è già chiuso.

La pagina usa l'orologio dell'AudioContext, con `getOutputTimestamp()` quando
disponibile, per stimare la posizione in ascolto. È un allineamento visivo, non una
misura della latenza acustica. Le note illuminano le righe per la durata programmata,
con un minimo visivo di 80 ms anche per i controlli e le percussioni di pochi campioni,
che altrimenti possono cadere interamente fra due aggiornamenti dello schermo. Release e riverberi non
prolungano l'evidenziazione. Più righe possono essere attive contemporaneamente.

## Risultati e limiti

- Polpo produce 1.795 eventi, tutti con origine raccolta durante la costruzione
  delle liste: il rapporto include le righe dei produttori, i chiamanti dentro le
  sezioni e le cinque chiamate a `section`. Le catture dello stack sono 1.804.
  La partitura non contiene marcatori e l'export nativo con/senza tracciamento è identico.
- Lo stack descrive le chiamate ancora attive. Osservare soltanto `p/events`, come
  nella prima versione, mostrava quasi solo le cinque chiamate a `section`.
  Per la granularità interna bisogna raccogliere le posizioni durante la costruzione
  dei singoli eventi. Non tutte le righe di calcolo producono un evento osservabile.
- Le chiamate in coda fra funzioni Janet possono ancora eliminare frame intermedi.
  La fixture perde la posizione interna del suo `tail-helper` basato su `p/steps`,
  ma conserva il produttore che termina con `array/push` grazie al passaggio C.
- Janet può riunire costanti immutabili uguali durante la compilazione. Due pattern
  identici, costruiti su righe diverse, possono quindi diventare lo stesso oggetto
  quando vengono usati insieme. Una tabella basata sull'indirizzo darebbe attribuzioni
  errate. Il prototipo raccoglie invece le possibili origini dei pattern uguali,
  evidenzia tutte le candidate e riporta quanti eventi hanno origine ambigua.
- Un pattern costruito direttamente come struct non attraversa i costruttori:
  il punto di scheduling diventa l'origine di ripiego, conteggiata nel rapporto.
- `p/events` usa l'origine raccolta da `array/push` quando lo slot è ancora
  riconoscibile, altrimenti usa il proprio punto di chiamata. Le operazioni della
  libreria pattern conservano le origini disponibili; questo non ricostruisce la
  provenienza di calcoli o manipolazioni arbitrarie. `:fallback-events` conta gli
  eventi dei pattern senza annotazione, non tutti i casi di dettaglio ridotto.
- Le posizioni sono punti (file, riga e colonna), non intervalli completi del testo.
  La pagina mostra le righe del file aperto; il rapporto conserva anche i frame
  dei file importati. Non individua automaticamente ogni numero di una lista.
- Il tracciamento non è ancora un contratto pubblico. Import con cache alternativa,
  `:fresh`, scritture con `put`, riordinamenti manuali degli array, trasformazioni
  manuali dei dati e scheduling parzialmente fallito e poi
  recuperato non hanno una garanzia di provenienza completa.

`trace.mjs` confronta il PCM con e senza tracciamento a 44,1 e 48 kHz, verifica
trasformazioni, pause, chiamate dirette, produttori interni e importati, alias di
`array/push`, origini ambigue e tail call. Verifica anche che l'import sia inerte,
che l'installazione sia unica e che dopo errori si possano preparare score normali
e tracciati sullo stesso host. Scrive un rapporto in `build/trace-fixture.json`.
`test-editor-web` osserva nel DOM il tracking di Polpo e il comportamento della
proiezione condivisa: modificare il testo sospende le evidenziazioni, Stop conserva
le note, un errore non sostituisce l'ultima esecuzione e la successiva può ripartire.


## UI Perone condivise

`make test-editor-ui` prepara bundle di fixture temporanei e usa la stessa UI ES
in Chromium con entrambi i backend dell'editor. La UI importa un altro modulo JS,
un CSS e un Wasm con import esterni tramite `instantiateStreaming`: questi asset
devono mantenere percorsi e MIME corretti, senza entrare nel loader DSP standalone.
Il test verifica valori iniziali/automatizzati, meter, modifiche manuali, messaggi
binari, passaggio ai controlli generici, callback scaduti, smontaggio, Stop durante una creazione asincrona e riavvio.
Non richiede Tibia o A-SID; lascia le catture in `build/editor-ui-{web,native}.png`.

`make test-web` include `perone-controls.mjs`: verifica anche l'audio delle due copie
di un effetto mono dopo una modifica, la precedenza delle automazioni, conferme dei
valori, code FIFO limitate, messaggi vuoti/binari, overflow e riattacco della vista.
`request.mjs` controlla che una risposta tardiva non confermi una richiesta successiva.
