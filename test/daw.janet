(import ./music :as music-test)
(import ../lib/music)

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
(def path "plugins/synth_mono/build/plugin.perone")
(def synth (daw/plugin path {:vcf_cutoff 500}))
(def filter (daw/plugin "plugins/tibia_test/build/plugin.perone"))
(rejects (fn [] (daw/end 61))) # Unconnected plugins are never silently discarded.
(def track (daw/track synth {:effects [filter]}))
(def master (daw/master))
(def nan (/ 0 0))
(def cutoff ((daw/info synth) 26))
(assert (= [(cutoff :name) (cutoff :unit) (cutoff :min) (cutoff :max) (cutoff :default) (cutoff :integer)]
           [:vcf_cutoff "hz" 20 20000 20000 false]))
(assert (= (((daw/info track) 0) :name) :gain))
(assert (= (length (daw/info synth)) 39))
(assert (= (((daw/info synth) 38) :direction) :output))
(assert (= (((daw/info synth) 38) :name) :level))
(assert (= (cutoff :map) :logarithmic))
(assert (= (get-in (daw/product synth) [:parameters 26 :id]) "vcf_cutoff"))
(assert (= ((daw/product synth) :bundleName) "bw_example_synth_mono"))
(assert (find |($ :scale-points) (daw/info synth)))
(rejects (fn [] (put cutoff :max 999999))) # Metadata cannot mutate validation rules.
(rejects (fn [] (put (daw/product synth) :parameters [])))
(rejects (fn [] (daw/param synth 0 :level -12)))
(rejects (fn [] (daw/plugin path {:level -12})))
(each f [
  (fn [] (apply daw/plugin []))
  (fn [] (daw/plugin "build/nonexistent.so"))
  (fn [] (daw/plugin path {:typo 1}))
  (fn [] (daw/plugin path {:vca_attack -1}))
  (fn [] (daw/plugin path {:vco1_wave 1.5}))
  (fn [] (daw/plugin path {:vco1_wave 1.000000001}))
  (fn [] (daw/plugin path {:vcf_cutoff nan}))
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
  (fn [] (daw/param synth 0 :vcf_cutoff 20001))
  (fn [] (daw/param synth 0 :vcf_cutoff 20000.00001))
  (fn [] (daw/param synth 0 :vca_attack -1))
  (fn [] (daw/param track 0 :gain -1))
  (fn [] (daw/param track 0 :gain (/ 1 0)))
  (fn [] (daw/param track 0 :typo 1))
  (fn [] (daw/param synth 0 :vco1_wave 2.5))
  (fn [] (daw/param synth -1 :vcf_cutoff 1000))
  (fn [] (daw/end 0))
  (fn [] (daw/end 3601))
  (fn [] (daw/end 61 {:format :bad}))
  (fn [] (daw/end 61 {:normalise 0.9}))
  (fn [] (daw/end 61 {:normalize 2}))]
  (rejects f))
(daw/note synth 0 60 60)
# One minute at 50 Hz, beyond the former 2048-event limit.
(music/curve 0 60 3000 |(music/lerp 500 3500 $)
  (fn [t value] (daw/param synth t :vcf_cutoff value)))
(daw/param filter 1 :cutoff 4000)
(daw/param track 2 :gain 0.5)
(daw/param track 3 :pan 0.75)
(daw/param master 4 :gain 0.8)
(gccollect)
(rejects (fn [] (daw/end 59)))
(daw/end 61)
(each f [(fn [] (daw/end 61)) (fn [] (daw/plugin path))
         (fn [] (daw/note synth 0 1 60)) (fn [] (daw/param track 0 :gain 1))]
  (rejects f))
