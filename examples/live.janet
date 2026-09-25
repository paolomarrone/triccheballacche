# Change the notes or cutoff and press Run. The next revision enters on a four-beat grid.
# Play resumes; Stop keeps the position and instruments and cancels a pending revision.
(import ../lib/pattern :as p)

(def bass (daw/plugin :bass "plugins/synth_mono/build/plugin.perone"
  {:volume 35 :vcf_cutoff 1200}))
(def echo (daw/plugin :echo "plugins/echo/build/plugin.perone"))
(daw/output (daw/track bass {:effects [echo] :gain 0.4}))
(daw/tempo 132)

(def notes (p/map |[:note bass $ 100] (p/steps 0.5 [36 nil 36 43 39 nil 46 43])))
(def cutoff (p/map |[:param bass :vcf_cutoff $]
  (p/events 6 [[0 0 500] [2 2 1800] [4 4 3500]])))
(daw/score (p/parallel [(p/loop notes) (p/loop cutoff)]))
# To export a finite excerpt, add {:duration 30} as the second argument to daw/score.
