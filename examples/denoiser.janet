# Denoiser — 40 bars of electro-funk at 126 BPM, with a talking lead and a noisy radio bus.
# Requires Bertom Denoiser Classic and Clack.DIGITAL Ue Ue Wah VST3, local Perone plugins,
# and the canonical Brickworks synth_poly bundle. Intro and break include short A/B comparisons.
(import ../lib/pattern :as p)
(import ../lib/music)

(def bpm 126)
(def beat (/ 60 bpm))
(def pickup 0.18)
(defn local [id name params]
  (daw/plugin id (string "plugins/" name "/build/plugin.perone") params))
(def lead (local :lead "synth_mono"
  {:volume 70 :vco1_wave 2 :vco1_pw 32 :vco2_wave 3 :vco2_level 18 :vco2_fine 7
   :vcf_cutoff 7400 :vcf_resonance 8 :portamento 14 :vca_attack 3
   :vca_decay 150 :vca_sustain 26 :vca_release 55}))
(def bass (local :bass "synth_mono"
  {:volume 78 :vco1_wave 2 :vco1_pw 47 :vco2_wave 3 :vco2_coarse -1 :vco2_level 38
   :vcf_cutoff 460 :vcf_resonance 22 :vcf_contour 24 :vcf_decay 180
   :vca_attack 3 :vca_decay 160 :vca_sustain 36 :vca_release 38}))
(def drums (local :drums "drums" {:seed 305126}))
(def answer (local :answer "synth_mono"
  {:volume 61 :vco1_wave 3 :vco2_wave 3 :vco2_coarse 1 :vco2_fine -8 :vco2_level 25
   :vcf_cutoff 6200 :vca_attack 3 :vca_decay 240 :vca_sustain 0 :vca_release 190}))
(def echo (local :answer-echo "echo"
  {:time1 (* beat 750) :time2 (* beat 1250) :time3 (* beat 1750)
   :level1 0.27 :level2 0.15 :level3 0.08}))
(def root (or (os/getenv "BRICKWORKS_PERONE") "build/library/brickworks"))
(def keys (daw/plugin :keys (string root "/synth_poly/build/bw_example_synth_poly.perone")
  {:volume 55 :vco1_wave 3 :vco2_wave 3 :vco2_fine 5 :vco2_level 22
   :vcf_cutoff 2600 :vcf_resonance 6 :vca_attack 5 :vca_decay 370
   :vca_sustain 12 :vca_release 150}))
(def hiss (local :hiss "synth_mono"
  {:volume 100 :vco1_level 0 :vco2_level 0 :vco3_level 0
   :noise_color 1 :noise_level 100 :vcf_cutoff 14000
   :vca_attack 10 :vca_sustain 100 :vca_release 30}))

# Native VST3 IDs. Range 1..6 at 0.25 means -16.5 dB; stereo link at 1 means 100%.
(def threshold :84)
(def bypass :1974165736)
(def ranges [:2591 :2592 :2593 :2594 :2595 :2596])
(def denoiser (daw/plugin :denoiser
  {:format :vst3 :id "ABCDEF019182FAEB4272746D446E636C"}
  {threshold 0.50 bypass 1 :308648411 1
   :2591 0.25 :2592 0.25 :2593 0.25 :2594 0.25 :2595 0.25 :2596 0.25}))
(def position :1333256809)
(def wah-mix :907392060)
(def mid-tongue :1963712987)
(def toe-jaw :1838068427)
(def toe-color :1010384878)
(def wah (daw/plugin :wah {:format :vst3 :id "FDEF4AACEADD4EFAA4C00EC372FE9602"}
  {position 0.12 wah-mix 1 :683198070 1}))
# Mono wah before panning; denoising belongs to the stereo radio bus.
(def radio (daw/mix
  [(daw/track lead {:name "Talking lead" :gain 0.22 :pan -0.18 :effects [wah]})
   (daw/track answer {:name "Glass reply" :gain 0.25 :pan 0.42 :effects [echo]})
   (daw/track hiss {:name "Radio noise" :gain 0.038})]))
(def bass-track (daw/track bass {:name "Rubber bass" :gain 0.34}))
(def keys-track (daw/track keys {:name "Electric keys" :gain 0.27 :pan -0.35}))
(def radio-track (daw/track radio {:name "Denoiser" :gain 0.92 :effects [denoiser]}))
(def master (daw/master (daw/mix
  [(daw/track drums {:name "Broken funk" :gain 0.46}) bass-track keys-track radio-track]) {:gain 0.72}))
(daw/output master)

(defn note [items node at duration pitch velocity]
  (array/push items [at (+ at duration) [:note node pitch velocity]]))
(defn control [items node at key value] (array/push items [at at [:param node key value]]))
(defn curve [items node at duration key shape]
  (each [offset _ value] (p/flatten (p/curve duration (max 16 (math/ceil (* 16 duration))) shape))
    (control items node (+ at offset) key value)))
(defn wave [phase] (* 0.5 (- 1 (math/cos (* 2 math/pi phase)))))
(defn vocal [items at duration pitch accent opening]
  (control items lead at :volume accent)
  (note items lead at duration pitch 100)
  (curve items wah at duration position |(+ 0.12 (* opening (- 1 $) (- 1 $)))))

# Dm9 / Bbmaj9 / Gm9 / A7(b9), voiced around a small recurring hook.
(def roots [38 34 31 33])
(def chords [[53 57 60 64] [50 53 57 60] [46 50 53 57] [49 52 55 58]])
(def colors [[62 65 69 72 76] [62 65 69 70 74] [62 65 67 69 74] [61 64 67 70 73]])
(def hooks [
  [[0.25 0.32 0] [1 0.20 2] [1.75 0.38 1] [2.75 0.24 3] [3.5 0.28 2]]
  [[0 0.48 1] [0.75 0.20 0] [1.5 0.30 2] [2.5 0.55 4]]])
