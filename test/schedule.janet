(import ../lib/pattern :as p)

(def synth (daw/plugin "build/fixture.perone" {:gain 0.25}))
(daw/track synth)

(defn rejects [f]
  (assert (try (do (f) false) ([_] true)) "expected a schedule error"))

(def empty (p/events 0 []))
(assert (= (daw/schedule 0 120 empty) 0))
(assert (= (daw/schedule 3600 120 empty) 3600))
(each bad [0 -1 nil "120" math/inf (/ 0 0)]
  (rejects (fn [] (daw/schedule 0 bad empty))))
(each bad [-1 3601 nil "0" math/inf (/ 0 0)]
  (rejects (fn [] (daw/schedule bad 120 empty))))
(each bad [nil {} {:length math/inf :events []} {:length -1 :events []}
           {:length 1 :events nil} {:length 1 :events [[0 1]]}
           {:length 1 :events [[0 1 [:param synth :gain 0.5]]]}
           {:length 1 :events [[0 0 [:note synth 60 100]]]}
           {:length 1 :events [[0 1e-8 [:note synth 60 100]]]}
           {:length 1 :events [[0 1 [:note synth 60]]]}
           {:length 1 :events [[0 1 [:note synth 60.5 100]]]}
           {:length 1 :events [[0 1 [:note synth 60 0]]]}
           {:length 1 :events [[0 0 [:param synth :gain 2]]]}
           {:length 1 :events [[0 0 [:param synth :meter 0]]]}
           {:length 1 :events [[0 0 [:unknown synth :gain 0]]]}
           {:length 1 :events [[1 0 [:note synth 60 100]]]}
           {:length 1 :events [[0 math/inf [:note synth 60 100]]]}
           {:length 1 :events [[-1 0 [:note synth 60 100]]]}]
  (rejects (fn [] (daw/schedule 0 120 bad))))
(rejects (fn [] (daw/schedule 3599 60 (p/events 2 []))))
(rejects (fn [] (daw/schedule 3599 60 (p/events 1 [[0 2 [:note synth 60 100]]]))))

# A pickup before the origin and a gate past the nominal end, with coincident controls.
(def notes (p/map |[:note synth (+ $ 12) 100]
  (p/events 1 [[-0.25 0.25 60] [0.5 1.5 64] [1.5 1.75 67]])))
(def controls (p/map |[:param synth :gain $]
  (p/events 1 [[0.5 0.5 0.5] [0 0 0.25] [0.5 0.5 0.75] [1.5 1.5 0.5]])))
(assert (= (daw/schedule 0.5 120 (p/parallel [notes controls])) 1))
(daw/end 1.5)
