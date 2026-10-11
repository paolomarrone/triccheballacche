# Insetti di vetro — original fractured electronica, 158 BPM, 96 bars + a scored tail.
# Glass melody, unstable piano, acid/sub, broken drums and short bursts of machine failure.
# Form: specimen / pulse / fracture / suspension / swarm / missing clock / return / coda.
# Build: make library; build local drums and the optional embedded piano (see README).
# Render: ./build/cli examples/insetti-di-vetro.janet renders/insetti-di-vetro.wav 48000
(import ../lib/pattern :as p)

(def bpm 158)
(def beat-ms (/ 60000 bpm))
(def bw-root (or (os/getenv "BRICKWORKS_PERONE") "build/library/brickworks"))
(defn bw [id name params]
  (daw/plugin id (string bw-root "/" name "/build/bw_example_" name ".perone") params))
(def tracks @[])
(defn lane [source name gain pan effects]
  (def node (daw/track source {:name name :gain gain :pan pan :effects effects}))
  (array/push tracks node)
  node)
(defn drum [id seed] (daw/plugin id "plugins/drums/build/plugin.perone" {:seed seed}))

# A dry low end; the destructive processing belongs to the smaller percussion.
(def kick (drum :kick 310301))
(def kick-track (lane kick "01 / impact" 1.04 0
  [(bw :kick-satur "fx_satur" {:gain 1.7}) (bw :kick-hp "fx_hp1" {:cutoff 27})]))
(def snare (drum :snare 310302))
(def snare-crush (bw :snare-crush "fx_bitcrush" {:sr_ratio 76 :bit_depth 13 :gate 1}))
(def snare-track (lane snare "02 / broken spine" 0.67 -0.035
  [(bw :snare-drive "fx_drive" {:drive 20 :tone 62 :volume 81}) snare-crush
   (bw :snare-hp "fx_hp1" {:cutoff 155})]))
(def hats (drum :hats 310303))
(def hat-track (lane hats "03 / needles" 0.32 0.33
  [(bw :hat-crush "fx_bitcrush" {:sr_ratio 69 :bit_depth 12 :gate 1})
   (bw :hat-hp "fx_hp1" {:cutoff 4300})]))
(def fragments (drum :fragments 310304))
(def fragment-ring (bw :fragment-ring "fx_ring_mod" {:frequency 293 :amount 73}))
(def fragment-comb (bw :fragment-comb "fx_comb"
  {:ff_delay 8 :fb_delay 21 :blend 0.85 :ff 0.38 :fb 0.31}))
(def fragment-track (lane fragments "04 / insect legs" 0.46 -0.48
  [fragment-ring fragment-comb (bw :fragment-hp "fx_hp1" {:cutoff 430})]))

(def bass (bw :acid "synth_mono"
  {:volume 79 :vco1_wave 2 :vco1_pw 37 :vco2_wave 1 :vco2_fine -6 :vco2_level 23
   :vcf_cutoff 360 :vcf_resonance 48 :vcf_contour 35 :vcf_decay 125 :vcf_sustain 0
   :vca_attack 2 :vca_decay 125 :vca_sustain 32 :vca_release 19 :portamento 23}))
(def bass-drive (bw :bass-drive "fx_drive" {:drive 17 :tone 37 :volume 81}))
(def bass-track (lane bass "05 / acid skeleton" 0.70 0
  [bass-drive (bw :bass-hp "fx_hp1" {:cutoff 63})]))
(def sub (bw :sub "synth_mono"
  {:volume 88 :vco1_wave 3 :vcf_cutoff 155 :vcf_resonance 0
   :vca_attack 3 :vca_decay 210 :vca_sustain 64 :vca_release 34}))
(def sub-track (lane sub "06 / undertow" 0.58 0 []))

# The same four-bar melodic fingerprint migrates between piano and glass.
(def piano (daw/plugin :piano "plugins/piano/build/plugin.perone"
  {:gain 0.72 :detune 6 :blend 0.24}))
(def piano-space (bw :piano-space "fx_reverb"
  {:predelay 31 :bandwidth 8300 :damping 5700 :decay 63 :wet 13}))
(def piano-track (lane piano "07 / crooked music box" 0.82 -0.12
  [(bw :piano-wobble "fx_vibrato" {:rate 2.3 :amount 0.011})
   (bw :piano-hp "fx_hp1" {:cutoff 165}) piano-space]))
(def glass (bw :glass "synth_mono"
  {:volume 64 :vco1_wave 3 :vco2_wave 3 :vco2_coarse 1 :vco2_fine 9 :vco2_level 27
   :vcf_cutoff 6700 :vcf_resonance 4 :vcf_contour 0
   :vca_attack 2 :vca_decay 255 :vca_sustain 0 :vca_release 180}))
