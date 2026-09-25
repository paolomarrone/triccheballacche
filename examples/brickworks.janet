# Uses precompiled Brickworks C bundles directly, including stereo plugins.
(import ../lib/pattern :as p)

(def root (or (os/getenv "BRICKWORKS_PERONE") "build/library/brickworks"))

(defn bundle [name] (string root "/" name "/build/bw_example_" name ".perone"))

(def synth (daw/plugin (bundle "synth_poly")))
(def comp (daw/plugin (bundle "fx_comp"))) # Optional sidechain stays disconnected.
(def pan (daw/plugin (bundle "fx_pan")))
(def reverb (daw/plugin (bundle "fx_reverb")))
(daw/output (daw/track synth {:effects [comp pan reverb] :gain 0.3}))
(defn chord [pitches]
  (p/events 2 (map |[0 1.5 $] pitches)))

(daw/schedule 0 60
  (p/map |[:note synth $ 100] (p/serial [(chord [60 64 67]) (chord [62 65 69])])))
(daw/end 5)
