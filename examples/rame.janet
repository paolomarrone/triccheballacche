# Rame — 24 bars of electro at 112 BPM, then a six-second reverb tail.
# Only precompiled Brickworks Perone bundles; run from the repository root.
(import ../lib/music)
(import ../lib/pattern :as p)

(def bpm 112)
(def intro-rest 8) # Beats before the arpeggio enters.
(def root (or (os/getenv "BRICKWORKS_PERONE") "../brickworks/build/perone"))

(defn bw [name &opt params]
  (daw/plugin (string root "/" name "/build/bw_example_" name ".perone") params))

# Local builders collect explicit events; no daw/* calls while composing.
(defn control [items node beat parameter value]
  (array/push items [beat beat [:param node parameter value]]))

(defn sweep [items node parameter beat duration from to]
  (each [t _ value] ((p/curve duration (* 32 duration) |(music/lerp from to $)) :events)
    (control items node (+ beat t) parameter value)))

(defn note [items node beat duration pitch volume]
  # These synths use their volume parameter for accents, not MIDI velocity.
  (control items node beat :volume volume)
  (array/push items [beat (+ beat duration) [:note node pitch 100]]))

# Pads: pulse + detuned saw, chorus, mono-to-stereo pan, stereo reverb.
(def pad (bw "synthpp_poly"
  {:volume 68 :vco1_wave 2 :vco1_pw 42 :vco2_level 68 :vco2_fine 7
   :vcf_cutoff 650 :vcf_resonance 5 :vcf_contour 12
   :vcf_attack 350 :vcf_decay 700 :vcf_sustain 55
   :vca_attack 240 :vca_decay 600 :vca_sustain 70 :vca_release 850}))
(def chorus (bw "fx_chorus" {:rate 0.25 :depth 28}))
(def pad-space (bw "fxpp_reverb" {:predelay 28 :damping 4200 :decay 72 :wet 27}))
(def pad-track (daw/track pad
  {:gain 0.26 :effects [chorus (bw "fx_pan" {:pan -15}) pad-space]}))

# Bass: a resonant saw with a triangle underneath and a little distortion.
(def bass (bw "synth_mono"
  {:vco2_wave 3 :vco2_coarse -1 :vco2_level 72 :portamento 16
   :vcf_cutoff 180 :vcf_resonance 22 :vcf_contour 24
   :vcf_decay 150 :vcf_sustain 0 :vcf_release 90
   :vca_decay 190 :vca_sustain 42 :vca_release 65}))
(def drive (bw "fx_dist" {:distortion 12 :tone 38 :volume 65}))
(daw/track bass {:gain 1.25 :effects [drive]})

# The simple synth plucks eighth notes; its panner travels across the stereo field.
(def arp (bw "synth_simple"
  {:pulse_width 32 :cutoff 1500 :resonance 13 :decay 130 :sustain 0 :release 90}))
(def phaser (bw "fx_phaser" {:rate 0.17 :amount 1.4 :center 1200}))
(def arp-pan (bw "fx_pan"))
(def arp-space (bw "fxpp_reverb" {:predelay 60 :damping 5800 :decay 65 :wet 20}))
(daw/track arp {:gain 0.45 :effects [phaser arp-pan arp-space]})

(def lead (bw "synth_mono"
  {:vco1_wave 2 :vco1_pw 38 :vco2_wave 3 :vco2_level 66 :vco2_fine -5
   :portamento 38 :vcf_cutoff 2800 :vcf_resonance 8 :vcf_contour 10
   :vcf_decay 260 :vcf_sustain 30
   :vca_attack 12 :vca_decay 230 :vca_sustain 55 :vca_release 240}))
(def lead-space (bw "fxpp_reverb" {:predelay 85 :damping 4600 :decay 73 :wet 22}))
(daw/track lead {:gain 0.38 :effects [(bw "fx_pan" {:pan 22}) lead-space]})

# The drums are three more synth instances: pitched triangle, snare, filtered noise.
(def kick (bw "synth_mono"
  {:vco1_wave 3 :vcf_cutoff 450 :vca_decay 170 :vca_sustain 0 :vca_release 70}))
