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

(assert (= (music/lerp 2 4 -1) 0))
(assert (= (music/lerp 900 3000 0.5) 1950))

(defn rejects [f]
  (assert (try (do (f) false) ([_] true)) "expected a music API error"))

(each bad [0 -1 math/inf (/ 0 0) "120"]
  (rejects (fn [] (music/seconds bad 1)))
  (rejects (fn [] (music/bars 120 1 4 bad))))
(each bad [math/inf (/ 0 0) "1"]
  (rejects (fn [] (music/seconds 120 bad))))
(rejects (fn [] (music/degree 60 [] 0)))
(rejects (fn [] (music/degree 60 minor 0.5)))
(print "OK: Janet musical time, scales, chords and interpolation")
