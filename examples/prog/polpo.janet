# Il polpo a sette gomiti — 30 seconds for an imaginary prog band.
# Times are seconds; rhythm and harmony are ordinary Janet functions.
(def patches [ # wave, cutoff, resonance, attack, decay, sustain, release, glide
  [2 700 12 2 95 65 35 0]
  [1 3800 8 2 90 30 24 0] [1 3500 10 2 100 32 28 0]
  [1 4200 18 3 110 90 70 9]
  [3 4800 5 8 330 42 310 0] [3 5200 5 8 330 42 310 0]
  [3 5600 5 8 330 42 310 0] [2 3400 24 3 120 65 80 4]])
(def ids [:vco1_wave :vcf_cutoff :vcf_resonance :vca_attack :vca_decay :vca_sustain :vca_release :portamento])
(def pans [0 -0.78 0.78 0.08 -0.55 0 0.55 -0.3])
(def gains [0.73 0.28 0.28 0.58 0.23 0.21 0.23 0.34])
(def band @[])
(for tr 0 8
  (def keys (<= 4 tr 6))
  (def params @{:vco1_pw (if (= tr 7) 28 47) :vco2_wave (if (= tr 0) 3 2)
    :vco2_coarse (if (= tr 0) -1 (if keys 1 0)) :vco2_fine (case tr 1 -7 2 7 3)
    :vco2_level (if (= tr 0) 65 (if keys 45 52)) :vcf_contour (if (= tr 0) 55 22)
    :vcf_decay 105 :vcf_sustain 15})
  (for i 0 8 (put params (ids i) ((patches tr) i)))
  (def synth (daw/plugin "examples/synth_mono/plugin.so" params))
  (def effects @[])
  (when (= tr 0) (array/push effects (daw/plugin "examples/shape/plugin.so" {:drive 2 :level 0.7})))
  (when (<= 1 tr 2) (array/push effects (daw/plugin "examples/shape/plugin.so"
    {:drive 8 :dc 0.013 :lowpass 0.34})))
  (def wet (if (>= tr 3) 0.3 0.045))
  (array/push effects (daw/plugin "examples/echo/plugin.so"
    {:level1 wet :level2 (/ wet 2) :level3 (/ wet 3)}))
  (daw/track synth {:pan (pans tr) :gain (gains tr) :effects effects})
  (array/push band synth))
(def [bass left right lead key1 key2 key3 counter] band)
(def param daw/param)
(def drum-notes {:kick 0 :snare 1 :hat 2 :open-hat 3 :crash 4 :tom-high 5 :tom-low 6})
(def drum-band @[])
(for i 0 7
  (def source (daw/plugin "examples/drums/plugin.so" {:seed (+ 7368556 i)}))
  (def effects (if (= i 0) [] [(daw/plugin "examples/echo/plugin.so"
    {:level1 0.07 :level2 0.035 :level3 (/ 0.07 3)})]))
  (daw/track source {:pan ([0 -0.08 0.35 0.4 -0.55 -0.4 0.45] i) :effects effects})
  (array/push drum-band source))
(defn drum [kind t strength]
  (def pitch (drum-notes kind))
  (daw/note (drum-band pitch) t 0.001 pitch (math/floor (+ 0.5 (* strength 127)))))
(def master (daw/master {:effects [(daw/plugin "examples/shape/plugin.so" {:drive 1.35 :dc 0.002})]}))
(defn note [tr t duration pitch volume]
  # This synth uses parameter 0 for volume, not MIDI velocity.
  (param tr t :volume volume)
  (daw/note tr t duration pitch))
(defn scale [degree]
  (+ ([0 2 3 5 7 8 11] (% degree 7)) (* 12 (math/floor (/ degree 7)))))
(defn riff [t duration pitch volume]
  (note left t duration pitch volume)
  (note right (+ t 0.004) (* duration 0.97) (+ pitch 7) (- volume 3)))
(defn chord [t duration root third seventh volume]
  (note key1 t duration (+ root 24) volume)
  (note key2 (+ t 0.006) duration (+ root 24 third) (- volume 2))
  (note key3 (+ t 0.011) duration (+ root 24 seventh) (- volume 4)))

