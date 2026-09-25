# Campagnola Stomp

Piano honky-tonk solo ispirato al tema strumentale di **A campagnola** del MIDI di
Gigione fornito dall'utente. 4/4, 124 BPM, 68 battute, circa **2 minuti e 17 secondi**
compresi rallentando e decadimento finale.

Il riferimento stilistico è il pianismo di Jelly Roll Morton e
[The Crave](https://imslp.org/wiki/The_Crave_(Morton,_Jelly_Roll)):
accompagnamento di habanera, stride, sincopi, acciaccature cromatiche e alternanza
fra maggiore e minore. Non viene citata la melodia di The Crave.

Il tema di Gigione viene dal canale MIDI 0, dove gli ottoni lo raddoppiano
all'ottava. L'estrazione conserva la nota superiore di ogni attacco: battiti
4–36 per il tema e 60–92 per la risposta cromatica, a 192 tick per quarto.
Le note e le durate originali sono in [sources/campagnola-theme.janet](sources/campagnola-theme.janet),
insieme all'hash del file di provenienza `Gigione - A Campagnola.mid`.
Questa è una rielaborazione del motivo strumentale presente nel file; non una
trascrizione della voce cantata. Bassi, armonizzazioni, introduzione, trio e coda
sono nuovi. Il primo tema conserva tutte le altezze e gli attacchi della frase;
le durate più lunghe vengono accorciate per l'articolazione pianistica.

| Battute | Forma |
| --- | --- |
| 1–4 | Introduzione di habanera e lancio cromatico. |
| 5–12 | Tema esposto, accompagnamento di habanera. |
| 13–20 | Tema con stride, bassi all'ottava e piccole risposte. |
| 21–28 | Secondo motivo cromatico del MIDI. |
| 29–36 | Trio in minore, più raccolto. |
| 37–40 | Break stop-time con corse della destra e silenzi. |
| 41–48 | Tema con ottave e acciaccature. |
| 49–56 | Risposta cromatica più fitta. |
| 57–64 | Ultimo ritornello, dinamica più forte. |
| 65–68 | Coda sul tema, rallentando e due accordi conclusivi. |

Il piano campionato usa una seconda corda a +12 cent, miscelata al 34%.
Il tocco e i tempi variano leggermente in modo riproducibile; non ci sono batteria,
compressione da musica dance o riverbero aggiunto.

## Esecuzione

Preparare il [plugin piano](../plugins/piano/README.md), poi dalla radice del repo:

```sh
mkdir -p renders
./build/cli examples/campagnola.janet renders/campagnola-stomp.wav 48000
./build/cli --play examples/campagnola.janet 48000
./build/gui --serve examples/campagnola.janet
```

Il WAV è stereo PCM16, normalizzato al picco 0,94. La GUI nativa usa lo stesso
strumento; il progetto non è incluso nel catalogo WebAssembly.

Per esportare gli eventi della stessa esecuzione in CSV (inizio/fine in quarti
effettivi, nota MIDI, velocity, mano: 0 sinistra / 1 destra), impostare
`CAMPAGNOLA_EXPORT=renders/campagnola-events.csv` davanti al comando di render.
Il rallentando è già incorporato nei tempi esportati.
