# The same musical data, repeated, reversed, transposed and stretched.
(import ../lib/music)
(import ../lib/pattern :as p)

(def root (or (os/getenv "BRICKWORKS_PERONE") "../brickworks/build/perone"))
(def synth (daw/plugin (string root "/synth_mono/build/bw_example_synth_mono.perone")
  {:volume 65 :vco1_wave 2 :vcf_resonance 12 :vca_attack 8 :vca_release 100}))
(daw/track synth {:gain 0.4})

(def motif (p/steps 0.5 [60 nil 64 67]))
(def theme (p/serial [motif (p/reverse motif) (p/map |(+ $ 12) motif) (p/stretch 2 motif)]))
(def cutoff (p/curve (theme :length) 128 |(music/lerp 400 4000 $)))
(def score
  (p/parallel [(p/map |[:note synth $ 100] theme)
               (p/map |[:param synth :vcf_cutoff $] cutoff)]))
(def end (daw/schedule 0 112 score))
(daw/end (+ end 1) {:format :pcm16 :normalize 0.94})
