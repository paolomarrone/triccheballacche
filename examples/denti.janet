# Denti di vetro — a crooked lullaby with a very unreliable drummer.
# Original IDM miniature: no samples. Enable GUI in the editor for the two A-SIDs.
(import ../lib/music)
(import ../lib/pattern :as p)

(def asid (or (os/getenv "ASID_PERONE") "../asid/plugin/perone/build/asid.perone"))
(defn synth [params] (daw/plugin "plugins/synth_mono/build/plugin.perone" params))
(defn echo [params] (daw/plugin "plugins/echo/build/plugin.perone" params))
(defn shape [params] (daw/plugin "plugins/shape/build/plugin.perone" params))

# Two detuned toy pianos. The upper oscillator is deliberately inharmonic.
(def bells
  (seq [i :range [0 2]]
    (def voice (synth {:volume 68 :vco1_wave 3 :vco2_wave 3 :vco2_coarse 1.271
                      :vco2_level 23 :vco2_fine (if (= i 0) -11 9)
                      :vcf_cutoff 6800 :vca_attack 2 :vca_decay 270
                      :vca_sustain 0 :vca_release 210}))
    (daw/track voice {:gain 0.62 :pan (if (= i 0) -0.52 0.52)
                     :effects [(echo {:time1 179 :time2 359 :time3 719
                                      :level1 0.28 :level2 0.17 :level3 0.1})]})
    voice))
(def pads
  (seq [i :range [0 3]]
    (def voice (synth {:volume 55 :vco1_wave 3 :vco2_wave 2 :vco2_level 20
                      :vco2_fine (- (* i 9) 8) :vcf_cutoff 1600 :vcf_resonance 8
                      :vca_attack 180 :vca_decay 600 :vca_sustain 55 :vca_release 950}))
    (daw/track voice {:gain 0.25 :pan ([-0.72 0 0.72] i)
                     :effects [(echo {:time1 401 :time2 809 :time3 1217
                                      :level1 0.22 :level2 0.14 :level3 0.09})]})
    voice))

# A-SID is the acid voice's moving filter, before saturation and a short, dry echo.
(def acid (synth {:volume 70 :vco1_wave 2 :vco1_pw 31 :vco2_wave 1 :vco2_level 24
                 :portamento 24 :vcf_cutoff 9000 :vcf_resonance 18
                 :vca_attack 2 :vca_decay 110 :vca_sustain 38 :vca_release 30}))
(def mouth (daw/plugin asid {:cutoff 42 :lfo_amount 15 :lfo_speed 25}))
(def teeth (shape {:drive 3 :level 0.58 :dc 0.001 :lowpass 0.8}))
(def acid-track (daw/track acid {:gain 0.42 :pan -0.1
                               :effects [mouth teeth (echo {:time1 89 :time2 179 :time3 269
                                                           :level1 0.14 :level2 0.08 :level3 0.04})]}))
(def sub (synth {:volume 72 :vco1_wave 3 :vcf_cutoff 250
                :vca_attack 3 :vca_decay 180 :vca_sustain 45 :vca_release 65}))
(daw/track sub {:gain 0.55})

# Kick, snare and hats have separate tails. The fourth voice is the broken machinery.
(def metal (daw/plugin asid {:cutoff 74 :lfo_amount 42 :lfo_speed 62}))
(def drums
  (seq [i :range [0 4]]
    (def voice (daw/plugin "plugins/drums/build/plugin.perone" {:seed (+ 15011997 (* i 7919))}))
    (def effects
      (case i
        1 [(shape {:drive 2.3 :level 0.75 :dc 0.003})
           (echo {:time1 17 :time2 43 :time3 79 :level1 0.12 :level2 0.08 :level3 0.04})]
        3 [metal (shape {:drive 4 :level 0.55 :dc 0.002})
           (echo {:time1 47 :time2 97 :time3 193 :level1 0.2 :level2 0.11 :level3 0.06})]
        []))
    (daw/track voice {:gain ([0.85 0.65 0.27 0.48] i) :pan ([0 -0.08 0.38 -0.46] i) :effects effects})
    voice))
# A bounded waveshaper leaves playback headroom even without export normalization.
(def master (daw/master {:effects [(shape {:drive 1.2 :level 0.82 :lowpass 0.9})]}))

# These builders only collect pattern data; scheduling happens once, at the end.
(defn control [items t node key value]
  (array/push items [t t [:param node key value]]))
(defn note [items t duration node pitch volume]
  (control items t node :volume volume)
  (array/push items [t (+ t duration) [:note node pitch 100]]))
(defn hit [items t lane pitch velocity]
  (array/push items [t (+ t 0.001) [:note (drums lane) pitch velocity]]))
(defn dice [i salt] (% (+ (* i 73) (* i i 19) (* salt 37)) 101))
(defn gesture [duration node key f]
  (p/map |[:param node key $] (p/curve duration (* duration 32) f)))

(def motif (p/steps 0.5 [0 nil 7 3 14 nil 10 2 7 nil 12 3 -1 nil 2 nil]))
(def roots [45 41 48 43])
(def acid-line [0 0 12 7 0 15 10 0 19 12 3 7 0]) # 13 steps against a 16-step bar.

