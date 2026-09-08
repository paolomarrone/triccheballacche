# Seven eighth notes: change this file and render again, without recompiling.
(def bass (daw/instrument "examples/synth_mono/plugin.so"
  {:params [7 2 26 900 33 2 36 80]}))
(def beat (/ 60 154))
(def phrase [40 40 47 41 40 50 44])
(for i 0 (length phrase)
  (daw/note bass (* i beat 0.5) (* beat 0.4) (phrase i)))
(daw/export 2)
