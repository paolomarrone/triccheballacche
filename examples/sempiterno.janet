# Sempiterno — a lauda caught inside a cathedral drum machine.
# Francisco Soto de Langa, Nell'apparir del sempiterno sole.
# All three parts follow the supplied score, including accidentals and the octave clef.
(import ../lib/pattern :as p)

(def tracks @[])

(def root (or (os/getenv "BRICKWORKS_PERONE") "../brickworks/build/perone"))
(defn bw [name &opt params]
  (daw/plugin (string root "/" name "/build/bw_example_" name ".perone") params))
(defn shape [params] (daw/plugin "plugins/shape/build/plugin.perone" params))
(defn echo [params] (daw/plugin "plugins/echo/build/plugin.perone" params))

# Soprano, alto and tenor have separate pipes. Short releases keep the tuttis dry.
(def manuals
  (seq [i :range [0 3]]
    (bw "synth_mono"
      {:volume ([82 73 79] i) :vco1_wave (if (= i 1) 2 3) :vco1_pw 50
       :vco2_wave 3 :vco2_coarse 1 :vco2_level ([62 35 28] i)
       :vco3_wave 3 :vco3_coarse 2 :vco3_level 26 :vcf_cutoff 7200
       :vca_attack 3 :vca_sustain 100 :vca_release 18})))
(def rooms
  (seq [i :range [0 3]]
    (bw "fx_reverb" {:predelay (+ 13 (* i 7)) :damping 5800 :decay 60 :wet 12})))
(def manual-tracks
  (seq [[i voice] :pairs manuals]
    (daw/track voice {:gain ([0.48 0.28 0.36] i) :pan ([-0.1 0.3 -0.3] i)
                     :effects [(rooms i)]})))
(array/concat tracks manual-tracks)
(def mixture (bw "synth_mono"
  {:volume 74 :vco1_wave 2 :vco1_pw 50 :vco2_wave 3 :vco2_coarse 1.584963
   :vco2_level 45 :vco3_wave 3 :vco3_coarse 2 :vco3_level 38
   :vcf_cutoff 9800 :vca_attack 2 :vca_sustain 100 :vca_release 12}))
(def rotor (bw "fx_trem" {:rate 5 :amount 20}))
(def mixture-track (daw/track mixture {:gain 0.3 :pan 0.38 :effects
  [rotor (echo {:time1 94 :time2 188 :time3 375 :level1 0.12 :level2 0.07 :level3 0.04})]}))
(array/push tracks mixture-track)

# A-SID supplies the moving acid filter; the pedal keeps the original tenor's roots.
(def bass (bw "synth_mono"
  {:volume 86 :vco1_wave 2 :vco1_pw 29 :vco2_wave 1 :vco2_coarse -1 :vco2_level 28
   :vcf_cutoff 9800 :portamento 12 :vca_attack 2 :vca_decay 95
   :vca_sustain 38 :vca_release 14}))
(def sid (daw/plugin (or (os/getenv "ASID_PERONE") "../asid/plugin/perone/build/asid.perone")
  {:cutoff 49 :lfo_amount 16 :lfo_speed 31}))
(def grit (shape {:drive 3.5 :level 0.7 :dc 0.001 :lowpass 0.88}))
(array/push tracks
  (daw/track bass {:gain 0.76 :effects [sid grit]}))
(def pedal (bw "synth_mono"
  {:volume 80 :vco1_wave 3 :vcf_cutoff 210 :vca_attack 3
   :vca_decay 170 :vca_sustain 42 :vca_release 28}))
(array/push tracks
  (daw/track pedal {:gain 0.56}))
(def drums
  (seq [i :range [0 4]]
    (def voice (daw/plugin "plugins/drums/build/plugin.perone" {:seed (+ 1599 (* i 12289))}))
    (array/push tracks
      (daw/track voice {:gain ([1.65 1.1 0.46 0.65] i) :pan ([0 -0.06 0.28 -0.35] i)
        :effects (case i
          0 [(shape {:drive 1.15 :level 0.93})]
          1 [(echo {:time1 13 :time2 29 :time3 61 :level1 0.14 :level2 0.08 :level3 0.04})]
          3 [(shape {:drive 2.5 :level 0.7 :dc 0.002})]
          [])}))
    voice))
