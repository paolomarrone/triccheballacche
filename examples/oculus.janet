# Oculus — two-part techno trance. 150 BPM, 4/4, 100 bars.
# Both complete MIDI voices are the lead material, including the lower entry.
# The rhythm section and transformations are new. See oculus.md for the form.
# ./build/cli examples/oculus.janet renders/oculus.wav 48000
(import ../lib/pattern :as p)
(import ./sources/oculus-non-vidit :as source)

(def bpm 150)
(def beat-ms (/ 60000 bpm))
(def tracks @[])
(def layers @[])
(defn synth [id params] (daw/plugin id "plugins/synth_mono/build/plugin.perone" params))
(defn shape [params] (daw/plugin "plugins/shape/build/plugin.perone" params))
(defn echo [params] (daw/plugin "plugins/echo/build/plugin.perone" params))
(defn track [node name gain pan effects]
  (def out (daw/track node {:name name :gain gain :pan pan :effects effects}))
  (array/push tracks out)
  out)

# Each original part has its own pair of detuned synths and stereo position.
# P2 has a pulse core; the saw layers are raised to balance their lower RMS.
(for part 0 2
  (for side 0 2
    (def voice (synth (keyword (string "p" (+ part 1) "-" side))
      {:volume (if (= part 0) 79 82)
       :vco1_wave (if (= part 0) 1 2) :vco1_pw 43 :vco1_fine (if (= side 0) -6 7)
       :vco2_wave 1 :vco2_fine (if (= side 0) 11 -12) :vco2_level 71
       :vco3_wave 1 :vco3_fine (if (= side 0) -17 18) :vco3_level 60
       :vcf_cutoff (if (= part 0) 6200 4300) :vcf_resonance (if (= part 0) 7 15)
       :vcf_contour 9 :vcf_decay 180 :vcf_sustain 28
       :vca_attack 3 :vca_decay 190 :vca_sustain 82 :vca_release 45 :portamento 0}))
    (def gain (([[0.7 0.4] [0.49 0.23]] part) side))
    (def lane (track voice (string (if (= part 0) "P1 / white voltage " "P2 / black voltage ") (+ side 1))
      gain (([[-0.43 0.1] [0.43 -0.1]] part) side)
      [(shape {:drive 1.12 :level 0.91 :dc 0.003})
       (echo {:time1 (* beat-ms (if (= part 0) 0.75 0.5))
              :time2 (* beat-ms 1.5) :time3 (* beat-ms 2.25)
              :level1 (if (= side 0) 0.13 0.2) :level2 0.065 :level3 0.025})]))
    (array/push layers {:part part :voice voice :track lane :gain gain})))

(def kick (daw/plugin :kick "plugins/drums/build/plugin.perone" {:seed 41501}))
(def snare (daw/plugin :snare "plugins/drums/build/plugin.perone" {:seed 41502}))
(def hats (daw/plugin :hats "plugins/drums/build/plugin.perone" {:seed 41503}))
(def metal (daw/plugin :metal "plugins/drums/build/plugin.perone" {:seed 41504}))
(def crash (daw/plugin :crash "plugins/drums/build/plugin.perone" {:seed 41505}))
(track kick "Kick / four on the floor" 1.72 0 [(shape {:drive 1.5 :level 0.94})])
(track snare "Snare / warehouse" 0.91 -0.035
  [(shape {:drive 1.9 :level 0.78 :dc 0.008})
   (echo {:time1 12 :time2 24 :time3 39 :level1 0.19 :level2 0.1 :level3 0.04})])
(track hats "Hats / offbeat" 0.35 0.27 [])
(track metal "Percussion / ricochet" 0.51 -0.31
  [(shape {:drive 1.8 :level 0.79 :dc 0.009})])
(track crash "Crash / entrance" 0.25 0.12 [])

(def bass (synth :bass
  {:volume 85 :vco1_wave 2 :vco1_pw 34 :vco2_wave 1 :vco2_level 37
   :vcf_cutoff 750 :vcf_resonance 15 :vcf_contour 38 :vcf_decay 80 :vcf_sustain 0
   :vca_attack 2 :vca_decay 100 :vca_sustain 22 :vca_release 12 :portamento 5}))
(def bass-track (track bass "Bass / rolling sixteen" 0.82 0
  [(shape {:drive 2.2 :level 0.77 :dc 0.009 :lowpass 0.78})]))
(def sub (synth :sub
  {:volume 87 :vco1_wave 3 :vcf_cutoff 145 :vcf_resonance 0
   :vca_attack 3 :vca_decay 145 :vca_sustain 34 :vca_release 18}))
(def sub-track (track sub "Sub / offbeat" 0.83 0 []))

(def acid (synth :acid
  {:volume 76 :vco1_wave 1 :vco2_level 0 :vco3_level 0
   :vcf_cutoff 640 :vcf_resonance 63 :vcf_contour 39 :vcf_decay 95 :vcf_sustain 0
   :vca_attack 2 :vca_decay 120 :vca_sustain 18 :vca_release 14 :portamento 28}))