(def glass-ring (bw :glass-ring "fx_ring_mod" {:frequency 443 :amount 26}))
(def glass-pan (bw :glass-pan "fx_pan" {:pan 23}))
(def glass-space (bw :glass-space "fx_reverb"
  {:predelay 57 :bandwidth 10000 :damping 6400 :decay 64 :wet 16}))
(def glass-track (lane glass "08 / glass insects" 1.25 0 [glass-ring glass-pan glass-space]))

# A real parallel delay send; the shared instrument is rendered only once.
(def glass-echo (daw/through glass (bw :glass-delay "fx_delay" {:delay (* beat-ms 0.75)})))
(lane glass-echo "09 / glass afterimage" 0.26 -0.61
  [(bw :glass-echo-filter "fx_svf" {:cutoff 3100 :q 0.7})])
(def chord (bw :chords "synth_poly"
  {:volume 51 :vco1_wave 2 :vco1_pw 24 :vco2_wave 3 :vco2_fine -8 :vco2_level 31
   :vcf_cutoff 1550 :vcf_resonance 9 :vcf_contour 14 :vcf_decay 230 :vcf_sustain 0
   :vca_attack 4 :vca_decay 290 :vca_sustain 0 :vca_release 170}))
(def chord-track (lane chord "10 / bent harmonics" 0.40 0.12
  [(bw :chord-pan "fx_pan" {:pan -19})
   (bw :chord-space "fx_reverb" {:predelay 18 :damping 3700 :decay 53 :wet 11})]))
(def wire (bw :wire "synth_simple"
  {:volume 57 :pulse_width 16 :cutoff 4800 :resonance 31
   :attack 2 :decay 42 :sustain 0 :release 18}))
(def wire-crush (bw :wire-crush "fx_bitcrush" {:sr_ratio 25 :bit_depth 9 :gate 1}))
(def wire-track (lane wire "11 / faulty arithmetic" 0.40 0.55
  [wire-crush (bw :wire-hp "fx_hp1" {:cutoff 760})]))

(def mix (daw/mix tracks {:gain 0.68}))
(def master (daw/master mix {:gain 0.88 :effects
  [(bw :master-hp "fx_hp1" {:cutoff 24})
   (bw :master-comp "fx_comp" {:threshold -10 :ratio 1.9 :attack 15 :release 105 :gain 0})]}))
(daw/output master)

(def events @[])
(def kicks @[])
(defn param [t node key value] (array/push events [t t [:param node key value]]))
(defn note [t duration node pitch velocity]
  (array/push events [t (+ t duration) [:note node pitch (math/floor (max 1 (min 127 velocity)))]]))
(defn synth-note [t duration node pitch volume]
  # BW synths use their volume control for accents; MIDI velocity does not set amplitude.
  (param t node :volume volume)
  (note t duration node pitch 100))
(defn ramp [t duration node key from to &opt steps]
  (default steps 24)
  (for i 0 (+ steps 1)
    (def x (/ i steps))
    (param (+ t (* duration x)) node key (+ from (* (- to from) x)))))
(defn hit [t node type velocity] (note t 0.018 node type velocity))
(defn thump [t velocity] (hit t kick 0 velocity) (array/push kicks t))
(defn pick [bar step salt n] (% (+ (* bar 17) (* step 11) (* bar step 3) salt) n))
(defn burst [t count spacing strength]
  (for i 0 count
    (hit (+ t (* i spacing)) snare 1 (* strength (- 1 (* 0.055 i))))))

(def roots [38 34 43 45])
(def voicings [[50 57 60 64 65] [46 53 57 62 64] [43 53 57 58 62] [45 52 55 58 61]])
(def melody [
  [[0 0.36 74 91] [0.75 0.18 81 79] [1.5 0.52 77 86] [2.75 0.2 76 74] [3.5 0.28 72 82]]
  [[0.25 0.3 74 87] [1 0.22 77 75] [1.75 0.32 76 82] [2.5 0.7 69 81] [3.5 0.19 70 75]]
  [[0 0.42 79 86] [0.75 0.18 81 70] [1.25 0.28 82 74] [2 0.52 77 82] [3.25 0.35 74 89]]
  [[0 0.32 73 81] [0.75 0.12 76 72] [1 0.12 77 64] [1.5 0.7 70 85] [2.75 0.32 69 85] [3.5 0.22 73 69]]])
(def kick-patterns [
  [[0 124] [1.5 103] [2.75 116]]
  [[0 122] [0.75 88] [2 121] [3.25 92]]
  [[0 123] [1.75 108] [2.5 116] [3.75 73]]
  [[0 124] [1.25 89] [2.75 116] [3.5 98]]
  [[0 125] [0.5 77] [1.75 114] [2.5 105]]
  [[0.25 119] [1.5 107] [2.25 117] [3.5 95]]
  [[0 124] [1.75 106] [2.5 111] [3.25 91]]
  [[0 124] [1.5 109] [2.625 113]]])