# Leave transients room. This explicit bound also applies during live playback.
(def master
  (daw/master (daw/mix tracks)
    {:effects [(bw "fx_hp1" {:cutoff 24})
               (shape {:drive 1.05 :level 0.9 :lowpass 0.96})]}))
(daw/output master)

(defn control [items t node key value]
  (array/push items [t t [:param node key value]]))
(defn note [items t duration node pitch &opt velocity]
  (array/push items [t (+ t duration) [:note node pitch (or velocity 100)]]))
(defn hit [items t lane pitch velocity]
  (note items t 0.001 (drums lane) pitch velocity))
(defn gesture [beats node key f]
  (p/map |[:param node key $] (p/curve beats (* beats 24) f)))

# Twelve notated 4/4 bars per voice. Durations are quarter notes, halved for sequencing.
# Each row below holds two bars; the tenor already sounds an octave below its clef.
(def parts
  (map (fn [notes]
    (p/stretch 0.5
      (p/serial (seq [[pitch beats] :in notes]
        (p/events beats (if pitch [[0 beats pitch]] []))))))
    [# Soprano
     [[64 2] [64 1] [64 1] [69 3] [69 1]
      [69 1.5] [69 0.5] [67 1] [65 1] [64 2] [64 2]
      [nil 1] [65 1] [65 1] [65 1] [65 3] [65 1]
      [65 1.5] [65 0.5] [64 1] [65 1] [67 2] [67 2]
      [69 2] [69 1.5] [67 0.5] [65 3] [65 1]
      [67 1.5] [65 0.5] [64 1] [62 1] [64 2] [62 2]]
     # Alto: C-sharps remain sharp through their bars, including the final leading note.
     [[60 2] [60 1] [60 1] [65 3] [65 1]
      [65 1.5] [65 0.5] [64 1] [62 1] [61 2] [61 2]
      [62 2] [62 1] [62 1] [62 3] [62 1]
      [62 1.5] [62 0.5] [61 1] [62 1] [64 2] [64 2]
      [65 2] [65 1.5] [64 0.5] [62 3] [62 1]
      [62 1.5] [62 0.5] [61 1] [62 1] [62 1] [61 1] [62 2]]
     # Tenor: B-flats in bars 10 and 11; the last chord resolves to bare D octaves.
     [[57 2] [57 1] [57 1] [53 3] [53 1]
      [53 1.5] [53 0.5] [60 1] [62 1] [57 2] [57 2]
      [50 2] [50 1] [50 1] [50 3] [50 1]
      [50 1.5] [50 0.5] [57 1] [62 1] [60 2] [60 2]
      [53 2] [53 1.5] [53 0.5] [58 3] [58 1]
      [55 1.5] [55 0.5] [57 1] [58 1] [57 2] [50 2]]]))
(defn pitch-at [voice beat]
  (def t (% beat 24))
  (def event (find (fn [[a b _]] (and (<= a t) (< t b))) ((parts voice) :events)))
  (when event (event 2)))
(def teeth [0 12 0 7 12 0 19 7 0 12 7 24 0 7 12])

