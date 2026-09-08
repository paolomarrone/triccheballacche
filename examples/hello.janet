# Seven eighth notes: change this file and render again, without recompiling.
(import ../lib/music)

(def bass (daw/plugin "plugins/synth_mono/build/plugin.so"
  {:vco1_wave 2 :vcf_cutoff 900 :vca_attack 2 :vca_release 80}))
(daw/track bass)
(def beat (music/seconds 154 1))
(def phrase [40 40 47 41 40 50 44])
(music/sequence 0 (* beat 0.5) phrase
  (fn [t pitch] (daw/note bass t (* beat 0.4) pitch)))
(daw/end 2)
