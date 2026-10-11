# Settings → MIDI: connect a keyboard to "keys", and optionally "keys" to an output device.
# The stream also carries the repeating phrase. Remove the note pattern to play only the keyboard.
(import ../lib/pattern :as p)

(def keys (daw/midi :keys))
(def root (or (os/getenv "BRICKWORKS_PERONE") "build/library/brickworks"))
(def synth (daw/plugin :synth (string root "/synth_poly/build/bw_example_synth_poly.perone")))
(daw/midi-through keys synth)
(daw/output (daw/track synth {:name "Keys" :gain 0.25}))
(daw/midi-output keys)
(daw/tempo 120)
(daw/score (p/loop (p/events 4
  [[0 0.3 [:note keys 48 90]]
   [0.75 1 [:note keys 55 65]]
   [1.5 1.8 [:note keys 60 80]]
   [2 2.3 [:note keys 58 95]]
   [2.75 3 [:note keys 55 70]]
   [3.5 3.8 [:note keys 51 85]]
   [0 0 [:midi keys [0xe0 0 64]]]
   [3 3 [:midi keys [0xe0 0 72]]]])))