(def bass-riff [[0 0.40 0] [0.75 0.18 12] [1.5 0.32 0] [2.25 0.20 7] [3 0.25 10] [3.625 0.16 12]])

(defn section [kind bars]
  (def items @[])
  (def beats (* bars 4))
  (def intro (= kind :intro))
  (def floating (= kind :break))
  (def busy (or (= kind :turn) (= kind :return)))
  (def coda (= kind :coda))
  (control items denoiser 0 bypass (if (or intro floating) 1 0))
  (when (or intro floating) (control items denoiser 8 bypass 0))
  (control items lead 0 :portamento (if floating 45 14))
  (control items bass 0 :vcf_cutoff (if busy 720 460))

  # The mouth changes over four bars, twice as fast in the busy sections.
  (def cycles (/ beats (if busy 8 16)))
  (each [key low high phase]
    [[wah-mix (if floating 0.58 0.78) (if busy 1 0.92) 0]
     [mid-tongue 0.24 0.82 0.25] [toe-jaw 0.68 0.98 0.5] [toe-color 0.28 0.76 0.75]]
    (curve items wah 0 beats key |(music/lerp low high (wave (+ phase (* cycles $))))))
  # Staggered band reductions sweep the noise color while keeping the low bands gentler.
  (curve items denoiser 0 beats threshold
    |(music/lerp 0.46 (if busy 0.57 0.53) (wave (* (/ beats 16) $))))
  (eachp [i key] ranges
    (curve items denoiser 0 beats key
      |(music/lerp (- 0.40 (* i 0.05)) (- 0.64 (* i 0.05))
        (wave (+ (/ i 12) (* cycles $))))))
  (for bar 0 bars
    (def t (* bar 4))
    (def h (cond intro 0 floating 2 coda 0 (% (math/floor (/ bar 2)) 4)))
    (def root (roots h))
    (def chord (chords h))
    (def pitches (colors h))
    (def fill (= (% bar 4) 3))
    (def last (and coda (= bar (- bars 1))))
    (def kicks (cond last [0] floating [0 2.5] (and busy fill) [0 1.5 2.75 3.25] [0 1 2 3]))
    (each at kicks
      (note items drums (+ t at) 0.02 0 (if (= at 0) 117 101))
      # Brief bass recovery keeps the kick clear without interrupting the phrase.
      (each [offset _ value] (p/flatten (p/curve 0.45 8 |(* 0.34 (+ 0.45 (* 0.55 $)))))
        (control items bass-track (+ t at offset) :gain value)))
    (unless last
      (each at (if floating [3] [1 3])
        (note items drums (+ t at 0.018) 0.02 1 (if floating 63 99)))
      (for step 0 (if floating 8 16)
        (when (or (even? step) (and busy (>= (% step 8) 5)))
          (def at (+ t (* step (if floating 0.5 0.25)) (if (odd? step) 0.026 0)))
          (note items drums at 0.012 (if (= (% step 8) 6) 3 2)
            (if (even? step) (+ 38 (* 5 (% step 3))) 29))))
      (when (and busy fill)
        (eachp [i at] [2.75 3.5 3.75 3.875]
          (note items drums (+ t at) 0.012 1 (+ 32 (* i 9))))))

    (cond
      last (note items bass t 2.2 root 100)
      floating (note items bass (+ t 0.5) 1.6 root 100)
      (eachp [i [at duration interval]] bass-riff
        (unless (and intro (odd? i))
          (control items bass (+ t at) :volume (if (= (% i 3) 0) 81 68))
          (note items bass (+ t at) duration
            (+ root (if (and (= interval 10) (= h 1)) 9 interval)) 100))))

    (each at (cond last [0] floating [0.5] intro [0.75 2.5] [0.75 1.75 3.25])
      (eachp [i pitch] chord
        (note items keys (+ t at (* i 0.009)) (if (or floating last) 2.1 0.28) pitch 100)))
    (cond
      last (vocal items (+ t 0.25) 1.8 74 64 0.65)
      floating (eachp [i at] [0.5 2.25]
        (vocal items (+ t at) 0.9 (pitches (+ 1 i)) 63 0.60))
      (eachp [i [at duration degree]] (hooks (% bar 2))
        (unless (and intro (odd? i))
          (vocal items (+ t at) duration
            (+ (pitches degree) (if (and (= kind :return) fill (= i 3)) 12 0))
            (if (even? i) 75 62) (if busy 0.82 0.68)))))
    # The bell answers the hook; delays continue through the spaces in the lead.
    (when (or floating coda (and (not intro) (odd? bar)))
      (eachp [i at] (if last [1.5 2.25 3] [0.5 1.25 3.25])
        (note items answer (+ t at) (if floating 0.5 0.16)
          (+ 12 (pitches (if last i (+ 1 i)))) 100)))
    (when (and busy fill)
      (control items wah (+ t 3.4) :683198070 0)
      (control items wah (+ t 3.98) :683198070 1)))
  (p/events beats items))

(def song (p/serial [(section :intro 4) (section :groove 8) (section :turn 8)
                    (section :break 4) (section :return 12) (section :coda 4)]))
(daw/schedule pickup bpm song)
(def finish (+ pickup (* beat (song :length))))
(daw/note hiss 0 finish 60 100)
(defn fade [at duration from to]
  (daw/schedule at 60
    (p/map |[:param master :gain (music/lerp from to $)] (p/curve duration 64 identity))))
(fade 0 0.03 0 0.72)
(fade (+ finish 0.25) 2.5 0.72 0)
(daw/end (+ finish 3) {:format :pcm16})
