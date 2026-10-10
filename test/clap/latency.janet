(def s (daw/plugin {:format :clap :path "build/test/fixture.clap" :id "org.triccheballacche.test.synth"}
  {:987654321 0.5}))
(def f (daw/plugin {:format :clap :path "build/test/fixture.clap" :id "org.triccheballacche.test.effect"}
  {:987654321 1 :1007 0.9}))
(daw/output (daw/mix [s (daw/through s f)]))
(daw/note s 0.01 0.1 60 100)
(daw/end 0.2)
