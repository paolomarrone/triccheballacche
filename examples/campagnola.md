# Campagnola Stomp — versione 2

Piano honky-tonk solo ispirato al tema strumentale di **A campagnola** del MIDI di
Gigione fornito dall'utente. 4/4, tempo di riferimento 124 BPM, 68 battute, circa
**2 minuti e 18 secondi** compresi rallentando e decadimento finale. Il trio si
rilassa a circa 115 BPM, l'ultimo ritornello accelera leggermente a circa 127.

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
Le sue 34 note mantengono esattamente tempi, durate e velocity della prima
versione. Le riprese successive cambiano registro, mano, articolazione e
accompagnamento; nel centro entrano due nuove sezioni melodiche.

| Tempo | Battute | Forma |
| --- | --- | --- |
| 0:00 | 1–4 | Introduzione di habanera e lancio cromatico. |
| 0:08 | 5–12 | Tema completo esposto sull'habanera. |
| 0:23 | 13–20 | Ripresa ragtime con anticipi, bassi rivoltati e accompagnamento Charleston più leggero. |
| 0:39 | 21–28 | Motivo cromatico del MIDI, salti di registro, bassi in movimento e risposte separate da pause. |
| 0:54 | 29–36 | Nuovo cantabile fra minore e relativo maggiore: frasi più lunghe, accordi arpeggiati e tempo più disteso. |
| 1:11 | 37–44 | Nuovo ponte in si bemolle, accordo diminuito di passaggio e catena di dominanti per tornare in sol. |
| 1:26 | 45–48 | Break stop-time: chiamate diseguali, risposta nel basso e pausa prima della ripresa. |
| 1:34 | 49–56 | Tema in dialogo: prima alla sinistra, poi alla destra, infine nel registro acuto. |
| 1:50 | 57–64 | Ritornello a ottave, bassi in movimento, registro crescente e piccolo slancio di tempo. |
| 2:05 | 65–68 | Coda sul tema, rallentando e due accordi conclusivi. |

Il piano campionato usa una seconda corda a +12 cent, miscelata al 34%.
Il tocco e i tempi variano leggermente in modo riproducibile; non ci sono batteria,
compressione da musica dance o riverbero aggiunto.
La varietà viene anche dai vuoti: accompagnamenti a due appoggi, arpeggi spezzati,
passaggi più aperti e una dinamica più raccolta nel cantabile. Gli unisoni fra le
mani vengono uniti e ogni tasto viene rilasciato prima dell'attacco successivo.

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