(def bass-pattern [[0.25 0.38 0] [1 0.17 12] [1.75 0.34 0]
                  [2.5 0.18 7] [3 0.25 10] [3.625 0.23 12]])

(for bar 0 96
  (def t (* bar 4))
  (def kind (cond (< bar 8) :specimen (< bar 24) :pulse (< bar 40) :fracture
                  (< bar 48) :suspension (< bar 64) :swarm (< bar 72) :clock
                  (< bar 88) :return :coda))
  (def intro (= kind :specimen))
  (def quiet (= kind :suspension))
  (def clock (= kind :clock))
  (def coda (= kind :coda))
  (def frantic (or (= kind :fracture) (= kind :swarm) (= kind :return)))
  (def harmony (% (math/floor (/ bar 2)) 4))
  (def root (roots harmony))
  (def last (= bar 95))
  (def hole (or (= bar 39) (= bar 63) (= bar 71) (= bar 87)))
  (def stop (if hole 3.375 4))
  (def drums-on (and (not quiet) (or (not intro) (>= bar 4)) (or (not coda) (< bar 92))))
  (def bass-on (and (not quiet) (>= bar 6) (< bar 94)))
  (param t bass :vcf_cutoff (+ (if frantic 490 270) (* (pick bar 0 3 7) (if frantic 120 45))))
  (param t bass :vcf_resonance (if (= kind :swarm) 65 (if frantic 55 42)))
  (param t bass-drive :drive (if (= kind :swarm) 28 17))
  (param t snare-crush :sr_ratio (if frantic (+ 28 (* 8 (% bar 5))) 76))
  (param t snare-crush :bit_depth (if (= kind :swarm) 10 13))
  (param t fragment-ring :frequency ([197 293 443 587 887 1319 659] (% bar 7)))
  (param t fragment-comb :fb_delay ([17 29 11 37 23] (% bar 5)))
  (param t piano-space :wet (if quiet 29 13))
  (param t piano :detune (if quiet 10 (if clock 17 6)))
  (param t glass-ring :frequency ([293.66 440 523.25 349.23] harmony))
  (param t glass-ring :amount (if (= kind :swarm) 49 26))
  (ramp t 3.8 glass-pan :pan (if (even? bar) -32 43) (if (even? bar) 43 -32) 16)

  # Distinct kick cells, late snares, quiet ghosts and tightly bounded rolls.
  (when drums-on
    (each [at velocity] (kick-patterns (% bar 8))
      (when (and (< at stop) (or (not clock) (< at 2.6)) (or (not intro) (even? (math/floor at))))
        (thump (+ t at) velocity)))
    (each at (if clock [2] [1 3])
      (when (< at stop) (hit (+ t at 0.012) snare 1 (if intro 89 113))))
    (unless intro
      (each at [0.75 2.375 3.625]
        (when (and (< at stop) (< (pick bar (math/floor (* at 8)) 3 5) 3))
          (hit (+ t at) snare 1 (+ 27 (pick bar (math/floor (* at 8)) 1 24))))))
    (for i 0 16
      (def at (+ (* i 0.25) (if (odd? i) 0.022 0)))
      (when (and (< at stop) (or (even? i) (and frantic (< (pick bar i 7 7) 4))))
        (hit (+ t at) hats (if (and (= i 14) (= (% bar 3) 1)) 3 2)
          (+ (if (= (% i 4) 2) 53 28) (pick bar i 2 14)))))
    (when (and frantic (= (% bar 4) 3))
      (def count (if (= kind :swarm) 9 6))
      (param (+ t 2.95) snare-crush :sr_ratio 14)
      (param (+ t 2.95) snare-crush :bit_depth 8)
      (burst (+ t 3) count (/ (if hole 0.32 0.84) count) 73)))
  (when (and quiet (even? bar)) (thump t 82))
  (when (= bar 92) (thump t 104))

  (when bass-on
    (eachp [i [at duration interval]] bass-pattern
      (when (and (< at stop) (or (not clock) (even? i)) (or (not coda) (< at 2)))
        (def alteration (if (= interval 10) ([10 11 10 10] harmony) interval))
        (synth-note (+ t at) duration bass (+ root alteration)
          (if (= (% i 3) 0) 81 67))))
    (each [at duration] (if clock [[0.5 1.3] [3 0.45]] [[0.125 0.74] [2.125 0.65]])
      (when (< at stop) (synth-note (+ t at) duration sub (if (> root 40) (- root 12) root) 88))))

  # Melody A has an answer in the next bar, with register and rhythm changing by section.
  (def statement (or intro quiet coda (= kind :pulse) (= kind :return)))
  (when (and statement (not last))
    (eachp [i [at duration pitch velocity]] (melody harmony)
      (def answer (odd? bar))
      (def offset (if (and answer (not quiet)) (+ 0.25 (* at 0.875)) at))
      (when (and (< offset stop) (not (and coda (> bar 92) (> i 1)))
                 (or (not answer) (even? i) quiet))
        (note (+ t offset (if (odd? i) 0.006 0))
          (* duration (if quiet 2.1 (if coda 1.5 1))) piano
          (+ pitch (if (or intro quiet coda) -12 0))
          (* velocity (if quiet 0.91 (if intro 0.81 1)))))))
  (when (or frantic (and intro (>= bar 2)) (and (= kind :pulse) (>= bar 16)))
    (eachp [i [at duration pitch velocity]] (melody harmony)
      (when (and (< at stop) (or frantic (odd? i)))
        (def displacement (if (and (= kind :swarm) (odd? bar)) 0.125 0))
        (synth-note (+ t at displacement) (* duration (if (= kind :swarm) 0.55 0.82)) glass
          (+ pitch (if (and (= kind :swarm) (= (% i 3) 1)) 12 0))
          (+ 49 (* 0.14 velocity))))))

  # Sparse chord punctuations; the suspension gives the piano room for new voicings.
  (when (and (not last) (or (even? bar) quiet (= kind :return)))
    (eachp [i pitch] (voicings harmony)
      (when (or quiet (< i 4))
        (if (or quiet coda)
          (note (+ t 0.12 (* i 0.038)) (if quiet 2.3 1.4) piano pitch (+ 48 (* i 4)))
          (synth-note (+ t (if intro 0.12 0.5) (* i 0.012)) 0.39 chord (+ pitch 12) 51)))))
  (when (and quiet (odd? bar))
    (eachp [i pitch] (reverse (voicings harmony))
      (note (+ t 1.5 (* i 0.375)) 0.36 piano (+ pitch 12) (+ 56 (* 3 i)))))

  # A five-step electronic counter-rhythm moves across the four-beat bars.
  (when (or frantic clock)
    (for i 0 16
      (def at (* i 0.25))
      (when (and (< at stop) (= (% (+ (* bar 16) i) 5) 0))
        (hit (+ t at) fragments (if (even? i) 5 6) (+ 46 (pick bar i 7 29))))))
  (when (or (= kind :swarm) clock (and (= kind :fracture) (odd? bar)))
    (param t wire-crush :sr_ratio ([18 31 12 43 24] (% bar 5)))
    (def steps (if clock 11 7))
    (for i 0 steps
      (def at (+ (if clock 0.375 2) (* i (if clock 0.3 0.25))))
      (when (< at stop)
        (synth-note (+ t at) 0.052 wire
          (+ 12 ((voicings harmony) (% (+ i bar) 5)) (if (= (% i 4) 3) 12 0))
          (+ 40 (pick bar i 1 16))))))
  (when (or (= bar 7) (= bar 23) (= bar 47) (= bar 71))
    (eachp [i pitch] [74 77 81 86]
      (synth-note (+ t 3 (* i 0.2)) 0.08 glass pitch (+ 52 (* i 3)))))
  (when hole
    (ramp (+ t 3.36) 0.025 master :gain 0.88 0.08 8)
    (ramp (+ t 3.94) 0.06 master :gain 0.08 0.88 16)))

