(def synth (daw/plugin :synth {:format :clap :path "build/test/fixture.clap" :id "org.triccheballacche.test.synth"}
  {:987654321 0.5}))
(def fx (daw/plugin :fx {:format :clap :path "build/test/fixture.clap" :id "org.triccheballacche.test.effect"}
  {:987654321 0.5}))
(def stepped (get-in (daw/product synth) [:parameters 126]))
(assert (= (stepped :defaultValue) 1))
(assert (= (stepped :initialValue) -1))
(daw/output (daw/track synth {:effects [fx]}))
(daw/note synth 0.01 0.1 60 100)
(daw/param synth 0.04 :987654321 0.75)
(daw/param synth 0.05 :1007 0.9)
(daw/end 0.2)
