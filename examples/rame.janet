# Rame — 24 bars of electro at 112 BPM, then a six-second reverb tail.
# Only precompiled Brickworks Perone bundles; run from the repository root.
(import ../lib/music)

(def bpm 112)
(def intro-rest 8) # Beats before the arpeggio enters.
(def root (or (os/getenv "BRICKWORKS_PERONE") "../brickworks/build/perone"))

(defn bw [name &opt params]
  (daw/plugin (string root "/" name "/build/bw_example_" name ".perone") params))

(defn seconds [beats] (music/seconds bpm beats))

(defn control [node beat parameter value]
  (daw/param node (seconds beat) parameter value))

(defn sweep [node parameter beat duration from to]
  (music/curve (seconds beat) (seconds duration) (* 32 duration)
    |(music/lerp from to $)
    (fn [time value] (daw/param node time parameter value)))
  (+ beat duration))

(defn note [node beat duration pitch volume]
  # These synths use their volume parameter for accents, not MIDI velocity.
  (control node beat :volume volume)
  (daw/note node (seconds beat) (seconds duration) pitch))

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

(defn thump [beat volume]
  (note kick beat 0.36 32 volume)
  # Coarse tuning is in octaves. Each kick falls two octaves in 80 ms.
  (music/curve (seconds beat) 0.08 24 |(* 2 (math/pow (- 1 $) 3))
    (fn [time value] (daw/param kick time :vco1_coarse value))))

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

# Parts take and return beats; conversion to seconds stays at the daw/* boundary.
(defn band [kind bars start]
  (def break? (= kind :break))
  (def groove? (or (= kind :groove) (= kind :reprise)))
  (for bar 0 bars
    (def beat (+ start (* 4 bar)))
    (def harmony (% bar 4))
    (def pitches (chords harmony))
    (def arps? (or (not= kind :intro) (>= (* 4 bar) intro-rest)))
    (def final? (and (= kind :reprise) (>= bar (/ bars 2))))
    (each pitch pitches (note pad beat 3.5 pitch 68))
    (when arps?
      (music/sequence beat 0.5 [0 2 1 3 2 1 3 2]
        (fn [t degree]
          (def offbeat (= (% (- t beat) 1) 0.5))
          (note arp (+ t (if offbeat 0.045 0)) (if break? 0.4 0.22)
            (+ 12 (pitches degree)) (if offbeat 56 65)))))
    (when groove?
      (each [offset duration interval volume] bassline
        (def degree (if (= interval 10) ([10 11 11 7] harmony) interval))
        (note bass (+ beat offset) duration (+ (roots harmony) degree) volume))
      (each offset [0 1.5 2 3.25] (thump (+ beat offset) (if (= offset 1.5) 88 96)))
      (each offset [1 3] (note snare (+ beat offset 0.018) 0.22 50 84))
      (when (= (% bar 2) 1) (note snare (+ beat 2.75) 0.12 50 48))
      (for i 0 4
        (sweep pad-track :gain (+ beat i) 0.125 0.26 0.12)
        (sweep pad-track :gain (+ beat i 0.125) 0.75 0.12 0.26)))
    (when (or (and (= kind :intro) (= bar (- bars 1))) (and break? (= (% bar 2) 0)))
      (thump beat 82)
      (note bass beat 2.5 (roots harmony) 74))
    (when (and arps? (not break?))
      (for i 0 8
        (def t (+ beat (* i 0.5) (if (= (% i 2) 1) 0.045 0)))
        (def open? (= i 6))
        (control hat t :vca_decay (if open? 230 38))
        (control hat t :vca_release (if open? 100 25))
        (note hat t (if open? 0.38 0.1) 60 (if (= (% i 2) 1) 96 84)))
      (when final? (note hat (+ beat 3.8) 0.08 60 74)))
    (when (or (= kind :reprise) (and (= kind :groove) (>= bar (/ bars 2))))
      (each [offset duration pitch] (melody harmony)
        (note lead (+ beat offset) duration pitch (if final? 77 73))))
    (when break? (note lead (+ beat 0.5) 2.5 (+ 12 (pitches 3)) 65))
    (when (or (and (= kind :groove) (= (% bar 4) 3))
              (and (= kind :reprise) (= bar (- bars 1))))
      (each offset [3.5 3.75] (note snare (+ beat offset) 0.12 50 65))))
  (+ start (* 4 bars)))

# Notes and their automation share one span and move together.
(defn section [kind bars start]
  (def duration (* 4 bars))
  (music/parallel start [
    (partial band kind bars)
    (fn [t]
      (case kind
        :intro (sweep pad :vcf_cutoff t duration 650 1800)
        :groove (do
          (sweep bass :vcf_cutoff t duration 180 1200)
          (sweep pad-space :wet (+ t duration -1) 1 27 55)
          (sweep lead-space :wet (+ t duration -1) 1 22 48))
        :break (do
          (sweep pad :vcf_cutoff t duration 900 3800)
          (sweep chorus :depth t duration 28 65)
          (control bass t :vcf_cutoff 160)
          (sweep arp :cutoff t duration 900 4200)
          (sweep phaser :center t duration 500 2800)
          (control lead t :vco1_wave 3)
          (sweep pad-space :wet (+ t duration -1) 1 55 27)
          (sweep lead-space :wet (+ t duration -1) 1 48 22))
        :reprise (do
          (sweep pad :vcf_cutoff t duration 1800 900)
          (sweep chorus :depth t (/ duration 2) 65 28)
          (sweep bass :vcf_cutoff t duration 220 1800)
          (sweep bass :vcf_resonance t duration 22 42)
          (sweep drive :distortion t duration 12 30)
          (control lead t :vco1_wave 2)))
      (+ t duration))]))

(defn opening [start]
  (def end (music/serial start [(partial section :intro 4) (partial section :groove 8)]))
  (def enter (+ start intro-rest))
  (sweep arp :cutoff enter (- end enter) 1500 6500)
  end)

(defn piece [start]
  (def end (music/serial start [opening (partial section :break 4) (partial section :reprise 8)]))
  # These two gestures span the whole arrangement after the arpeggio enters.
  (def enter (+ start intro-rest))
  (def duration (- end enter))
  (sweep arp :pulse_width enter duration 24 66)
  (music/curve (seconds enter) (seconds duration) (* 16 duration)
    |(* 65 (math/sin (* 22 math/pi $)))
    (fn [time value] (daw/param arp-pan time :pan value)))
  end)

# Resolve at the arrangement's end and leave six seconds for the reverb tail.
(def finish (piece 0))
(each pitch (chords 0) (note pad finish 1.5 pitch 62))
(note bass finish 1 38 76)
(note lead finish 1.25 74 65)
(thump finish 92)
(sweep master :gain 0 0.125 0 1)
(def end (+ (seconds finish) 6))
(music/curve (- end 2) 1.99 160 |(* (- 1 $) (- 1 $))
  (fn [time value] (daw/param master time :gain value)))
(daw/end end {:format :pcm16 :normalize 0.94})
