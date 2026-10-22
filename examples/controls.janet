# Three Brickworks web UIs, with the same controls on native and Wasm.
# Stop the timeline to adjust parameters without automation taking over.
(import ../lib/pattern :as p)

(def root (or (os/getenv "BRICKWORKS_PERONE") "build/library/brickworks"))
(defn bw [name params]
  (daw/plugin (string root "/" name "/build/bw_example_" name ".perone") params))

(def synth (bw "synth_mono"
  {:volume 45 :vco1_wave 2 :vco1_pw 35 :vco2_level 30 :vco2_fine 7
   :vcf_cutoff 6000 :vcf_contour 15 :vcf_decay 180 :vcf_sustain 30
   :vca_attack 4 :vca_decay 180 :vca_sustain 35 :vca_release 120}))
(def filter (bw "fx_svf" {:cutoff 2400 :q 1.4}))
(def eq (bw "fx_eq_3band" {:ls_gain 2 :peak_cutoff 900 :peak_gain -4 :hs_gain -2}))
(daw/output (daw/track synth {:effects [filter eq] :gain 0.25}))
(daw/tempo 120)

(def notes (p/map |[:note synth $ 100]
  (p/steps 0.5 [36 nil 43 48 39 46 43 nil 36 48 46 43 39 nil 34 43])))
(def motion (p/curve 16 256 |(* 0.5 (- 1 (math/cos (* 2 math/pi $))))))
(daw/score
  (p/parallel [(p/loop notes)
               (p/loop (p/map |[:param filter :cutoff (* 400 (math/pow 12 $))] motion))
               (p/loop (p/map |[:param eq :peak_gain (- (* 10 $) 7)] motion))]))
# Add {:duration 30} to daw/score for a finite WAV export.