(def acid-track (track acid "Acid / unstable spiral" 0.2 -0.23
  [(shape {:drive 2.4 :level 0.65 :dc 0.015 :lowpass 0.8})
   (echo {:time1 (* beat-ms 0.75) :time2 (* beat-ms 1.5) :time3 (* beat-ms 2.25)
           :level1 0.22 :level2 0.1 :level3 0.045})]))
(def arp (synth :arp
  {:volume 69 :vco1_wave 2 :vco1_pw 21 :vco2_wave 1 :vco2_coarse 1 :vco2_level 30
   :vcf_cutoff 4900 :vcf_resonance 19 :vcf_contour 15 :vcf_decay 60
   :vca_attack 2 :vca_decay 70 :vca_sustain 0 :vca_release 22}))
(def arp-track (track arp "Arp / centrifugal" 0.21 0.35
  [(echo {:time1 (* beat-ms 0.75) :time2 (* beat-ms 1.5) :time3 (* beat-ms 3)
          :level1 0.25 :level2 0.11 :level3 0.04})]))
(def noise (synth :noise
  {:volume 62 :vco1_level 0 :noise_color 1 :noise_level 86
   :vcf_cutoff 900 :vcf_resonance 21 :vca_attack 650 :vca_decay 300
   :vca_sustain 75 :vca_release 90}))
(def noise-track (track noise "Noise / fuse" 0.12 0
  [(shape {:drive 1.2 :level 0.8 :dc 0.03})]))

(def master (daw/master (daw/mix tracks)
  {:gain 0.78 :effects [(shape {:drive 0.94 :level 0.95 :dc 0.0008})]}))
(daw/output master)
(def items @[])
(defn control [t node key value] (array/push items [t t [:param node key value]]))
(defn note [t duration node pitch &opt velocity]
  (array/push items [t (+ t duration) [:note node pitch (math/floor (or velocity 100))]]))
(defn ramp [t duration node key from to &opt steps]
  (default steps 32)
  (for i 0 (+ steps 1)
    (def x (/ i steps))
    (control (+ t (* duration x)) node key (+ from (* (- to from) x)))))
(defn duck [t]
  (each [node gain] [[bass-track 0.82] [sub-track 0.83]]
    (control t node :gain (* gain 0.13))
    (for i 1 13
      (def x (/ i 12))
      (control (+ t (* 0.36 x)) node :gain (* gain (+ 0.13 (* 0.87 x x)))))))
(defn thump [t velocity] (note t 0.01 kick 0 velocity) (duck t))

# Whole parts, not a single selected theme: each layer follows its own MIDI line.
(defn quote-parts [start from until scale]
  (each layer layers
    (each [at duration pitch] (source/parts (layer :part))
      (def a (max at from))
      (def b (min (+ at duration) until))
      (when (< a b)
        (note (+ start (* (- a from) scale)) (* (- b a) scale) (layer :voice) pitch)))))
(defn source-pitch [part beat]
  (var pitch (if (= part 0) 76 62))
  (each [at _ key] (source/parts part) (when (<= at beat) (set pitch key)))
  pitch)
(defn register [pitch minimum] (+ minimum (% (- pitch minimum) 12)))
(defn source-time [kind beat]
  (case kind
    :acid (% (* beat 2) 64)
    :break (+ 64 beat)
    :build (+ 112 beat)
    :end (+ 112 beat)
    beat))

(def form [[:ignition 4] [:duet 32] [:acid 16] [:break 8]
           [:build 4] [:rave 32] [:end 4]])
