# Native CLAP example. Install Surge XT and Surge XT Effects, or set CLAP_PATH.
(import ../lib/pattern :as p)

(def synth (daw/plugin :surge {:format :clap :id "org.surge-synth-team.surge-xt"}))
(def effect (daw/plugin :effect {:format :clap :id "org.surge-synth-team.surge-xt-fx"}))
(daw/output (daw/track synth {:name "Surge" :effects [effect] :gain 0.25}))
(daw/tempo 128)

(def notes (p/steps 0.5 [36 43 48 nil 39 46 51 43]))
(def part (p/map |[:note synth $ 105] notes))
(daw/score (p/loop part) {:duration 24})
