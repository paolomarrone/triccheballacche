# Falalalan — a folk hook inside an impatient drum machine. 144 BPM.
# Only the melody comes from falalalan.mid; bass, harmony and drums are new.
(import ../lib/pattern :as p)

(defn phrase [notes]
  (p/serial (seq [[pitch beats] :in notes]
    (p/events beats (if pitch [[0 beats pitch]] [])))))
# Two eight-beat phrases from the tune, with its pickup and answering cadence.
(def hook (phrase [[nil 0.5] [69 0.5] [67 0.5] [69 0.5] [65 1] [64 0.5]
                   [62 1] [64 0.5] [65 0.5] [67 0.5] [69 1] [69 1]]))
(def answer (phrase [[nil 0.5] [69 0.5] [69 0.5] [71 0.5] [72 0.75] [71 0.25]
                     [69 0.5] [67 0.5] [65 0.5] [67 0.5] [64 1] [62 2]]))
(def shards [69 67 69 65 64 62 64 65 67 69 69])

(def root (or (os/getenv "BRICKWORKS_PERONE") "../brickworks/build/perone"))
(defn bw [name &opt params]
  (daw/plugin (string root "/" name "/build/bw_example_" name ".perone") params))
(defn shape [params] (daw/plugin "plugins/shape/build/plugin.perone" params))
(defn echo [params] (daw/plugin "plugins/echo/build/plugin.perone" params))

# A short pulse lead leaves room for the bass and the answering acid sequence.
(def lead (bw "synth_mono"
  {:volume 85 :vco1_wave 2 :vco1_pw 42 :vco2_wave 1 :vco2_fine 4 :vco2_level 42
   :vcf_cutoff 3200 :vcf_resonance 12 :vcf_contour 30 :vcf_decay 100 :vcf_sustain 0
   :vca_attack 3 :vca_decay 170 :vca_sustain 46 :vca_release 42}))
(def delay (echo {:time1 312.5 :time2 625 :time3 937.5 :level1 0.17 :level2 0.08 :level3 0.035}))
(daw/track lead {:gain 0.8 :pan 0.12 :effects [delay]})
(def bass (bw "synth_mono"
  {:volume 90 :vco1_wave 2 :vco1_pw 48 :vco2_wave 3 :vco2_level 80
   :vcf_cutoff 550 :vcf_resonance 10 :vcf_contour 32 :vcf_decay 115 :vcf_sustain 0
   :vca_attack 3 :vca_decay 165 :vca_sustain 30 :vca_release 35}))
(def bass-track (daw/track bass {:gain 1 :effects [(shape {:drive 1.8 :level 0.8 :dc 0.001})]}))
(def acid (bw "synth_mono"
  {:volume 82 :vco1_wave 1 :vco2_wave 2 :vco2_coarse -1 :vco2_level 42
   :vcf_cutoff 800 :vcf_resonance 72 :vcf_contour 45 :vcf_decay 140 :vcf_sustain 0
   :vca_attack 2 :vca_decay 150 :vca_sustain 26 :vca_release 24}))
(def sid (daw/plugin (or (os/getenv "ASID_PERONE") "../asid/plugin/perone/build/asid.perone")
  {:cutoff 85 :lfo_amount 6 :lfo_speed 28}))
(def acid-track (daw/track acid {:gain 0.82 :pan -0.3 :effects
  [sid (shape {:drive 2.8 :level 0.72 :dc 0.001})
   (echo {:time1 156.25 :time2 312.5 :time3 468.75 :level1 0.16 :level2 0.08 :level3 0.04})]}))
(def stab (bw "synth_poly"
  {:volume 76 :vco1_wave 1 :vco2_wave 2 :vco2_fine -5 :vco2_level 55
   :vcf_cutoff 1800 :vcf_contour 30 :vcf_decay 90 :vcf_sustain 0
   :vca_attack 3 :vca_decay 170 :vca_sustain 0 :vca_release 90}))