(var bar-offset 0)
(each [kind bars] form
  (def start (* bar-offset 4))
  (def beats (* bars 4))
  (def intro (= kind :ignition))
  (def wild (= kind :acid))
  (def quiet (= kind :break))
  (def build (= kind :build))
  (def full (= kind :rave))
  (def ending (= kind :end))
  (each layer layers
    (def lower (= (layer :part) 1))
    (control start (layer :track) :gain (* (layer :gain) (if wild 0.7 1)))
    (control start (layer :voice) :vca_attack (if quiet 6 3))
    (control start (layer :voice) :vca_release (if wild 18 45))
    (control start (layer :voice) :vca_sustain (if wild 61 82))
    (control start (layer :voice) :vco1_wave (if quiet (if lower 3 2) (if lower 2 1)))
    (ramp start beats (layer :voice) :vcf_cutoff
      (if (or intro quiet build) 1900 (if lower 4300 6200))
      (if quiet 3200 (if lower 6300 9200))))
  (control start acid-track :gain (if wild 0.43 (if quiet 0.13 (if full 0.23 0.17))))
  (control start arp-track :gain (if full 0.24 0.17))
  (ramp start beats bass :vcf_cutoff (if quiet 350 670) (if build 1800 1150))
  (control start acid :vco1_wave (if wild 2 1))
  (control start acid :vco1_pw (if wild 23 50))

  (case kind
    :ignition (quote-parts start 0 16 1)
    :duet (quote-parts start 0 128 1)
    :acid (do (quote-parts start 0 64 0.5)
              (quote-parts (+ start 32) 0 64 0.5))
    :break (quote-parts start 64 96 1)
    :build (quote-parts start 112 128 1)
    :rave (quote-parts start 0 128 1)
    :end (quote-parts start 112 128 1))
  (when (or (= kind :duet) wild full) (note start 0.01 crash 4 105))
  (when build
    (note start (- beats 0.8) noise 62)
    (ramp start (- beats 0.8) noise :vcf_cutoff 650 12500)
    (ramp start (- beats 0.8) noise-track :gain 0.06 0.25))

  (for bar 0 bars
    (def t (+ start (* bar 4)))
    (def local (* bar 4))
    (def root (register (source-pitch 1 (source-time kind local)) 33))
    (def low (register root 26))
    (def stop (if (and build (= bar (- bars 1))) 3.25 4))
    (def closing (and ending (>= bar 2)))
    (def active (not (or quiet closing)))
    (each at (cond quiet (if (even? bar) [0] [])
                   closing (if (= bar 2) [0] [])
                   [0 1 2 3])
      (when (< at stop) (thump (+ t at) (if quiet 90 (if (= at 0) 127 119)))))
    (when active
      (each at [1 3]
        (when (< at stop) (note (+ t at) 0.01 snare 1 (if intro 102 121))))
      (for step 0 16
        (def at (* step 0.25))
        (when (and (< at stop) (or (even? step) wild full))
          (note (+ t at) 0.01 hats (if (= (% step 4) 2) 3 2)
            (if (= (% step 4) 2) 84 (if (even? step) 43 25)))))
      (for beat 0 4
        (eachp [i at] (if intro [0.5] [0.25 0.5 0.75])
          (def offset (+ beat at))
          (when (< offset stop)
            (control (+ t offset) bass :volume (if (= at 0.5) 85 (if (= at 0.25) 70 77)))
            (note (+ t offset) (if (= at 0.5) 0.21 0.12) bass root)))
        (when (< (+ beat 0.5) stop)
          (note (+ t beat 0.5) 0.38 sub low)))
      (when (and (= (% bar 8) 7) (not build))
        (for step 0 (if (or wild full) 6 4)
          (note (+ t 3 (* step (if (or wild full) (/ 1 6) 0.25))) 0.01 metal
            (if (even? step) 6 1) (+ 63 (* step 10))))))

    # Acid follows pitch material from the lower source line, with 5/7-step accents.
    (unless closing
      (for step 0 16
        (def at (* step 0.25))
        (when (and (< at stop)
                   (cond quiet (= (% step 4) 2) intro (= (% step 4) 2) true))
          (def key (register (source-pitch 1 (source-time kind (+ local at))) 45))
          (def pulse (/ (% (+ step (* bar 3)) (if wild 7 11)) (if wild 6 10)))
          (control (+ t at) acid :volume (if (= (% step 5) 0) 81 70))
          (control (+ t at) acid :portamento (if (= (% step 7) 0) 65 18))
          (control (+ t at) acid :vcf_cutoff (+ 360 (* (if wild 6700 (if full 3900 2200)) pulse pulse)))
          (note (+ t at) (if (= (% step 7) 0) 0.22 0.14) acid
            (+ key (if (and wild (= (% step 7) 5)) 12 0))))))
    (when (or wild (and full (>= bar 8)))
      (control t arp-track :pan (if (even? bar) 0.4 -0.35))
      (for step 0 (if (and full (>= bar 24)) 12 8)
        (def at (* step (if (and full (>= bar 24)) (/ 1 3) 0.5)))
        (def key (source-pitch (% step 2) (source-time kind (+ local at))))
        (note (+ t at) 0.1 arp (+ key (if (= (% step 3) 0) 12 0)))))
    (when quiet
      (note (+ t 0.5) 0.75 sub low)
      (each at [1.5 3.5] (note (+ t at) 0.01 hats 2 51)))
    (when build
      (for step 0 (if (< bar 2) 8 16)
        (def at (* step (if (< bar 2) 0.5 0.25)))
        (when (< at stop)
          (note (+ t at) 0.01 metal 1 (+ 43 (* bar 11) (* (% step 4) 6)))))))
  # Explicit silence before the final drop; the original duet then starts again.
  (when build
    (ramp (+ start (- beats 0.65)) 0.1 master :gain 0.78 0)
    (control (+ start beats) master :gain 0.78))
  (set bar-offset (+ bar-offset bars)))

# Both MIDI parts settle on their original final Ds; one final kick supports them.
(def beats (* bar-offset 4))
(def cadence (- beats 8))
(note cadence 6.8 sub 26)
(note cadence 6.8 bass 38)
(control cadence bass :vcf_cutoff 330)
(def finish (/ (* beats 60) bpm))
(def duration (+ finish 1.2))
(daw/schedule 0 bpm (p/events beats items))
(daw/schedule finish 60
  (p/map |[:param master :gain (* 0.78 (math/pow (- 1 $) 2))] (p/curve 1.1 96 |$)))
(daw/end duration {:format :pcm16 :normalize 0.94})
