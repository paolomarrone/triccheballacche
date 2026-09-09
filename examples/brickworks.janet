# Uses precompiled Brickworks bundles directly, including C++ and stereo plugins.
(def root (or (os/getenv "BRICKWORKS_PERONE") "../brickworks/build/perone"))

(defn bundle [name] (string root "/" name "/build/bw_example_" name ".perone"))

(def synth (daw/plugin (bundle "synthpp_poly")))
(def comp (daw/plugin (bundle "fx_comp"))) # Optional sidechain stays disconnected.
(def pan (daw/plugin (bundle "fx_pan")))
(def reverb (daw/plugin (bundle "fxpp_reverb")))
(daw/track synth {:effects [comp pan reverb] :gain 0.3})
(each pitch [60 64 67] (daw/note synth 0 1.5 pitch))
(each pitch [62 65 69] (daw/note synth 2 1.5 pitch))
(daw/end 5)