(def room (bw "fx_reverb" {:predelay 26 :damping 3800 :decay 65 :wet 15}))
(def stab-track (daw/track stab {:gain 0.58 :pan 0.42 :effects
  [(bw "fx_chorus" {:rate 0.4 :depth 12}) room]}))
(def air (bw "synth_mono"
  {:volume 55 :vco1_level 0 :noise_level 100 :vcf_cutoff 900
   :vca_attack 70 :vca_sustain 100 :vca_release 320}))
(daw/track air {:gain 0.5 :pan -0.15 :effects [(bw "fx_hp1" {:cutoff 700})]})

(def bits (bw "fx_bitcrush" {:bit_depth 10 :sr_ratio 60}))
(def drums
  (seq [i :range [0 4]]
    (def voice (daw/plugin "plugins/drums/build/plugin.perone" {:seed (+ 1441526 (* i 65537))}))
    (daw/track voice {:gain ([1.65 1.1 0.44 0.55] i) :pan ([0 -0.06 0.3 -0.48] i)
      :effects (case i
        0 [(shape {:drive 1.55 :level 0.9})]
        1 [(shape {:drive 1.65 :level 0.85})
           (echo {:time1 12 :time2 24 :time3 36 :level1 0.24 :level2 0.18 :level3 0.1})]
        3 [bits (echo {:time1 104.167 :time2 208.333 :time3 416.667
                      :level1 0.2 :level2 0.1 :level3 0.04})]
        [])})
    voice))
(def master (daw/master {:effects [(bw "fx_hp1" {:cutoff 26})
                                 (shape {:drive 1.12 :level 0.9 :lowpass 0.96})]}))

(defn control [items t node key value]
  (array/push items [t t [:param node key value]]))
(defn note [items t duration node pitch volume]
  # Brickworks synth accents use volume; the drums respond to MIDI velocity.
  (control items t node :volume volume)
  (array/push items [t (+ t duration) [:note node pitch 100]]))
(defn hit [items t lane pitch velocity]
  (array/push items [t (+ t 0.001) [:note (drums lane) pitch velocity]]))
(defn sweep [items t beats node key f]
  (each [a _ value] ((p/curve beats (* beats 32) f) :events)
    (control items (+ t a) node key value)))
(defn duck [items t]
  # Each kick makes space in the accompaniment; the lead and drum tails keep moving.
  (each [track gain] [[bass-track 1] [acid-track 0.82] [stab-track 0.58]]
    (sweep items t 0.5 track :gain |(* gain (+ 0.28 (* 0.72 (- 1 (math/pow (- 1 $) 2))))))))

