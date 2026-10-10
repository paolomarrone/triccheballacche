(def synth (daw/plugin {:format :lv2 :path "build/test/fixture.lv2" :id "urn:triccheballacche:test:lv2:synth"}
  {:gain 0.5}))
(def effect (daw/plugin {:format :lv2 :path "build/test/fixture.lv2" :id "urn:triccheballacche:test:lv2:effect"}
  {:gain 1 :lag 1}))
(daw/output (daw/mix [synth (daw/through synth effect)]))
(daw/note synth 0.01 0.1 60 100)
(daw/end 0.2)
