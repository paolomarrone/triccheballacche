# Seven eighth notes: change this file and render again, without recompiling.
(import ../lib/pattern :as p)

(def bass (daw/plugin "plugins/synth_mono/build/plugin.perone"
  {:vco1_wave 2 :vcf_cutoff 900 :vca_attack 2 :vca_release 80}))
(daw/track bass)
(def phrase (p/steps 0.5 [40 40 47 41 40 50 44]))
# Each note holds for 0.4 beats; the phrase retains its seven eighth-note slots.
(daw/schedule 0 154
  (p/events (phrase :length)
    (seq [[a _ pitch] :in (phrase :events)] [a (+ a 0.4) [:note bass pitch 100]])))
(daw/end 2)
