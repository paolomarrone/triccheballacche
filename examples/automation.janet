# A dry sustained note, a real insert effect, and one minute of automation.
(def synth (daw/plugin "examples/synth_mono/plugin.so" {:vco1_wave 2 :volume 60}))
(def filter (daw/plugin "examples/tibia_test/plugin.so" {:cutoff 600}))
(def track (daw/track synth {:effects [filter] :gain 0.4 :pan -0.8}))
(daw/note synth 0 60 48)
(for i 0 3001
  (def t (/ i 50))
  (daw/param filter t :cutoff (+ 600 (* 5400 (/ i 3000))))
  (daw/param track t :pan (+ -0.8 (* 1.6 (/ i 3000)))))
(daw/param track 30 :gain 0.2)
(daw/end 61)