(defn section [kind bars]
  (def beats (* bars 4))
  (def boot (= kind :boot))
  (def dub (= kind :dub))
  (def rise (= kind :rise))
  (def splice (= kind :splice))
  (def outro (= kind :outro))
  (def hot (or (= kind :acid) (= kind :finale) splice))
  (def items @[])
  (control items 0 lead :vco1_wave (if (or dub (= kind :return)) 3 2))
  (control items 0 lead :vca_decay (if dub 400 170))
  (control items 0 lead :vca_release (if dub 220 42))
  (control items 0 lead :vcf_cutoff (if boot 1600 (if dub 2400 (if hot 7600 4600))))
  (control items 0 delay :level1 (if dub 0.38 0.17))
  (control items 0 bass :vcf_cutoff (if hot 1100 (if dub 300 650)))
  (control items 0 bass :vca_sustain (if dub 55 30))
  (control items 0 stab :vca_release (if dub 620 90))
  (control items 0 room :wet (if dub 34 15))
  (control items 0 bits :bit_depth (if splice 7 (if hot 9 13)))
  (control items 0 bits :sr_ratio (if splice 24 65))

  (for bar 0 bars
    (def t (* bar 4))
    (def mode (case kind
      :boot (if (< bar 2) :bare :four)
      :electro :electro :return :electro :dub :half :rise :rise
      :splice ([:electro :rush :half :four] (% bar 4))
      :outro (if (< bar 2) :four :bare) :four))
    (def last (= bar (- bars 1)))
    # D minor/Dorian, with C major under the rising answer. No imported inner parts.
    (def fundamental (if (and (not hot) (not boot) (not rise) (= (% bar 4) 2)) 36 38))
    (def chord (if (= fundamental 38) [57 62 65 69] [55 60 64 67]))
    (def kicks (case mode
      :bare [0 2] :half [0 2.75]
      :electro (if (even? bar) [0 1.75 2.5] [0 0.75 2.5 3.25])
      :rush [0 0.75 1.5 2.75 3.5] :rise (if (< bar 2) [0 2] [0 1 2 3])
      [0 1 2 3]))
    (each at kicks
      (hit items (+ t at) 0 0 (if boot 103 (if (= at 0) 123 114)))
      (duck items (+ t at)))
    (unless (= mode :bare)
      (each at (if (= mode :half) [2] [1 3])
        (hit items (+ t at 0.008) 1 1 (if rise (+ 77 (* bar 10)) 119)))
      (when (or (= mode :electro) (= mode :rush))
        (each at (if (even? bar) [0.75 2.75] [1.75 3.5 3.75])
          (hit items (+ t at 0.014) 1 1 (if (= mode :rush) 66 42)))))
    (for step 0 16
      (def at (+ t (* step 0.25) (if (and (= mode :electro) (odd? step)) 0.018 0)))
      (when (or (and (not (= mode :bare)) (not (= mode :half))) (= (% step 4) 2))
        (hit items at 2 (if (= (% step 4) 2) 3 2)
          (+ ([37 21 85 28] (% step 4)) (% (* bar 5) 12)))))
    (when (or hot (= mode :electro))
      (each at (if (even? bar) [0.5 1.25 2.75] [0.25 1.75 3.5])
        (hit items (+ t at) 3 (if (< at 2) 5 6) (+ 48 (% (* bar 7) 19)))))
    (when (or last (= (% bar 4) 3) rise (= mode :rush))
      (def count (if rise (+ 2 (* bar 2)) (if (or hot last) 8 4)))
      (for j 0 count
        (hit items (+ t 3 (* 0.875 (/ j count))) (if (even? j) 1 3)
          (if (even? j) 1 (if (= mode :rush) 2 5)) (+ 45 (math/floor (* 65 (/ (+ j 1) count)))))))
    (when (and (= bar 0) (not boot) (not dub)) (hit items t 3 4 69))

    # Offbeat techno bass and a separate, sparse electro pattern.
    (def bassline (case mode
      :half [[0.25 0 0.9] [2.5 0 0.6] [3.5 12 0.24]]
      :bare [[0.5 0 0.3] [2.5 0 0.3]]
      :electro (if (even? bar)
        [[0.5 0 0.27] [1.25 0 0.15] [2.25 12 0.15] [3 0 0.25] [3.75 7 0.12]]
        [[0.25 0 0.15] [1.5 0 0.3] [2 7 0.15] [2.75 12 0.14] [3.5 0 0.27]])
      [[0.5 0 0.28] [1.5 0 0.28] [2.5 0 0.28] [3.25 12 0.13] [3.5 0 0.28]]))
    (unless (and rise last)
      (each [at interval gate] bassline
        (note items (+ t at) gate bass (+ fundamental interval) (if boot 77 92))))
    (unless (or boot outro splice (and hot (odd? bar)))
      (each at (case mode :half [0.5] :electro [0.75 2.5] :rise [1.5 3.5] [0.5 2.5])
        (each pitch chord
          (note items (+ t at) (if dub 1.3 0.16) stab pitch (if dub 72 75)))))

    # Call and response: the hook hands whole bars to the sequencer.
    (def sing (case kind
      :boot (>= bar 2) :electro (< (% bar 4) 2) :drive (not (<= 4 bar 5))
      :dub false :rise false :acid (>= (% bar 8) 6) :splice false
      :return true :finale (not (<= 4 bar 7)) :outro false false))
    (when sing
      (def tune (if (or boot (< (% bar 4) 2)) hook answer))
      (def from (* (% bar 2) 4))
      (each [a b pitch] (tune :events)
        (when (<= from a (- (+ from 4) 0.0001))
          (note items (+ t (- a from)) (* (- b a) (if hot 0.64 0.8)) lead pitch
            (if boot 72 (if hot 91 86))))))
    (when (and dub (= bar 0))
      (each [a b pitch] (hook :events)
        (note items (* a 2) (* (- b a) 1.4) lead (- pitch 12) 81)))
    (when splice
      (for step 0 16
        (unless (= (% (+ step bar) 5) 0)
          (note items (+ t (* step 0.25)) 0.1 lead
            (+ (shards (% (+ (* bar 3) (if (odd? bar) (- 15 step) step)) (length shards)))
               (if (= (% step 7) 6) 12 0)) 82))))
    (when (or hot rise (and (not boot) (not dub) (not outro) (not sing)))
      (def line (if (= fundamental 38) [0 0 12 7 0 10 0 3 7 0 12 0 10 7 3]
                                      [0 0 12 7 0 11 0 4 7 0 12 0 11 7 4]))
      (for step 0 16
        (when (and (not (= (% (+ step bar) 7) 1)) (or (not sing) (= (% step 4) 3)))
          (def at (+ t (* step 0.25)))
          (def accent (= (% (+ step bar) 4) 0))
          (control items at acid :vcf_cutoff
            (+ (if rise (+ 220 (* bar 460)) (if hot 650 430))
               (* (if accent 2300 650) (/ (% (+ step (* bar 3)) 11) 10))))
          (control items at acid :vcf_decay (if accent 190 80))
          (note items at (if accent 0.2 0.115) acid
            (+ fundamental 12 (line (% (+ step (* bar 16)) (length line)))) (if accent 91 78)))))
    (when (and outro last)
      (note items (+ t 2) 1.8 lead 62 82)))

  (sweep items 0 beats sid :cutoff |(+ (if hot 51 68) (* (if hot 43 25)
    (* 0.5 (+ 1 (math/sin (* 2 math/pi (+ 0.15 (* $ (/ beats (if hot 5 16)))))))))))
  (sweep items 0 beats sid :lfo_amount |(+ 4 (* $ (if splice 40 12))))
  (sweep items 0 beats sid :lfo_speed |(+ 25 (* $ (if hot 48 15))))
  (when (or rise (= kind :drive) (= kind :return) (= kind :acid))
    (def length (if rise beats 4))
    (def start (- beats length))
    (note items start (- length 0.1) air 62 40)
    (sweep items start length air :volume |(+ 40 (* 50 $)))
    (sweep items start length air :vcf_cutoff |(* 450 (math/pow 28 $))))
  (p/events beats items))

# Each return changes the orchestration. The dub passage keeps a half-time beat;
# the build strips the bass before the acid drop, and the final hook lands over techno.
(def song
  (p/serial [(section :boot 4)
             (section :electro 8)
             (section :drive 8)
             (section :dub 4)
             (section :rise 4)
             (section :acid 8)
             (section :splice 4)
             (section :return 8)
             (section :finale 12)
             (section :outro 4)]))
(def start 0.08)
(def finish (daw/schedule start 144 song))
(daw/param master 0 :gain 0)
(daw/schedule start 60 (p/map |[:param master :gain $] (p/curve 0.03 12 |$)))
(daw/note bass finish 0.65 38 100)
(daw/note (drums 0) finish 0.001 0 115)
(daw/schedule (+ finish 0.8) 60
  (p/map |[:param master :gain $] (p/curve 2.2 88 |(math/pow (- 1 $) 2))))
(daw/end (+ finish 3.1) {:format :pcm16 :normalize 0.94})
