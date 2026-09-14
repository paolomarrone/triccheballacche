# One rhythm section, two parallel paths. MIDI and audio composition stay separate.
(import ../lib/pattern :as p)

(def kick (daw/plugin "plugins/drums/build/plugin.perone" {:seed 2026}))
(def bass (daw/plugin "plugins/synth_mono/build/plugin.perone"
  {:vco1_wave 2 :vcf_cutoff 800 :vcf_resonance 30
   :vca_attack 3 :vca_decay 140 :vca_sustain 20 :vca_release 70}))
(def drum-track (daw/track kick {:name "Kick" :gain 0.9}))
(def bass-track (daw/track bass {:name "Bass" :gain 0.35}))
(def rhythm (daw/track (daw/mix [drum-track bass-track]) {:name "Rhythm"}))

(defn distort [signal drive]
  (daw/through signal
    (daw/plugin "plugins/shape/build/plugin.perone" {:drive drive :level 0.6})))

(def dry (daw/mix [rhythm]))
(def echo (daw/plugin "plugins/echo/build/plugin.perone"
  {:time1 214 :time2 429 :time3 643 :level1 0.35 :level2 0.2 :level3 0.1}))
(def wet (daw/track (daw/through (distort rhythm 4) echo) {:name "Parallel" :gain 0}))
(def master (daw/master (daw/mix [dry wet]) {:gain 0.7}))
(daw/output master)

(defn voice [node pattern]
  (p/map |[:note node $ 100] pattern))
(def bar
  (p/parallel
    [(voice kick (p/steps 1 [0 0 0 0]))
     (voice bass (p/steps 0.25 [36 nil 36 48 nil 43 36 nil 36 46 nil 48 43 nil 34 nil]))]))
(daw/schedule 0 140 (p/serial (seq [i :range [0 16]] bar)))

# Eight bars dry, then crossfade to the processed path over four bars.
# The effect runs throughout, so its echo history is already present when it opens.
(def fade (p/curve 16 512 |$))
(daw/schedule (/ (* 32 60) 140) 140
  (p/parallel [(p/map |[:param dry :gain (- 1 $)] fade)
               (p/map |[:param wet :gain $] fade)]))
(daw/end 30)
