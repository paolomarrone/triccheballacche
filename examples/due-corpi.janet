# Due corpi — 28 bars at 144 BPM, about 51 seconds including the ending.
# Samplo's original struck-metal bank meets Vitsel's dry synth bass and answers.
# Prepare/render on Android ARM64: python tools/due-corpi.py --render
# The helper supplies CLAP_PATH and an isolated Samplo instruments directory.
(import ../lib/pattern :as p)
(import ../lib/music)

(assert (= :file (os/stat "instruments/01-due-corpi.sfz" :mode))
  "Prepare and run this example with python tools/due-corpi.py --render (or --gui)")
(def bpm 144)
(def beat (/ 60 bpm))
(def pickup 0.2)
(defn bw [id name params]
  (daw/plugin id (string "build/library/brickworks/" name "/build/bw_example_" name ".perone") params))
(defn drum [id seed] (daw/plugin id "plugins/drums/build/plugin.perone" {:seed seed}))

# These releases expose normalized float controls and integer enum controls.
# Convert physical envelope times and cutoff frequencies to their NIH-plug ranges.
(defn norm [value low high exponent] (math/pow (/ (- value low) (- high low)) exponent))
(def gain :3165055)
(def attack :96929)
(def decay :99330)
(def sustain :114257)
(def release :112793)
(def cutoff :97128169)

(def sampler (daw/plugin :tines {:format :clap :id "dev.mlm-games.samplo"}
  {:3237446 1 :1336493441 15 :863781340 0
   gain 0.39 attack (norm 1.5 0 2000 0.3) decay (norm 480 1 4000 0.3)
   sustain 0.42 release (norm 240 1 8000 0.3) :2119957241 0.82}))
(def bass (daw/plugin :bass {:format :clap :id "dev.example.vitsel"}
  {:3642105 1 :1336493441 3 :967896348 0.21 :812278777 0.048
   :863781340 1 :97142087 0.34 gain 0.28
   attack (norm 2 0 2000 2.5) decay (norm 160 1 4000 2.5)
   sustain 0.42 release (norm 46 1 8000 2.5) cutoff (norm 720 20 20000 0.2)}))
(def reply (daw/plugin :reply {:format :clap :id "dev.example.vitsel"}
  {:3642105 2 :1336493441 7 :967896348 0.32 :812278777 0.17
   :863781340 1 :97142087 0.24 gain 0.19
   attack (norm 2 0 2000 2.5) decay (norm 85 1 4000 2.5)
   sustain 0.08 release (norm 90 1 8000 2.5) cutoff (norm 3400 20 20000 0.2)}))
(def kick (drum :kick 144011))
(def snare (drum :snare 144013))
(def hats (drum :hats 144017))

(def tines-track (daw/track sampler {:name "Samplo / lamelle" :gain 0.74 :pan -0.23
  :effects [(bw :tines-hp "fx_hp1" {:cutoff 180})]}))
(def bass-track (daw/track bass {:name "Vitsel / gomma" :gain 0.57 :effects
  [(bw :bass-satur "fx_satur" {:gain 1.65}) (bw :bass-hp "fx_hp1" {:cutoff 33})]}))
(def reply-track (daw/track reply {:name "Vitsel / risposta" :gain 0.35 :pan 0.34}))
(def echo (daw/through sampler (bw :tine-echo "fx_delay" {:delay (* beat 750)})))
(def echo-track (daw/track echo {:name "Samplo / riflesso" :gain 0.21 :pan 0.65
  :effects [(bw :echo-hp "fx_hp1" {:cutoff 1250})
            (bw :echo-space "fx_reverb" {:predelay 18 :bandwidth 7400 :damping 4300 :decay 55 :wet 32})]}))
(def master (daw/master (daw/mix
  [(daw/track kick {:name "Kick" :gain 0.94})
   (daw/track snare {:name "Snare" :gain 0.50 :pan -0.04})
   (daw/track hats {:name "Hat" :gain 0.25 :pan 0.26})
   bass-track tines-track reply-track echo-track])
  {:gain 0.72 :effects [(bw :master-hp "fx_hp1" {:cutoff 25})]}))
(daw/output master)
(daw/tempo bpm)

