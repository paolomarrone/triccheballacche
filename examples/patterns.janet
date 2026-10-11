# Brickworks instruments, stereo effects, patterns and automation.
(import ../lib/music)
(import ../lib/pattern :as p)

(def root (or (os/getenv "BRICKWORKS_PERONE") "build/library/brickworks"))
(defn bundle [name] (string root "/" name "/build/bw_example_" name ".perone"))

(def synth (daw/plugin (bundle "synth_poly")
  {:volume 65 :vco1_wave 2 :vcf_resonance 12 :vca_attack 8 :vca_release 100}))
(def comp (daw/plugin (bundle "fx_comp"))) # Optional sidechain stays disconnected.
(def pan (daw/plugin (bundle "fx_pan")))
(def reverb (daw/plugin (bundle "fx_reverb") {:wet 20}))
(def track (daw/track synth {:effects [comp pan reverb] :gain 0.3}))
(daw/output track)

(def motif (p/steps 0.5 [60 nil 64 67]))
(def theme (p/serial [motif (p/reverse motif) (p/map |(+ $ 12) motif) (p/stretch 2 motif)]))
(def notes (p/parallel [theme (p/map |(+ $ 7) theme)]))
(def curve (p/curve (theme :length) 128 identity))

# One curve drives instrument, effect and track parameters at the same beat positions.
(def score
  (p/parallel [(p/map |[:note synth $ 100] notes)
               (p/map |[:param synth :vcf_cutoff (music/lerp 400 4000 $)] curve)
               (p/map |[:param reverb :wet (music/lerp 20 45 $)] curve)
               (p/map |[:param track :pan (music/lerp -0.8 0.8 $)] curve)]))
(def end (daw/schedule 0 112 score))
(daw/end (+ end 2) {:format :pcm16 :normalize 0.94})
