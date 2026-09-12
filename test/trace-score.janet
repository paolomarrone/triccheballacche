# Source-tracing fixture: valid unchanged both with and without the tracer.
(import ../lib/pattern :as p)
(import ./trace-helper :as external)
(def synth (daw/plugin "build/fixture.perone" {:gain 0.25}))
(daw/track synth)

(def first (p/steps 0.25 [60 nil 64])) # first origin
(def second (p/steps 0.25 [60 nil 64])) # equal value, distinct origin
(assert (= first second))
(defn tail-helper []
  (p/steps 0.5 [67])) # eliminated helper frame
(def tail (tail-helper)) # surviving tail call site
(defn helper []
  (def result (p/steps 0.5 [69])) # helper origin
  result)
(def ordinary (helper)) # ordinary call site
(def points (p/curve 2 2 identity)) # controls origin
(var calls 0)
(def notes
  (p/map (fn [pitch] (++ calls) [:note synth pitch 100])
    (p/serial [first second (p/reverse first) (p/stretch 2 tail) ordinary])))
(assert (= calls 8)) # Tracing must not re-evaluate the user's mapping function.
(gccollect)
(daw/schedule 0 120
  (p/parallel [notes (p/map |[:param synth :gain (* $ 0.5)] points)]))
(daw/note synth 2 0.2 72) # direct note
(daw/param synth 2.1 :gain 0.3) # direct parameter
# A literal pattern has no captured construction; scheduling is the honest fallback.
(daw/schedule 2.3 60 {:length 0.2 :events [[0 0.2 [:note synth 74 100]]]}) # fallback
(defn append-note [items t pitch]
  (array/push items [t (+ t 0.04) [:note synth pitch 100]])) # event producer
(defn phrase []
  (def items @[])
  (append-note items 0 77) # first producer call
  (append-note items 0.2 79) # second producer call
  (external/fill items synth) # imported producer call
  (p/events 0.4 items)) # collection happens later
(daw/schedule 2.6 60 (phrase)) # constructed phrase
(daw/end 3.1)
