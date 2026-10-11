# Install Surge XT and Surge XT Effects in the selected native format.
(import ../lib/pattern :as p)

(def format :clap) # Change to :lv2 or :vst3.
(def [synth-id effect-id]
  (case format
    :clap ["org.surge-synth-team.surge-xt"
           "org.surge-synth-team.surge-xt-fx"]
    :lv2 ["https://surge-synthesizer.github.io/lv2/surge-xt"
          "https://surge-synth-team.org//plugins/Surge_XT_Effects"]
    :vst3 ["ABCDEF019182FAEB566D624153675854"
           "ABCDEF019182FAEB566D624153465854"]
    (error "Use :clap, :lv2 or :vst3")))
(def synth (daw/plugin :surge {:format format :id synth-id}))
(def effect (daw/plugin :effect {:format format :id effect-id}))
(daw/output (daw/track synth {:name "Surge" :effects [effect] :gain 0.25}))
(daw/tempo 128)

(def notes (p/steps 0.5 [36 43 48 nil 39 46 51 43]))
(def part (p/map |[:note synth $ 105] notes))
(daw/score (p/loop part) {:duration 24})
