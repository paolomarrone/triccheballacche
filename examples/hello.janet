# Seven eighth notes: change this file and render again, without recompiling.
(def bass (daw/plugin "examples/synth_mono/plugin.so"
  {:vco1_wave 2 :vcf_cutoff 900 :vca_attack 2 :vca_release 80}))
(daw/track bass)
(def beat (/ 60 154))
(def phrase [40 40 47 41 40 50 44])
(for i 0 (length phrase)
  (daw/note bass (* i beat 0.5) (* beat 0.4) (phrase i)))
(daw/end 2)
