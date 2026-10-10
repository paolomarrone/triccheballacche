# Crocevia — 40 bars of electro at 138 BPM, crossing four native plugin formats.
# Requires local Perone plugins, Surge XT CLAP and Brickworks fx_comb VST3.
# A-SID LV2 comes from ../asid; ASID_LV2 can select another current build.
# With the local Surge download: CLAP_PATH=.deps/clap-test-plugins ./build/gui examples/crocevia.janet
(import ../lib/pattern :as p)
(import ../lib/music)

(def bpm 138)
(def beat (/ 60 bpm))
(defn perone [name params] (daw/plugin (string "plugins/" name "/build/plugin.perone") params))

(def kick (perone "drums" {:seed 138001}))
(def snare (perone "drums" {:seed 138003}))
(def hats (perone "drums" {:seed 138007}))
(def metal (perone "drums" {:seed 138011}))
(def bass (perone "synth_mono"
  {:volume 78 :vco1_wave 2 :vco1_pw 33 :vco2_wave 3 :vco2_level 35
   :portamento 18 :vcf_cutoff 7200 :vcf_resonance 8
   :vca_attack 2 :vca_decay 140 :vca_sustain 30 :vca_release 55}))
(def mouth (daw/plugin :mouth {:format :lv2 :id "https://www.orastron.com/asid"
  :path (or (os/getenv "ASID_LV2") "../asid/plugin/lv2/build/asid.lv2")}
  {:cutoff 42 :lfo_amount 12 :lfo_speed 26 :enabled 1}))
(def dirt (perone "shape" {:drive 2.2 :level 0.55 :dc 0.002 :lowpass 0.85}))

# Surge's CLAP controls use native IDs and normalized values.
# These are amp-envelope decay, sustain, release and scene pan, respectively.
(def decay :3946337085)
(def sustain :874858726)
(def release :3817344714)
(def pan :92522431)
(def stabs (daw/plugin :stabs {:format :clap :id "org.surge-synth-team.surge-xt"}
  {decay 0.39 sustain 0.18 release 0.31 :2005836581 0.015}))

(def bell (perone "synth_mono"
  {:volume 72 :vco1_wave 3 :vco2_wave 3 :vco2_coarse 1 :vco2_level 32
   :vco2_fine 6 :vcf_cutoff 5400 :vca_attack 2
   :vca_decay 210 :vca_sustain 0 :vca_release 180}))
# The comb becomes a dotted-eighth delay. Delay values span 0..1000 ms;
# signed coefficients map to 0..1, with 0.5 representing zero feedback.
(def feedback :1414173210)
(def dub (daw/plugin :dub {:format :vst3 :id "3347F70263444985B2010E0E7FD7CD3F"}
  {:310048330 (* beat 0.75) :313543176 (* beat 0.75)
   :356165414 1 :342145884 0.68 feedback 0.73}))

(def bass-track (daw/track bass {:name "Acid / LV2" :gain 0.55 :effects [mouth dirt]}))
(def chord-track (daw/track stabs {:name "Stabs / CLAP" :gain 0.36}))
(def bell-track (daw/track bell {:name "Dub / VST3" :gain 0.40 :pan 0.24 :effects [dub]}))
(def master (daw/master
  (daw/mix [(daw/track kick {:name "Kick" :gain 0.90})
            (daw/track snare {:name "Snare" :gain 0.64 :pan -0.08})
            (daw/track hats {:name "Hats" :gain 0.27 :pan 0.36})
            (daw/track metal {:name "Metal" :gain 0.22 :pan -0.42
              :effects [(perone "echo" {:time1 (* beat 250) :time2 (* beat 750)
                                       :time3 (* beat 1250) :level1 0.24 :level2 0.16 :level3 0.09})]})
            bass-track chord-track bell-track])
  {:effects [(perone "shape" {:drive 1.35 :level 0.88 :dc 0.0005 :lowpass 0.94})]}))
(daw/output master)
(daw/tempo bpm)

(defn note [items node t duration pitch velocity]
  (array/push items [t (+ t duration) [:note node pitch velocity]]))
(defn control [items node t key value]
  (array/push items [t t [:param node key value]]))
(defn ramp [duration node key from to]
  (p/map |[:param node key $]
    (p/curve duration (max 8 (math/ceil (* duration 16))) |(music/lerp from to $))))

