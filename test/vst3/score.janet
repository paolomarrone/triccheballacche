(def synth (daw/plugin :synth {:format :vst3 :path "build/test/fixture.vst3" :id "ABCDEF01222233334444555566667777"}
  {:987654321 0.5}))
(def effect (daw/plugin :effect {:format :vst3 :path "build/test/fixture.vst3" :id "ABCDEF02222233334444555566667777"}
  {:987654321 0.5}))
(daw/output (daw/track synth {:effects [effect]}))
(daw/note synth 0.01 0.1 60 100)
(daw/param synth 0.04 :987654321 0.75)
(daw/param synth 0.05 :7 2)
(daw/end 0.2)
