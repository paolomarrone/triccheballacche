# Pure Janet: no host, plugins, tempo or global event collector.
(import ../lib/pattern :as p)

(defn rejects [f]
  (assert (try (do (f) false) ([_] true)) "expected a pattern error"))

(def empty (p/events 0 []))
(def rest (p/events 2 []))
(def motif (p/steps 0.5 [60 nil 64 nil]))
(assert (deep= motif {:length 2 :events [[0 0.5 60] [1 1.5 64]]}))
(assert (deep= (p/steps 1 [nil nil]) rest))
(assert (deep= (p/steps 1 []) empty))
(assert (deep= (p/steps 1 [false 0]) (p/events 2 [[0 1 false] [1 2 0]])))

# Freeze nested musical values, not just the event list.
(def input @[@[0 1 @{:chord @[60 64 67]}]])
(def frozen (p/events 2 input))
(put-in input [0 2 :chord 0] 99)
(array/push input [1 2 72])
(assert (= (get-in frozen [:events 0 2 :chord 0]) 60))
(assert (= (length (frozen :events)) 1))
(rejects (fn [] (put frozen :length 3)))
(rejects (fn [] (put-in frozen [:events 0 2 :chord 0] 99)))

# Values may be chords, control packets or anything else; map keeps the rhythm.
(def octave (p/map |(+ $ 12) motif))
(assert (deep= octave (p/events 2 [[0 0.5 72] [1 1.5 76]])))
(assert (deep= motif (p/steps 0.5 [60 nil 64 nil])))
(assert (deep= (p/map identity frozen) frozen))
(assert (= (length ((p/map (fn [_] nil) motif) :events)) 2)) # Only steps treats nil as a rest.

(def controls (p/curve 2 4 |(* $ $)))
(assert (deep= controls
  (p/events 2 [[0 0 0] [0.5 0.5 0.0625] [1 1 0.25] [1.5 1.5 0.5625] [2 2 1]])))
(def combined (p/parallel [motif controls]))
(assert (deep= (combined :events) (tuple ;(motif :events) ;(controls :events))))
(assert (deep= (p/serial [motif rest octave])
  (p/events 6 [[0 0.5 60] [1 1.5 64] [4 4.5 72] [5 5.5 76]])))
(assert (deep= (p/parallel [motif rest (p/stretch 2 octave)])
  (p/events 4 [[0 0.5 60] [1 1.5 64] [0 1 72] [2 3 76]])))

# Points at the boundary survive; overhangs become pickups on reversal.
(def crossing (p/events 2 [[-0.5 0.5 :pickup] [1.5 3 :tail] [0 0 :first] [2 2 :last]]))
(assert (deep= (p/reverse crossing)
  (p/events 2 [[1.5 2.5 :pickup] [-1 0.5 :tail] [2 2 :first] [0 0 :last]])))
(assert (deep= (p/serial [rest crossing])
  (p/events 4 [[1.5 2.5 :pickup] [3.5 5 :tail] [2 2 :first] [4 4 :last]])))
(assert (deep= (p/stretch 0.5 crossing)
  (p/events 1 [[-0.25 0.25 :pickup] [0.75 1.5 :tail] [0 0 :first] [1 1 :last]])))
(assert (deep= (p/reverse controls)
  (p/events 2 [[2 2 0] [1.5 1.5 0.0625] [1 1 0.25] [0.5 0.5 0.5625] [0 0 1]])))

# Algebraic identities, including stable order at coincident boundaries.
(each pattern [empty rest motif controls combined crossing frozen]
  (assert (deep= (p/reverse (p/reverse pattern)) pattern))
  (assert (deep= (p/stretch 0.5 (p/stretch 2 pattern)) pattern))
  (each compose [p/serial p/parallel]
    (assert (deep= (compose []) empty))
    (assert (deep= (compose [empty pattern empty]) pattern))
    (assert (deep= (compose [(compose [pattern controls]) motif])
                   (compose [pattern (compose [controls motif])])))))
(assert (deep= (p/stretch 2 combined) (p/parallel [(p/stretch 2 motif) (p/stretch 2 controls)])))
(assert (deep= (p/map |[:value $] combined)
               (p/parallel [(p/map |[:value $] motif) (p/map |[:value $] controls)])))
(def points (p/events 0 [[0 0 :a] [0 0 :b]]))
(assert (deep= (p/reverse points) points))
(assert (deep= (p/serial [points points]) (p/events 0 [[0 0 :a] [0 0 :b] [0 0 :a] [0 0 :b]])))

# Durations are continuous numbers; thirds and fifths have no special time grid.
(def uneven (p/parallel [(p/steps (/ 1 3) [1 2 3]) (p/steps (/ 1 5) [4 5 6 7 8])]))
(assert (= (uneven :length) 1))
(eachp [i event] (uneven :events)
  (def back (((p/reverse (p/reverse uneven)) :events) i))
  (assert (< (math/abs (- (event 0) (back 0))) 1e-14))
  (assert (< (math/abs (- (event 1) (back 1))) 1e-14)))

(each bad [nil "1" -1 math/inf (/ 0 0)]
  (rejects (fn [] (p/events bad []))))
(each bad [nil "1" 0 -1 math/inf (/ 0 0)]
  (rejects (fn [] (p/steps bad [])))
  (rejects (fn [] (p/stretch bad motif)))
  (rejects (fn [] (p/curve bad 4 identity)))
  (rejects (fn [] (p/curve 1 bad identity))))
(each bad [0.5 2147483648]
  (rejects (fn [] (p/curve 1 bad identity))))
(each bad [nil {} [1 2] [[0 1]] [[0 1 60 :extra]] [[1 0 60]]
           [[0 math/inf 60]] [[(/ 0 0) 1 60]]]
  (rejects (fn [] (p/events 1 bad))))
(each bad [nil {} {:length 1 :events [[1 0 60]]} {:length 1 :events [] :extra true}]
  (rejects (fn [] (p/serial [bad])))
  (rejects (fn [] (p/parallel [bad])))
  (rejects (fn [] (p/map identity bad)))
  (rejects (fn [] (p/stretch 1 bad)))
  (rejects (fn [] (p/reverse bad))))
(rejects (fn [] (p/steps 1 nil)))
(rejects (fn [] (p/serial nil)))
(rejects (fn [] (p/parallel {})))
(rejects (fn [] (p/stretch 2 (p/events 1e308 []))))
(rejects (fn [] (p/serial [(p/events 1e308 []) (p/events 1e308 [])])))
(rejects (fn [] (p/curve 1 4 (fn [_] (error "shape")))))
(rejects (fn [] (p/map (fn [_] (error "value")) motif)))
(print "OK: immutable patterns, rests, nesting, transformations, boundaries and validation")