(daw/track kick {:gain 1.05})
(def snare (bw "synth_mono"
  {:vco1_wave 3 :vco1_level 50 :noise_level 100 :vcf_cutoff 6500
   :vca_decay 150 :vca_sustain 0 :vca_release 85}))
(daw/track snare {:gain 1.9 :pan -0.1
  :effects [(bw "fxpp_reverb" {:predelay 9 :damping 3800 :decay 35 :wet 9})]})
(def hat (bw "synth_mono"
  {:vco1_level 0 :noise_level 100 :vcf_cutoff 11000
   :vca_decay 38 :vca_sustain 0 :vca_release 25}))
(daw/track hat {:gain 0.75 :pan 0.32 :effects [(bw "fx_hp1" {:cutoff 7200})]})

# Remove DC from the asymmetric pulse waves and leave room below the kick.
(def master (daw/master {:effects [(bw "fx_hp1" {:cutoff 25})]}))

(defn thump [items beat volume]
  (note items kick beat 0.36 32 volume)
  # Coarse tuning is in octaves. Each kick falls two octaves in 80 ms.
  (each [t _ value] ((p/curve (* 0.08 (/ bpm 60)) 24 |(* 2 (math/pow (- 1 $) 3))) :events)
    (control items kick (+ beat t) :vco1_coarse value)))

(def roots [38 34 41 36]) # D minor 9, Bb major 7, F major 9, C add 9.
(def chords [[57 60 64 69] [53 57 62 65] [53 57 64 67] [55 60 62 67]])
(def bassline [
  [0 0.6 0 84] [0.75 0.2 12 70] [1.5 0.35 0 80]
  [2 0.45 0 85] [2.75 0.2 7 73] [3.25 0.32 10 76] [3.75 0.18 12 71]])
(def melody [
  [[0.5 0.6 77] [1.25 0.25 76] [2 0.8 72] [3.5 0.3 69]]
  [[0.5 0.65 74] [1.5 0.35 77] [2.5 1 81]]
  [[0.25 0.5 79] [1 0.4 77] [1.75 0.75 76] [3 0.65 72]]
  [[0.5 0.5 74] [1.25 0.3 72] [2 0.7 67] [3 0.75 69]]])

# Each section builds relative events, including its scored gain dips.
(defn band [kind bars]
  (def items @[])
  (def break? (= kind :break))
  (def groove? (or (= kind :groove) (= kind :reprise)))
  (for bar 0 bars
    (def beat (* 4 bar))
    (def harmony (% bar 4))
    (def pitches (chords harmony))
    (def arps? (or (not= kind :intro) (>= (* 4 bar) intro-rest)))
    (def final? (and (= kind :reprise) (>= bar (/ bars 2))))
    (each pitch pitches (note items pad beat 3.5 pitch 68))
    (when arps?
      (each [offset _ degree] ((p/steps 0.5 [0 2 1 3 2 1 3 2]) :events)
        (def t (+ beat offset))
        (def offbeat (= (% (- t beat) 1) 0.5))
        (note items arp (+ t (if offbeat 0.045 0)) (if break? 0.4 0.22)
          (+ 12 (pitches degree)) (if offbeat 56 65))))
    (when groove?
      (each [offset duration interval volume] bassline
        (def degree (if (= interval 10) ([10 11 11 7] harmony) interval))
        (note items bass (+ beat offset) duration (+ (roots harmony) degree) volume))
      (each offset [0 1.5 2 3.25] (thump items (+ beat offset) (if (= offset 1.5) 88 96)))
      (each offset [1 3] (note items snare (+ beat offset 0.018) 0.22 50 84))
      (when (= (% bar 2) 1) (note items snare (+ beat 2.75) 0.12 50 48))
      (for i 0 4
        (sweep items pad-track :gain (+ beat i) 0.125 0.26 0.12)
        (sweep items pad-track :gain (+ beat i 0.125) 0.75 0.12 0.26)))
    (when (or (and (= kind :intro) (= bar (- bars 1))) (and break? (= (% bar 2) 0)))
      (thump items beat 82)
      (note items bass beat 2.5 (roots harmony) 74))
    (when (and arps? (not break?))
      (for i 0 8
        (def t (+ beat (* i 0.5) (if (= (% i 2) 1) 0.045 0)))
        (def open? (= i 6))
        (control items hat t :vca_decay (if open? 230 38))
        (control items hat t :vca_release (if open? 100 25))
        (note items hat t (if open? 0.38 0.1) 60 (if (= (% i 2) 1) 96 84)))
      (when final? (note items hat (+ beat 3.8) 0.08 60 74)))
    (when (or (= kind :reprise) (and (= kind :groove) (>= bar (/ bars 2))))
      (each [offset duration pitch] (melody harmony)
        (note items lead (+ beat offset) duration pitch (if final? 77 73))))
    (when break? (note items lead (+ beat 0.5) 2.5 (+ 12 (pitches 3)) 65))
    (when (or (and (= kind :groove) (= (% bar 4) 3))
              (and (= kind :reprise) (= bar (- bars 1))))
      (each offset [3.5 3.75] (note items snare (+ beat offset) 0.12 50 65))))
  (p/events (* 4 bars) items))

