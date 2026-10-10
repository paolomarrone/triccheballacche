# Native VST3 example. Install Surge XT and Surge XT Effects, or set VST3_PATH.
(import ../lib/pattern :as p)

(def synth (daw/plugin :surge {:format :vst3 :id "ABCDEF019182FAEB566D624153675854"}))
(def effect (daw/plugin :effect {:format :vst3 :id "ABCDEF019182FAEB566D624153465854"}))
(daw/output (daw/track synth {:name "Surge" :effects [effect] :gain 0.25}))
(daw/tempo 128)

(def notes (p/steps 0.5 [36 43 48 nil 39 46 51 43]))
(def part (p/map |[:note synth $ 105] notes))
(daw/score (p/loop part) {:duration 24})
