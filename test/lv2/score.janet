(def synth (daw/plugin :synth {:format :lv2 :path "build/test/fixture.lv2" :id "urn:triccheballacche:test:lv2:synth"}
  {:gain 0.5}))
(def effect (daw/plugin :effect {:format :lv2 :path "build/test/fixture.lv2" :id "urn:triccheballacche:test:lv2:effect"}
  {:gain 0.5}))
# Reading the same entry through distinct references must not scale ranges again.
(def again (plugin/read @{:format :lv2 :id "urn:triccheballacche:test:lv2:synth" :path "build/test/fixture.lv2"}))
(assert (= (((again :parameters) 4) :default) (* 0.25 host/sample-rate)))
(assert (= (((daw/info synth) 4) :max) (* 0.5 host/sample-rate)))
(daw/output (daw/track synth {:effects [effect]}))
(daw/note synth 0.01 0.1 60 100)
(daw/param synth 0.04 :gain 0.75)
(daw/end 0.2)
