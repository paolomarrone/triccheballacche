# A dry sustained note, a real insert effect, and one minute of automation.
(import ../lib/music)

(def synth (daw/plugin "plugins/synth_mono/build/plugin.perone" {:vco1_wave 2 :volume 60}))
(def filter (daw/plugin "plugins/tibia_test/build/plugin.perone" {:cutoff 600}))
(def track (daw/track synth {:effects [filter] :gain 0.4 :pan -0.8}))
(daw/note synth 0 60 48)
(music/curve 0 60 3000 identity
  (fn [t x]
    (daw/param filter t :cutoff (music/lerp 600 6000 x))
    (daw/param track t :pan (music/lerp -0.8 0.8 x))))
(daw/param track 30 :gain 0.2)
(daw/end 61)