(def notes @[])
(defn note [node start duration pitch velocity]
  (array/push notes [start (+ start duration) [:note node pitch velocity]]))
(defn control [node start parameter value]
  (array/push notes [start start [:param node parameter value]]))

# D minor / B-flat / G minor / A, with a small chromatic turn in the answer.
(def roots [38 34 43 45])
(def phrases [[74 77 81 79 76 77] [74 77 82 81 77 74]
              [74 79 82 81 77 79] [73 76 81 79 76 73]])
(def rhythm [0 0.75 1.5 2.25 2.75 3.5])
(def bass-riff [[0 0] [0.75 0] [1.5 12] [2 0] [2.75 7] [3.5 10]])

# Intro 4 / groove 8 / suspended exchange 4 / variation 8 / coda 4.
(for bar 0 28
  (def t (* bar 4))
  (def intro (< bar 4))
  (def suspended (<= 12 bar 15))
  (def busy (<= 16 bar 23))
  (def coda (>= bar 24))
  (def final (= bar 27))
  (def h (% bar 4))
  (def root (if final 38 (roots h)))
  (def phrase (if final (phrases 0) (phrases h)))
  (def fill (= h 3))
  (def kicks (cond final [0] suspended [] (< bar 2) []
    (and busy fill) [0 1.5 2.5 3.25] [0 1 2 3]))
  (control bass t cutoff (norm (if busy 1400 (if suspended 360 620)) 20 20000 0.2))

  (each at kicks
    (note kick (+ t at) 0.02 0 (if (= at 0) 116 101))
    # Short volume recoveries make space for the kick without an external sidechain.
    (each [node level] [[bass-track 0.57] [tines-track 0.74]]
      (for i 0 9
        (control node (+ t at (* i 0.055)) :gain (* level (+ 0.35 (* 0.65 (/ i 8))))))))

  (unless (or intro suspended final)
    (each at [1 3] (note snare (+ t at 0.018) 0.02 1 103))
    (when (and fill (not coda))
      (eachp [i at] [3.5 3.75 3.875]
        (note snare (+ t at) 0.015 1 (+ 37 (* i 12))))))
  (unless (or (< bar 2) final)
    (for step 0 (if suspended 4 16)
      (def at (+ t (* step (if suspended 1 0.25)) (if (odd? step) 0.026 0)))
      (when (or busy (even? step) (= (% step 8) 7))
        (note hats at 0.015 (if (= (% step 8) 6) 3 2)
          (if (even? step) 64 35)))))

  (unless (< bar 2)
    (cond
      final (note bass t 2.1 root 95)
      suspended (note bass (+ t 0.5) 1.4 root 78)
      (each [at interval] bass-riff
        (note bass (+ t at) (if (= at 2.75) 0.27 0.20) (+ root interval) 103))))

  (if final
    (eachp [i pitch] [74 77 81 86]
      (note sampler (+ t (* i 0.18)) 1.5 pitch (- 100 (* i 6))))
    (eachp [i at] rhythm
      (unless (and coda (odd? i))
        (def pitch (phrase (if (and busy (odd? bar)) (- 5 i) i)))
        (note sampler (+ t at) (if suspended 0.52 0.29) pitch
          (if (even? i) 104 73))
        (when (and busy (= i 4))
          (note sampler (+ t at 0.125) 0.08 (+ pitch 12) 55)))))

  (when (and (>= bar 6) (not final) (or suspended busy fill))
    (eachp [i at] [0.5 1.75 3.125]
      (note reply (+ t at) (if suspended 0.35 0.12)
        (+ (phrase (+ 1 i)) (if busy -12 0)) (if suspended 67 84))))
  (when busy
    (for step 0 16
      (control bass (+ t (* step 0.25)) cutoff
        (norm (+ 520 (* 165 (% (+ step bar) 7))) 20 20000 0.2)))))

(daw/schedule pickup bpm (p/events 112 notes))
(defn fade [start duration from to]
  (daw/schedule start 60
    (p/map |[:param master :gain (music/lerp from to $)] (p/curve duration 80 identity))))
(fade 0 0.04 0 0.72)
(def finish (+ pickup (* beat 112)))
(fade (+ finish 0.3) 3 0.72 0)
(daw/end (+ finish 3.5) {:format :pcm16 :normalize 0.93})
