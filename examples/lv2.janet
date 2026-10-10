# Install Surge XT and Surge XT Effects as LV2 bundles, or set LV2_PATH.
(import ../lib/pattern :as p)

(def synth (daw/plugin :surge {:format :lv2 :id "https://surge-synthesizer.github.io/lv2/surge-xt"}))
(def effect (daw/plugin :effect {:format :lv2 :id "https://surge-synth-team.org//plugins/Surge_XT_Effects"}))
(daw/output (daw/track synth {:gain 0.3 :effects [effect]}))
(daw/tempo 128)
(daw/score (p/loop (p/map |[:note synth $ 100] (p/steps 0.5 [48 55 60 63 60 55 51 58])))
  {:duration 24})