(def hook [0 0 7 1 0 10 4])
(def spiral [0 2 4 6 5 3 1 7 4 8 6 3 9 5])
(defn section [start part roots eighths bpm]
  (def step (/ 30 bpm))
  (def bars (length roots))
  (for b 0 bars
    (def bar (+ start (* b eighths step)))
    (def root (roots b))
    (when (or (= b 0) (and (= part 3) (= b 4))) (drum :crash bar 0.33))
    (chord bar (* step (if (= part 2) 8.8 1.35)) root
      (if (= part 2) 4 3) (if (= part 2) 11 10) (if (= part 2) 69 49))
    (for j 0 eighths
      (def t (+ bar (* j step)))
      (def accent (or (= j 0) (= j 3) (= j (- eighths 2))))
      (def rest (or (and (= part 1) (= b (- bars 1)) (= j (- eighths 1)))
                    (and (= part 4) (= j 8))))
      (unless rest
        (drum (if (= j (- eighths 1)) :open-hat :hat) t (if accent 0.16 0.10))
        (when (and (not= part 2) (= (% j 2) 0)) (drum :hat (+ t (* 0.5 step)) 0.055))
        (when (or accent (and (= part 3) (= j 4))) (drum :kick t (if accent 0.82 0.55)))
        (when (or (= j 3) (and (not= part 2) (= j (- eighths 1)))) (drum :snare (+ t 0.002) 0.78))
        (when (and (= j 2) (not= part 2)) (drum :snare (+ t (* 0.72 step)) 0.17))
        (if (= part 2)
          (do
            (when (= (% j 2) 0) (note bass t (* 1.4 step) (+ root (if (= j 6) 7 0)) 68))
            (note counter t (* 0.55 step) (+ root 24 ([0 7 11 14 18 14 11 7 4 11] j)) 56)
            (when (= j 0) (note lead (+ t step) (* 4.8 step) (+ root 28) 64))
            (when (= j 7) (riff t (* 1.1 step) (+ root 19) 52)))
          (do
            (def interval (case part
              1 (if (< j 7) (hook j) (if (= j 7) 6 11))
              3 (case j 4 7 6 11 0)
              (hook (% j 7))))
            (note bass t (* 0.72 step) (+ root (if accent 0 interval)) (if accent 77 68))
            (riff t (* step (if accent 0.64 0.43)) (+ root 12 interval) (if accent 74 65))
            (when (and (= part 0) (= (% b 2) 1) (>= j 3))
              (note lead t (* 0.7 step) (+ root 24 (hook (- 6 j))) 67))
            (when (and (= part 1) (= (% j 3) 0))
              (note lead t (* 0.4 step) (+ root 24 (scale (% (+ j b) 10))) 67)
              (note counter (+ t (* 0.5 step)) (* 0.38 step) (+ root 24 (scale (% (+ j b 2) 10))) 59))
            (when (= part 4) (note lead t (* 0.75 step) (+ root 24 (hook (% j 7))) 71))))))
    (when (= part 3)
      (param lead bar :vcf_cutoff (+ 3300 (* 430 b)))
      (for j 0 14
        (note lead (+ bar (/ (* j step) 2)) (* step 0.46)
          (+ root 24 (scale (spiral (% (+ j (* 2 b)) 14)))) (+ 68 (% j 3))))
      (when (or (= b 3) (= b 5))
        (for j 0 6
          (note counter (+ bar (* (+ 5 (/ j 3)) step)) (* step 0.26) (+ root 36 (scale (- 5 j))) 57))))
    (when (or (= b (- bars 1)) (and (= part 3) (= (% b 2) 1)))
      (for j 0 4
        (drum (if (< j 2) :tom-high :tom-low) (+ bar (* (+ (- eighths 1) (* j 0.25)) step))
          (+ 0.45 (* j 0.06))))))
  (+ start (* bars eighths step)))

(var t (section 0 0 [40 40 41 40] 7 154))
(set t (section t 1 [40 43 42 35] 9 166))
(set t (section t 2 [36 34] 10 128))
(set t (section t 3 [40 40 38 35 36 40] 7 184))
(set t (section t 4 [40 41] 11 176))
(def ending [40 42 46 47 52])
(for i 0 5
  (def hit (+ t (/ (* i 30) 176)))
  (note bass hit 0.09 (- (ending i) (if (= i 4) 12 0)) 78)
  (riff hit 0.085 (+ (ending i) 12) 77)
  (note lead hit 0.085 (+ (ending i) 36) 69)
  (drum :kick hit 0.84) (drum :snare hit 0.56))
(set t (+ t (/ (* 5 30) 176)))
(each tr band (param tr t :vca_release 650))
(note bass t 0.62 28 79) (riff t 0.56 52 76)
(note lead t 0.66 88 62) (chord t 0.72 40 4 11 66)
(drum :kick t 0.95) (drum :snare t 0.85) (drum :crash t 0.55)
# The master fade and normalization belong to this composition, not to the host.
(for i 0 130
  (def gain (- 1 (/ i 130)))
  (param master (+ 29.35 (* i 0.005)) :gain (* gain gain)))
(param master (- 30 (/ 1 44100)) :gain 0)
(daw/end 30 {:format :pcm16 :normalize 0.94})
