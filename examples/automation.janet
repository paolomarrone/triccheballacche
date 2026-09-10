# A dry sustained note, a real insert effect, and one minute of automation.
(import ../lib/music)
(import ../lib/pattern :as p)

(def synth (daw/plugin "plugins/synth_mono/build/plugin.perone" {:vco1_wave 2 :volume 60}))
(def filter (daw/plugin "plugins/tibia_test/build/plugin.perone" {:cutoff 600}))
(def track (daw/track synth {:effects [filter] :gain 0.4 :pan -0.8}))
(def curve (p/curve 60 3000 identity))
# At 60 BPM one beat is one second. Values and timing remain independent.
(daw/schedule 0 60
  (p/parallel [
    (p/events 60 [[0 60 [:note synth 48 100]] [30 30 [:param track :gain 0.2]]])
    (p/map |[:param filter :cutoff (music/lerp 600 6000 $)] curve)
    (p/map |[:param track :pan (music/lerp -0.8 0.8 $)] curve)]))
(daw/end 61)
