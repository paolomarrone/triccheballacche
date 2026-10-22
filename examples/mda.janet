# MDA instruments and effects, with the same Perone bundles on desktop and Wasm.
(import ../lib/music)
(import ../lib/pattern :as p)

(def root (or (os/getenv "MDA_PERONE") "build/library/mda"))
(defn mda [name &opt parameters]
  (daw/plugin (string root "/mda" name ".perone") (or parameters {})))

(def bass (mda "JX10" {:osc_mix 0.2 :vcf_freq 0.34 :vcf_reso 0.35
  :vcf_env 0.65 :vcf_att 0 :vcf_dec 0.3 :vcf_sus 0 :vcf_rel 0.2
  :env_att 0 :env_dec 0.35 :env_sus 0.2 :env_rel 0.15 :noise 0 :glide 0.55}))
(def drive (mda "Overdrive" {:drive 0.18 :muffle 0.25 :output 0.4}))
(def bass-track (daw/track bass {:name "Rubber" :effects [drive] :gain 0.45}))

(def keys (mda "EPiano" {:envelope_decay 0.35 :envelope_release 0.25 :hardness 0.7
  :modulation 0.25 :lfo_rate 0.35 :overdrive 0.2}))
(def rotor (mda "Leslie" {:speed 0.25 :output 0.4}))
(def room (mda "Ambience" {:size 0.65 :mix 0.24 :output 0.45}))
(def keys-track (daw/track keys {:name "Copper" :effects [rotor room] :gain 0.45}))

(def bell (mda "DX10" {:attack 0 :decay 0.2 :release 0.15 :mod_init 0.5 :mod_dec 0.35 :mod_sus 0.1}))
(def echo (mda "DubDelay" {:delay 0.3 :feedback 0.65 :fb_tone 0.38 :lfo_dep 0.1 :fx_mix 0.3 :output 0.4}))
(def bell-track (daw/track bell {:name "Glass" :effects [echo] :gain 0.3 :pan 0.2}))

(def kick (mda "DX10" {:attack 0 :decay 0.3 :release 0.1 :mod_init 0.8 :mod_dec 0.12 :mod_sus 0 :waveform 0.1}))
(def kick-track (daw/track kick {:name "Pulse" :gain 0.4}))
(def hat (mda "DX10" {:attack 0 :decay 0.06 :release 0.03 :coarse 0.83 :fine 0.32 :mod_init 1 :mod_dec 0.12 :mod_sus 0 :mod_thru 0.8}))
(def crush (mda "Degrade" {:quant 0.4 :rate 0.2 :headroom 0.9 :output 0.4}))
(def hat-track (daw/track hat {:name "Dust" :effects [crush] :gain 0.12 :pan -0.25}))

(def glue (mda "Dynamics" {:thresh 0.75 :ratio 0.4 :output 0.1 :mix 0.65}))
(def meter (mda "SpecMeter"))
(daw/through (daw/mix [bass-track keys-track bell-track kick-track hat-track]) glue)
(daw/through glue meter)
(daw/output meter)

(defn note [instrument pitch velocity] [:note instrument pitch velocity])
(defn bar [i]
  (def root-note (get [36 36 39 34] (% (math/floor (/ i 4)) 4)))
  (def bassline (p/steps 0.25 [0 nil 0 12 nil 7 0 nil 0 nil 10 nil 12 0 nil 7]))
  (def hats (p/steps 0.25 [nil 82 nil 70 nil 85 63 76 nil 84 nil 70 nil 89 60 78]))
  (p/parallel
    [(p/map |(note bass (+ root-note $) 100) bassline)
     (p/map (fn [_] (note kick 28 116)) (p/steps 1 [1 1 1 1]))
     (p/map |(note hat 90 $) hats)
     (p/map |(note keys (+ root-note 24 $) 82)
       (p/parallel [(p/steps 1 [0 nil 0 nil])
                    (p/steps 1 [3 nil 3 nil])
                    (p/steps 1 [7 nil 10 nil])]))
     (p/map |(note bell (+ root-note 36 $) 72)
       (p/steps 0.5 (if (odd? i) [nil 7 nil 10 nil 14 12 nil] [nil nil 12 nil 7 nil 3 nil])))
     (p/map |[:param bass :vcf_freq (music/lerp 0.25 0.7 $)] (p/curve 4 16 |(+ 0.5 (* 0.5 (math/sin (+ (* i 0.7) (* $ 6.28)))))))
     (p/map |[:param echo :feedback (music/lerp 0.56 0.75 $)] (p/curve 4 8 identity))]))

(def end (daw/schedule 0 128 (p/serial (seq [i :range [0 16]] (bar i)))))
(daw/end (+ end 3) {:format :pcm16 :normalize 0.92})
