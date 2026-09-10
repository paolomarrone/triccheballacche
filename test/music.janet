# This module runs with only Janet's core environment: no daw/* bindings or plugins.
(import ../lib/music)

(assert (= (music/seconds 120 3) 1.5))
(assert (= (music/seconds 60 -0.5) -0.5))
(assert (= (music/bars 120 2) 4))
(assert (= (music/bars 120 2 7 8) 3.5))
(assert (= (music/bars 90 0.5 3 4) 1))
(def minor [0 2 3 5 7 8 11])
(assert (deep= (map |(music/degree 60 minor $) [-8 -7 -1 0 6 7 8]) @[47 48 59 60 71 72 74]))
(assert (deep= (music/chord 60 [0 7 3 12]) @[60 67 63 72]))
(assert (deep= (music/chord 60 []) @[]))
(assert (deep= minor [0 2 3 5 7 8 11]))

(def events @[])

(defn collect [t value] (array/push events [t value]))

(assert (= (music/sequence 2 0.25 [60 nil 0 [64 67]] collect) 3))
(assert (deep= events @[[2 60] [2.5 0] [2.75 [64 67]]]))
(array/clear events)
(assert (= (music/sequence 3 0.25 [] collect) 3))
(assert (= (length events) 0))
# Sequences compose by returning the next start, including trailing rests.
(def next (music/sequence 0 0.5 [:kick nil] collect))
(music/sequence next 0.5 [:snare] collect)
(assert (deep= events @[[0 :kick] [1 :snare]]))

(array/clear events)
(assert (= (music/curve 2 1 4 |(music/lerp 1 0 (* $ $)) collect) 3))
(assert (deep= events @[[2 1] [2.25 0.9375] [2.5 0.75] [2.75 0.4375] [3 0]]))
(array/clear events)
(music/curve 0 60 3000 identity collect)
(assert (= (length events) 3001))
(assert (deep= (first events) [0 0]))
(assert (deep= (last events) [60 1]))
(assert (= (music/lerp 2 4 -1) 0))

# A part is a start -> end function. Notes and controls share relative time;
# pitch and tempo are ordinary arguments, captured with partial.
(defn phrase [transpose step start]
  (music/curve start step 1 identity
    (fn [t value] (collect t [:control value])))
  (music/sequence start step [60 nil 64 nil]
    (fn [t pitch] (collect t [:note (+ pitch transpose) (* 2 step)]))))

(array/clear events)
(def tune (partial phrase 0 0.5))
(def octave (partial phrase 12 0.25))

(defn rest [start] (+ start 1))

(defn voices [start] (music/parallel start [tune octave rest]))

(assert (= (music/serial 3 [voices rest octave]) 7))
(assert (deep= events @[
  [3 [:control 0]] [3.5 [:control 1]] [3 [:note 60 1]] [4 [:note 64 1]]
  [3 [:control 0]] [3.25 [:control 1]] [3 [:note 72 0.5]] [3.5 [:note 76 0.5]]
  [6 [:control 0]] [6.25 [:control 1]] [6 [:note 72 0.5]] [6.5 [:note 76 0.5]]]))

# Repeat the same function without accumulating offsets or changing its state.
(array/clear events)
(assert (= (music/serial 0 [octave octave]) 2))
(assert (deep= (array/slice events 4) (map |[(+ 1 ($ 0)) ($ 1)] (array/slice events 0 4))))
# Parallel composition waits for the longest part, including silent spans.
(assert (= (music/parallel 2 [|(music/serial $ [rest rest rest]) rest]) 5))
# Empty parts have zero duration; negative starts are useful for relative placement.
(each compose [music/serial music/parallel]
  (assert (= (compose -2 []) -2))
  (assert (= (compose -2 [identity]) -2)))
# A part declares its span, which can be shorter than a note's release or gate.
(array/clear events)
(defn overlap [start]
  (collect start [:note 60 3])
  (+ start 1))

(assert (= (music/serial 0 [overlap overlap]) 2))
(assert (deep= events @[[0 [:note 60 3]] [1 [:note 60 3]]]))

(defn rejects [f]
  (assert (try (do (f) false) ([_] true)) "expected a music API error"))

(each bad [0 -1 math/inf (/ 0 0) "120"]
  (rejects (fn [] (music/seconds bad 1)))
  (rejects (fn [] (music/bars 120 1 4 bad)))
  (rejects (fn [] (music/sequence 0 bad [60] collect)))
  (rejects (fn [] (music/curve 0 bad 4 identity collect)))
  (rejects (fn [] (music/curve 0 1 bad identity collect))))
(rejects (fn [] (music/degree 60 [] 0)))
(rejects (fn [] (music/degree 60 minor 0.5)))
(rejects (fn [] (music/curve 0 1 2.5 identity collect)))
(rejects (fn [] (music/curve 0 1 2147483648 identity collect)))
# Callback errors propagate to the score.
(rejects (fn [] (music/sequence 0 1 [60] (fn [&] (error "callback")))))
(rejects (fn [] (music/curve 0 1 4 (fn [_] (error "shape")) collect)))
(each compose [music/serial music/parallel]
  (each bad [math/inf (/ 0 0) "0"]
    (rejects (fn [] (compose bad []))))
  (each bad [nil "1" math/inf (/ 0 0) 0]
    (rejects (fn [] (compose 1 [(fn [_] bad)]))))
  (var reached false)
  (rejects (fn [] (compose 0 [(fn [_] (error "part")) (fn [t] (set reached true) t)])))
  (assert (not reached)))
(print "OK: Janet musical time, pitch, curves and serial/parallel parts")
