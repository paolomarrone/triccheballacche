# Oculus — techno trance a due voci

Brano in **4/4 a 150 BPM**. Cassa dritta, basso in sedicesimi e
sub in levare, due voci principali con oscillatori detunati, sequenze acide,
arpeggi veloci e rullate. Il brano dura **2:41,200** e ha un finale definito.

Si usano **entrambe le parti complete** del MIDI incluso, con il loro intreccio
e la cadenza conclusiva.
P1 ha 108 note; P2 ne ha 119 e inizia al beat 6 della fonte. Le esposizioni
complete mantengono questa entrata imitativa.

## Fonte e voci

Il MIDI originale è [sources/oculus-non-vidit.mid](sources/oculus-non-vidit.mid).
La trascrizione completa, leggibile e indipendente da un parser MIDI a runtime,
è in [sources/oculus-non-vidit.janet](sources/oculus-non-vidit.janet): ogni riga
contiene attacco, durata in semiminime e altezza MIDI.

SHA-256 del MIDI:
`a63915630cbaf48e5b886df91332828e935cff2f08a3453a2c3764207c59724e`.

P1 ha un suono a denti di sega, prevalentemente a sinistra. P2 ha un nucleo a
onda impulsiva, prevalentemente a destra. Ciascuna parte ha due istanze detunate;
i livelli sono bilanciati confrontando le parti isolate. Basso e arpeggio sono
strumenti aggiuntivi, costruiti sulle altezze della fonte.

Nelle due esposizioni di 32 battute tutte le note vengono eseguite nell'ordine,
registro e posizione della fonte. La fermata finale del MIDI, di 8,073 beat,
è limitata a 8 per chiudere la cella di 128 beat. Il tempo è fisso a 150 BPM;
il rallentando del MIDI non viene riprodotto. La sezione acida raddoppia la
velocità di un estratto di entrambe le parti, mantenendo anche lì due linee.

## Struttura

| Tempo | Battute | Sezione |
| --- | --- | --- |
| 0:00 | 1–4 | Ignition: ingresso delle due voci e apertura del filtro |
| 0:06 | 5–36 | Duet: primo passaggio completo del MIDI sopra cassa e basso |
| 0:58 | 37–52 | Acid: entrambe le parti a doppia velocità, filtri e accenti mobili |
| 1:23 | 53–60 | Break: estratto centrale a due voci, batteria diradata |
| 1:36 | 61–64 | Build: cadenza, rumore crescente, rullata e arresto |
| 1:42 | 65–96 | Rave: seconda esposizione completa, arpeggi e terzine nella ripresa |
| 2:34 | 97–100 | End: cadenza originale di entrambe le voci, re finale e coda |

## Ascolto ed esportazione

Dalla radice del repository, dopo aver compilato CLI e plugin come nel README:

```sh
./build/cli --play examples/oculus.janet 48000
```

Esportazione stereo PCM16 a 48 kHz:

```sh
mkdir -p renders
./build/cli examples/oculus.janet renders/oculus.wav 48000
```

Editor dal browser:

```sh
./build/gui --serve examples/oculus.janet
```

Si usano solo i plugin locali `synth_mono`, `drums`, `shape` ed `echo`.
Il render normalizza il picco a 0,94; la saturazione sul master agisce anche
durante la riproduzione nativa.

Il WAV viene generato in `renders/oculus.wav`. La cartella `renders/` è ignorata
da Git; partitura, MIDI e trascrizione sono inclusi nel repository.