# Notes and automation share a length and remain transformable as one pattern.
(defn section [kind bars]
  (def duration (* 4 bars))
  (def items @[])
  (case kind
    :intro (sweep items pad :vcf_cutoff 0 duration 650 1800)
    :groove (do
      (sweep items bass :vcf_cutoff 0 duration 180 1200)
      (sweep items pad-space :wet (- duration 1) 1 27 55)
      (sweep items lead-space :wet (- duration 1) 1 22 48))
    :break (do
      (sweep items pad :vcf_cutoff 0 duration 900 3800)
      (sweep items chorus :depth 0 duration 28 65)
      (control items bass 0 :vcf_cutoff 160)
      (sweep items arp :cutoff 0 duration 900 4200)
      (sweep items phaser :center 0 duration 500 2800)
      (control items lead 0 :vco1_wave 3)
      (sweep items pad-space :wet (- duration 1) 1 55 27)
      (sweep items lead-space :wet (- duration 1) 1 48 22))
    :reprise (do
      (sweep items pad :vcf_cutoff 0 duration 1800 900)
      (sweep items chorus :depth 0 (/ duration 2) 65 28)
      (sweep items bass :vcf_cutoff 0 duration 220 1800)
      (sweep items bass :vcf_resonance 0 duration 22 42)
      (sweep items drive :distortion 0 duration 12 30)
      (control items lead 0 :vco1_wave 2)))
  (p/parallel [(band kind bars) (p/events duration items)]))

(defn opening []
  (def form (p/serial [(section :intro 4) (section :groove 8)]))
  (def items @[])
  (sweep items arp :cutoff intro-rest (- (form :length) intro-rest) 1500 6500)
  (p/parallel [form (p/events (form :length) items)]))

(defn piece []
  (def form (p/serial [(opening) (section :break 4) (section :reprise 8)]))
  # These two gestures span the whole arrangement after the arpeggio enters.
  (def duration (- (form :length) intro-rest))
  (def items @[])
  (sweep items arp :pulse_width intro-rest duration 24 66)
  (each [t _ value] ((p/curve duration (* 16 duration) |(* 65 (math/sin (* 22 math/pi $)))) :events)
    (control items arp-pan (+ intro-rest t) :pan value))
  (p/parallel [form (p/events (form :length) items)]))

# Resolve at the arrangement's end and leave six seconds for the reverb tail.
(def form (piece))
(def finish (form :length))
(def items @[])
(each pitch (chords 0) (note items pad finish 1.5 pitch 62))
(note items bass finish 1 38 76)
(note items lead finish 1.25 74 65)
(thump items finish 92)
(sweep items master :gain 0 0.125 0 1)
(def unit (/ bpm 60))
(def length (+ finish (* 6 unit)))
(each [t _ value] ((p/curve (* 1.99 unit) 160 |(* (- 1 $) (- 1 $))) :events)
  (control items master (+ (- length (* 2 unit)) t) :gain value))
(def end (daw/schedule 0 bpm (p/parallel [form (p/events length items)])))
(daw/end end {:format :pcm16 :normalize 0.94})