(defn section [kind beats bpm salt]
  (def quiet (find |(= kind $) [:glass :sleep :false-end]))
  (def savage (find |(= kind $) [:teeth :panic :mutant]))
  (def items @[])
  (def root (roots (% salt 4)))
  (control items 0 master :gain 1)
  (control items 0 acid :vca_release (if quiet 150 30))

  # A recognisable tune survives reversal and a different rhythmic silhouette.
  (def tune (if (odd? salt) (p/reverse motif) motif))
  (for bar 0 (/ beats 4)
    (def t (* bar 4))
    (def chord-root (roots (% (+ bar salt) 4)))
    (when (or quiet (even? bar))
      (eachp [i interval] [12 19 26]
        (note items (+ t (* i 0.019)) (if quiet 3.2 2.6) (pads i)
          (+ chord-root interval) (if quiet 60 43))))
    (when (or (not quiet) (= kind :sleep))
      (note items t 0.65 sub (- chord-root 12) 76)
      (unless quiet (note items (+ t 2.75) 0.42 sub (- chord-root 12) 62))))
  (each [a b pitch] (tune :events)
    (for repeat 0 (math/ceil (/ beats (tune :length)))
      (def t (+ a (* repeat (tune :length))))
      (when (< t beats)
        (note items t (if quiet 0.42 0.22) (bells (% repeat 2)) (+ root 24 pitch)
          (if quiet 74 57))
        (when (and savage (= (% repeat 2) 1) (< (+ t 0.1875) beats))
          (note items (+ t 0.1875) 0.08 (bells (if (even? repeat) 1 0)) (+ root 36 pitch) 38)))))

  # The drummer remembers the backbeat, then keeps changing everything around it.
  (unless quiet
    (for step 0 (* beats 4)
      (def t (/ step 4))
      (def cell (% step 16))
      (def bar (math/floor (/ step 16)))
      (def crooked (= kind :panic))
      (def kick? (or (= cell 0) (= cell 10) (and savage (< (dice step salt) 13))))
      (when kick? (hit items t 0 0 (if (= cell 0) 113 87)))
      (when (or (= cell (if crooked 5 4)) (= cell 12))
        (hit items (+ t 0.009) 1 1 111))
      (when (and (not= cell 4) (not= cell 12) (< (dice step (+ salt 2)) (if savage 38 19)))
        (hit items (+ t 0.015) 1 1 (+ 25 (% (* step 17) 37))))
      (unless (= (% (+ step salt) 7) 3)
        (hit items (+ t (if (odd? step) 0.018 0)) 2 (if (= cell 14) 3 2)
          (+ 28 (% (* step 11) 36))))
      (when (and savage (= (% step 8) 7))
        (def count (if crooked 7 3))
        (for j 0 count
          (hit items (+ t (/ j (* count 4))) 3 (if (even? j) 5 2) (+ 25 (* j 5)))))
      (when (and (= cell 15) (or savage (odd? bar)))
        # A different number of slices every bar, squeezed into the last quarter beat.
        (def slices ([5 9 7 13] (% (+ bar salt) 4)))
        (for j 0 slices
          (hit items (+ t (* 0.245 (/ j slices))) 1 1 (+ 21 (math/floor (* 51 (/ j slices)))))))
      (when (or (not= (% step 5) 3) savage)
        (def accent (= (% step 7) 0))
        (def pitch (+ root (acid-line (% (+ step (* salt 3)) 13)) (if (and savage (= cell 11)) 12 0)))
        (control items t acid :portamento (if accent 65 9))
        (control items t acid :vco1_pw (+ 17 (% (* step 13) 67)))
        (note items t (if accent 0.34 0.13) acid pitch (if accent 81 59))))
    (hit items 0 3 4 39)
    # A small hole is louder than another fill. Only the last two savage bars get these cuts.
    (when savage
      (each bar [(- (/ beats 4) 2) (- (/ beats 4) 1)]
        (def t (+ (* bar 4) 3.5))
        (control items t master :gain 0)
        (control items (+ t 0.12) master :gain 1)
        (control items (+ t 0.22) master :gain 0.15)
        (control items (+ t 0.3) master :gain 1))))

  (when (= kind :false-end)
    (hit items 0 0 0 75)
    (note items 0 1 acid (+ root 12) 55)
    (control items 2.5 master :gain 0)
    (control items 3.94 master :gain 1))
  (p/stretch (/ 60 bpm)
    (p/parallel
      [(p/events beats items)
       (gesture beats mouth :cutoff
         |(+ (if quiet 35 24) (* (if quiet 24 62)
             (math/pow (* 0.5 (+ 1 (math/sin (* 2 math/pi (+ (* $ (if savage 17 5)) 0.17))))) 2))))
       (gesture beats mouth :lfo_amount |(+ 8 (* $ (if savage 79 25))))
       (gesture beats mouth :lfo_speed |(+ 18 (* 77 (math/pow $ (if savage 0.35 2)))))
       (gesture beats metal :cutoff |(+ 35 (* 60 (- 1 $))))
       (gesture beats metal :lfo_amount |(+ 18 (* 76 $)))
       (gesture beats metal :lfo_speed |(+ 25 (* 72 (* 0.5 (+ 1 (math/sin (* 14 math/pi $)))))))
       (gesture beats teeth :drive |(+ 1.5 (* (if savage 9 3) $)))
       (gesture beats acid-track :pan |(* (if savage 0.6 0.15) (math/sin (* 6 math/pi $))))])))

# All sections become seconds before concatenation: the tempo changes preserve every origin.
(def song
  (p/serial [(section :glass 8 84 0)
             (section :fracture 24 168 1)
             (section :teeth 32 174 2)
             (section :sleep 8 84 3)
             (section :panic 28 186 4)
             (section :false-end 4 84 5)
             (section :mutant 32 174 6)
             (section :glass 8 84 7)]))
(def duration (+ (song :length) 3))
(def fade
  (p/events duration
    (seq [[a b gain] :in ((p/curve 2.9 180 |(math/pow (- 1 $) 2)) :events)]
      [(+ (song :length) a) (+ (song :length) b) [:param master :gain gain]])))
(daw/schedule 0 60 (p/parallel [song fade]))
(daw/param master (- duration (/ 1 48000)) :gain 0)
(daw/end duration {:format :pcm16 :normalize 0.94})
