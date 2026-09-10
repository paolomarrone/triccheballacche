(import ./music :as music-test)
(import ./pattern :as pattern-test)
(import ../lib/music)
(import ../lib/pattern :as p)

(defn rejects [f]
  (assert (try (do (f) false) ([_] true)) "expected an API error"))

# Metadata checks run before any native instance is allocated.
(def metadata-param {:id "test" :direction "input" :minimum 0 :maximum 1 :defaultValue 0.5})
(each parameters [[metadata-param metadata-param]
                  [(merge metadata-param {:defaultValue 2})]
                  [(merge metadata-param {:integer true})]
                  [(merge metadata-param {:direction "wrong"})]
                  [(merge metadata-param {:id ""})]
                  (map |(merge metadata-param {:id (string $)}) (range 65))]
  (rejects (fn [] (perone/parameters parameters))))
(def bypass ((perone/parameters [{:id "bypass" :direction "input" :isBypass true}]) 0))
(assert (= [(bypass :min) (bypass :max) (bypass :default) (bypass :integer)] [0 1 0 true]))
(rejects (fn [] (perone/value bypass 0.5)))
(def path "build/fixture.perone")
(def synth (daw/plugin path {:gain 0.25}))
(def filter (daw/plugin "build/effect.perone"))
(rejects (fn [] (daw/end 61))) # Unconnected plugins are never silently discarded.
(def track (daw/track synth {:effects [filter]}))
(def master (daw/master))
(def nan (/ 0 0))
(def gain ((daw/info synth) 1))
(assert (= [(gain :name) (gain :unit) (gain :min) (gain :max) (gain :default) (gain :integer)]
           [:gain "linear" 0 1 0.5 false]))
(assert (= (((daw/info track) 0) :name) :gain))
(assert (= (length (daw/info synth)) 3))
(assert (= (((daw/info synth) 0) :direction) :output))
(assert (= (((daw/info synth) 0) :name) :meter))
(assert (= (gain :map) :linear))
(assert (= (get-in (daw/product synth) [:parameters 1 :id]) "gain"))
(assert (= ((daw/product synth) :bundleName) "fixture"))
(assert (find |($ :scale-points) (daw/info synth)))
(rejects (fn [] (put gain :max 999999))) # Metadata cannot mutate validation rules.
(rejects (fn [] (put (daw/product synth) :parameters [])))
(rejects (fn [] (daw/param synth 0 :meter -12)))
(rejects (fn [] (daw/plugin path {:meter -12})))
(each f [
  (fn [] (apply daw/plugin []))
  (fn [] (daw/plugin "build/nonexistent.so"))
  (fn [] (daw/plugin path {:typo 1}))
  (fn [] (daw/plugin path {:gain -1}))
  (fn [] (daw/plugin path {:mode 1.5}))
  (fn [] (daw/plugin path {:mode 1.000000001}))
  (fn [] (daw/plugin path {:gain nan}))
  (fn [] (daw/plugin path {100 1}))
  (fn [] (daw/track synth)) # An instance cannot have two owners.
  (fn [] (daw/track synth {:gian 1}))
  (fn [] (daw/track synth {:gain nan}))
  (fn [] (daw/track synth {:pan 2}))
  (fn [] (daw/track filter)) # An effect is not a source.
  (fn [] (daw/master))
  (fn [] (daw/note track 0 1 60))
  (fn [] (daw/note -1 0 1 60))
  (fn [] (daw/note synth 0 0 60))
  (fn [] (daw/note synth nan 1 60))
  (fn [] (daw/note synth 3600 1 60))
  (fn [] (daw/note synth 0 1 128))
  (fn [] (daw/note synth 0 1 60.5))
  (fn [] (daw/note synth 0 1 60 0))
  (fn [] (daw/param synth 0 :gain 1.1))
  (fn [] (daw/param synth 0 :gain 1.000000001))
  (fn [] (daw/param synth 0 :gain -1))
  (fn [] (daw/param track 0 :gain -1))
  (fn [] (daw/param track 0 :gain (/ 1 0)))
  (fn [] (daw/param track 0 :typo 1))
  (fn [] (daw/param synth 0 :mode 2.5))
  (fn [] (daw/param synth -1 :gain 0.5))
  (fn [] (daw/end 0))
  (fn [] (daw/end 3601))
  (fn [] (daw/end 61 {:format :bad}))
  (fn [] (daw/end 61 {:normalise 0.9}))
  (fn [] (daw/end 61 {:normalize 2}))]
  (rejects f))
(daw/note synth 0 60 60)
# One minute at 50 Hz, beyond the former 2048-event limit.
(daw/schedule 0 60
  (p/map |[:param synth :gain $] (p/curve 60 3000 |(music/lerp 0.25 0.75 $))))
(daw/param filter 1 :gain 0.5)
(daw/param track 2 :gain 0.5)
(daw/param track 3 :pan 0.75)
(daw/param master 4 :gain 0.8)
(gccollect)
(rejects (fn [] (daw/end 59)))
(daw/end 61)
(each f [(fn [] (daw/end 61)) (fn [] (daw/plugin path))
         (fn [] (daw/note synth 0 1 60)) (fn [] (daw/param track 0 :gain 1))]
  (rejects f))