(def roots [38 34 41 36])
(def chords [[62 65 69 72] [62 65 69 70] [60 64 67 69] [60 62 67 70]])
(def riff [0 nil 0 12 nil 7 0 nil 10 0 nil 12 7 nil 3 nil])
(def hook (p/steps 0.5 [0 nil 2 1 nil 3 2 nil]))

(defn section [kind bars]
  (def items @[])
  (def intro (= kind :intro))
  (def floating (= kind :float))
  (def busy (or (= kind :switch) (= kind :peak)))
  (def outro (= kind :outro))
  (def duration (* bars 4))
  (control items mouth 0 :lfo_amount (if busy 32 12))
  (control items stabs 0 sustain (if floating 0.55 0.18))
  (control items stabs 0 release (if floating 0.48 0.31))
  (control items dirt 0 :drive (if busy 3.6 2.2))
  (for bar 0 bars
    (def t (* bar 4))
    (def harmony (% bar 4))
    (def root (roots harmony))
    (def chord (chords harmony))
    (def fill (= (% bar 4) 3))
    (def pulse (if floating [0 2.5] (if (= kind :switch) [0 1.5 2 3.25] [0 1 2 3])))
    (each offset pulse
      (note items kick (+ t offset) 0.01 0 (if (= offset 0) 117 102)))
    (unless intro
      (each offset (if floating [3] [1 3])
        (note items snare (+ t offset 0.012) 0.01 1 (if floating 62 108))))
    (for step 0 (if floating 8 16)
      (def at (+ t (* step (if floating 0.5 0.25)) (if (odd? step) 0.025 0)))
      (unless (and intro (odd? step))
        (note items hats at 0.01 (if (= (% step 8) 6) 3 2)
          (if (even? step) 63 (if busy 41 28)))))
    (when (and busy fill)
      (eachp [i offset] [2.75 3.25 3.5 3.625 3.75 3.875]
        (note items snare (+ t offset) 0.01 1 (+ 35 (* i 10)))))
    (when (or busy floating)
      (each offset [0.75 2.25 3.5]
        (note items metal (+ t offset) 0.01 (if floating 4 5) (if floating 31 57))))
    (if floating
      (note items bass t 2.8 root 95)
      (eachp [step interval] riff
        (when (and interval (or (not intro) (even? step)))
          (def at (+ t (* step 0.25)))
          (control items bass at :volume (if (= (% step 4) 0) 83 66))
          (control items bass at :portamento (if (and busy (= step 11)) 90 15))
          (note items bass at (if (= step 11) 0.34 0.17) (+ root interval) 100))))
    (unless (and intro (< bar 2))
      (each offset (if floating [0.5 2.5] (if outro [0.75] [0.75 1.75 2.5]))
        (each pitch chord
          (note items stabs (+ t offset) (if floating 1.25 0.23) pitch (if busy 91 76)))))
    (when (or busy floating outro)
      (def phrase (if (odd? bar) (p/reverse hook) hook))
      (each [a b degree] (p/flatten phrase)
        (note items bell (+ t a) (* 0.55 (- b a)) (+ 12 (chord degree)) (if floating 85 98))))
    # Smooth gain dips make room for the kick without cutting the whole mix.
    (unless floating
      (each offset pulse
        (each [node level] [[bass-track 0.55] [chord-track 0.36]]
          (each [a _ gain] (p/flatten (p/curve 0.65 16 |(* level (+ 0.34 (* 0.66 $)))))
            (control items node (+ t offset a) :gain gain))))))
  (p/parallel [(p/events duration items)
    (ramp duration mouth :cutoff (if floating 32 28) (if busy 88 62))
    (ramp duration mouth :lfo_speed 20 (if busy 78 40))
    (ramp duration dub feedback (if floating 0.78 0.70) (if floating 0.83 0.77))
    (ramp duration stabs decay (if floating 0.49 0.36) (if busy 0.46 0.42))
    (p/map |[:param stabs pan (+ 0.5 (* 0.22 (math/sin (* 4 math/pi $))))]
      (p/curve duration (* duration 16) identity))]))

(def song (p/serial [(section :intro 4) (section :groove 8) (section :switch 8)
                    (section :float 4) (section :peak 12) (section :outro 4)]))
(daw/schedule 0 bpm song)
# A short opening fade and four seconds for the delay to resolve.
(daw/schedule 0 60 (ramp 0.025 master :gain 0 1))
(def finish (* beat (song :length)))
(daw/schedule (+ finish 1) 60 (ramp 2.95 master :gain 1 0))
(daw/end (+ finish 4) {:format :pcm16 :normalize 0.94})
