# A final partial audio quantum, MIDI gates, and stateful mono effects on stereo.
(def synth (daw/plugin "build/test/fixture.perone" {:gain 0.25 :mode 1}))
(def effect (daw/plugin "build/test/effect.perone" {:gain 1 :decay 0.15}))
(def track (daw/track synth {:effects [effect] :gain 4}))
(daw/output track) # Preserve float headroom before the audio output.
(daw/note synth 0 0.02 60)
(daw/note synth 0.03 0.01 72)
(daw/param synth 0.005 :gain 0.75)
(daw/param effect 0.013 :gain 0.25)
(daw/param track 0.021 :pan -0.25)
(daw/end 0.05003)