(defn section [kind beats bpm]
  (def quiet (or (= kind :dawn) (= kind :amen)))
  (def broken (= kind :electro))
  (def wild (= kind :toccata))
  (def rise (= kind :rise))
  (def full (or wild rise (= kind :sun)))
  (def items @[])
  (control items 0 pedal :volume (if quiet 57 84))
  (eachp [i voice] manuals
    (control items 0 (manual-tracks i) :gain (* (if quiet 0.74 1) ([0.48 0.28 0.36] i)))
    (control items 0 voice :vca_attack (if quiet 18 3))
    (control items 0 voice :vca_release (if quiet 75 14))
    (control items 0 voice :vco2_wave (if full 2 3))
    (control items 0 voice :vco3_level (if full 66 26))
    (control items 0 voice :vcf_cutoff (if quiet 6200 (if full 12500 8500)))
    (control items 0 (rooms i) :wet (if quiet 19 5))
    (control items 0 (rooms i) :decay (if quiet 67 42)))

  (eachp [v voice] manuals
    (for repeat 0 (math/ceil (/ beats 24))
      (each [a b pitch] ((parts v) :events)
        (def cadence (= kind :amen))
        (def t (if cadence (* 4 (- a 22)) (+ a (* repeat 24))))
        (when (<= 0 t (- beats 0.001))
          (def length (* (- b a) (if cadence 4 1)))
          (note items t (if quiet (* length 0.96) (min (if wild 0.19 0.38) (* length 0.8))) voice pitch)
          (when (and quiet (= v 2))
            (note items t (* length 0.94) pedal (- pitch 24)))
          (when (and (= kind :sun) (= v 0))
            (note items t (min 0.24 (* length 0.6)) mixture (+ pitch 12)))))))

  (unless quiet
    (for step 0 (* beats 4)
      (def t (/ step 4))
      (def cell (% step 16))
      (def bar (math/floor (/ step 16)))
      (def fundamental (- (pitch-at 2 t) 24))
      (when (if broken (find |(= cell $) [0 6 10 13]) (= (% cell 4) 0))
        (hit items t 0 0 (if (= cell 0) 127 118))
        (note items (+ t 0.12) 0.3 pedal fundamental))
      (when (or (= cell 4) (= cell 12)) (hit items t 1 1 124))
      (when (and broken (find |(= cell $) [3 9 15])) (hit items t 1 1 58))
      (when (or (even? step) full)
        (hit items (+ t (if (odd? step) 0.018 0)) 2 (if (= (% cell 4) 2) 3 2)
          (if (= (% cell 4) 2) 89 (+ 42 (% (* step 13) 31)))))
      (when (or (not= (% step 4) 0) full)
        (control items t bass :volume (if (= (% step 3) 0) 91 76))
        (control items t bass :portamento (if (= (% step 7) 0) 54 6))
        (note items t 0.16 bass (+ fundamental 12 (teeth (% step 15)))))
      (when (or wild (and broken (= (% step 3) 0)))
        # The original three voices become a rotating, octave-displaced toccata.
        (def pitch (pitch-at (% (+ step bar) 3) t))
        (when pitch
          (note items t 0.11 mixture (+ pitch 12 (* 12 (% (math/floor (/ step 7)) 2)))))))
    (hit items 0 3 4 89)
    # Fills carry the pulse across phrases; the instruments and their tails keep playing.
    (for i 0 (math/floor (/ beats 12))
      (def end (* 12 (+ i 1)))
      (def slices (if wild 6 3))
      (for j 0 slices
        (hit items (+ (- end 0.5) (* 0.5 (/ j slices))) (if (even? j) 1 3)
          (if (even? j) 1 (if (even? i) 5 6)) (+ 58 (math/floor (* 48 (/ j slices)))))))
    (when rise
      (for i 0 8
        (hit items (+ (- beats 2) (* i 0.25)) 1 1 (+ 48 (* i 9))))))

  (p/stretch (/ 60 bpm)
    (p/parallel
      [(p/events beats items)
       (gesture beats sid :cutoff
         |(+ 31 (* (if full 64 51) (math/pow (* 0.5 (+ 1 (math/sin (* 2 math/pi (+ 0.1 (* $ 9)))))) 2))))
       (gesture beats sid :lfo_amount |(+ 12 (* $ (if wild 77 36))))
       (gesture beats sid :lfo_speed |(+ 29 (* $ (if wild 69 38))))
       (gesture beats grit :drive |(+ 2.8 (* $ (if wild 5 2))))
       (gesture beats rotor :rate |(+ 4 (* (if full 14 5) $)))
       (gesture beats mixture-track :pan |(* 0.72 (math/sin (* (if wild 38 10) math/pi $))))])))

# A short rise leads into the toccata. The coda stretches the final bar and fermata.
(def song
  (p/serial [(section :dawn 24 144)
             (section :machine 48 160)
             (section :electro 48 160)
             (section :rise 8 160)
             (section :toccata 48 176)
             (section :sun 48 168)
             (section :amen 8 96)]))
# A little space before the first pipes, then a gentle attack into the opening chord.
(def start 0.12)
(def finish (+ start (song :length)))
(def duration (+ finish 4))
(daw/param master 0 :gain 0)
(daw/schedule start 60 song)
(daw/schedule start 60
  (p/map |[:param master :gain $] (p/curve 0.06 60 |(* $ $ (- 3 (* 2 $))))))
(daw/schedule finish 60
  (p/map |[:param master :gain $] (p/curve 3.9 160 |(math/pow (- 1 $) 2))))
(daw/param master (- duration (/ 1 48000)) :gain 0)
(daw/end duration {:format :pcm16 :normalize 0.94})