# Duck only until the next kick, so dense kick rolls cannot overwrite a fresh dip.
(def sorted-kicks (sorted kicks))
(eachp [i t] sorted-kicks
  (def duration (min 0.46 (- (get sorted-kicks (+ i 1) (+ t 0.5)) t 0.005)))
  (each [node gain depth] [[bass-track 0.70 0.26] [sub-track 0.58 0.16]]
    (param t node :gain (* gain depth))
    (for j 1 17
      (def x (/ j 16))
      (param (+ t (* duration x)) node :gain (* gain (+ depth (* (- 1 depth) x x)))))))

# The coda loses its machinery, then returns to an unresolved D minor ninth.
(ramp 352 23 piano-space :wet 13 23 48)
(ramp 352 23 glass-space :wet 16 34 48)
(eachp [i pitch] [50 57 64 65 74]
  (note (+ 380.1 (* i 0.055)) (- 3.1 (* i 0.055)) piano pitch ([63 54 57 60 71] i)))
(synth-note 382.125 0.15 glass 86 44)
(def end-beats 396)
(ramp 387 8.75 master :gain 0.88 0 224)
(daw/schedule 0 bpm (p/events end-beats events))
(daw/end (/ (* end-beats 60) bpm) {:format :pcm16 :normalize 0.93})
