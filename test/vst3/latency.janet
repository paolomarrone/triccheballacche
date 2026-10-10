(def synth (daw/plugin {:format :vst3 :path "build/test/fixture.vst3" :id "ABCDEF01222233334444555566667777"}
  {:987654321 0.5}))
(def effect (daw/plugin {:format :vst3 :path "build/test/fixture.vst3" :id "ABCDEF02222233334444555566667777"}
  {:987654321 1 :42 1}))
(daw/output (daw/mix [synth (daw/through synth effect)]))
(daw/note synth 0.01 0.1 60 100)
(daw/end 0.2)
