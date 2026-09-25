# Campagnola Stomp — v2, solo honky-tonk piano, 4/4, 68 bars + decay.
# 124 BPM, relaxing in the trio and pushing forward in the last refrain.
# Setup and form: campagnola.md. No drum machine; both hands share one piano.
(import ../lib/pattern :as p)
(import ./sources/campagnola-theme :as source)

(def bpm 124)
(def piano (daw/plugin :saloon "plugins/piano/build/plugin.perone"
  {:gain 0.6 :detune 12 :blend 0.34}))
(def lane (daw/track piano {:name "Campagnola / honky-tonk piano" :gain 0.8}))
(def master (daw/master lane {:gain 0.9}))
(daw/output master)
(def items @[])
(def performance @[])

# Integrate local tempo changes so note ends and starts share the same clock.
# The trio breathes at ~115 BPM; the last refrain moves at ~127 BPM.
(defn clock-beat [t]
  (+ 0.08 t
    (* 0.08 (max 0 (min 32 (- t 112))))
    (* 0.03 (max 0 (min 16 (- t 176))))
    (* -0.025 (max 0 (min 32 (- t 224))))
    (if (> t 256) (* 0.018 (math/pow (- t 256) 2)) 0)))
(defn lilt [t amount]
  (def whole (math/floor t))
  (def f (- t whole))
  (+ whole (if (<= f 0.5) (* f (+ 1 amount))
    (+ (* 0.5 (+ 1 amount)) (* (- f 0.5) (- 1 amount))))))
(defn note [at duration pitch velocity &opt hand swing]
  (default hand 1)
  (default swing 0)
  (def jitter (* 0.004 (- (% (+ (* pitch 7) (math/floor (* at 8))) 7) 3)))
  (def t (clock-beat (+ (lilt at swing) jitter (if (= hand 1) 0.012 0))))
  (def end (clock-beat (+ (lilt at swing) jitter duration)))
  (def vel (max 1 (min 127 (+ velocity (- (% (+ pitch (math/floor (* at 4))) 5) 2)))))
  (array/push performance @[t (max (+ t 0.04) end) pitch vel hand]))
(defn chord [t keys duration velocity &opt hand]
  (default hand 0)
  (each [i key] (pairs keys) (note (+ t (* i 0.008)) duration key (- velocity (* i 2)) hand)))
(defn run [t keys step velocity &opt hand]
  (default hand 1)
  (each [i key] (pairs keys) (note (+ t (* i step)) (* step 0.82) key (+ velocity (% i 3)) hand 0.08)))

# Bass / fifth / close-position middle-register voicing. Avoid muddy low chords.
(def harmony
  {:C [36 43 [52 55 57 60]] :G [31 38 [50 55 59 64]]
   :D [38 45 [54 57 60 62]] :G7 [31 38 [53 55 59 62]]
   :E7 [40 35 [50 56 59 62]] :A7 [33 40 [49 55 57 64]]
   :Cm [36 43 [51 55 58 60]] :Gm [31 38 [50 55 58 62]]
   :Eb [39 46 [51 55 58 62]] :Adim [33 39 [51 54 57 60]]
   :GB [35 38 [50 55 59 64]] :DFs [42 45 [54 57 60 62]]
   :Bb [34 41 [50 53 58 62]] :BbF [41 34 [50 53 58 62]]
   :F7 [41 36 [51 57 60 65]] :Edim [40 46 [55 58 61 64]]})
(defn bass [t pitch duration velocity heavy]
  (note t duration pitch velocity 0)
  (when heavy (note (+ t 0.008) (* duration 0.83) (+ pitch 12) (- velocity 13) 0)))
(defn left [bar name feel intensity &opt walking]
  (def t (* bar 4))
  (def [root fifth keys] (harmony name))
  (case feel
    :habanera
      (do
        (bass t root 0.83 (+ intensity 10) true)
        (chord (+ t 1.5) keys 0.3 (- intensity 3))
        (bass (+ t 2) fifth 0.56 (+ intensity 3) false)
        (chord (+ t 3) keys 0.46 intensity))
    :stop
      (do (bass t root 0.36 (+ intensity 9) true)
          (chord t keys 0.32 intensity))
    :charleston
      (do
        (bass t root 0.66 (+ intensity 8) false)
        (chord (+ t 1.5) keys 0.36 intensity)
        (note (+ t 3.5) 0.25 fifth (- intensity 5) 0))
    :open
      (do
        (bass t root 1.6 (+ intensity 5) false)
        (each [i key] (pairs keys)
          (note (+ t 1.75 (* i 0.07)) (- 1.35 (* i 0.07)) key (- intensity (* i 2)) 0)))
    :broken
      (do
        (bass t root 0.7 (+ intensity 6) false)
        (note (+ t 0.75) 0.32 (keys 0) (- intensity 6) 0)
        (note (+ t 1.5) 0.35 (keys 2) intensity 0)
        (bass (+ t 2.5) fifth 0.45 intensity false)
        (chord (+ t 3.25) [(keys 1) (keys 3)] 0.3 (- intensity 6)))
    :stride
      (do
        (bass t root 0.55 (+ intensity 12) true)
        (chord (+ t 1.015) keys 0.31 intensity)
        (bass (+ t 2) (if walking (+ root 12) fifth) 0.52 (+ intensity 6) false)
        (chord (+ t 3.014) keys 0.3 (- intensity 3))
        (when walking (note (+ t 3.56) 0.19 (+ fifth 1) (- intensity 8) 0))))
  nil)
(defn walking [bar pitches name intensity]
  (each [i key] (pairs pitches) (note (+ (* bar 4) i) 0.59 key (+ intensity (if (= i 0) 5 -3)) 0))
  (def keys ((harmony name) 2))
  (chord (+ (* bar 4) 1.5) [(keys 1) (keys 2)] 0.22 (- intensity 13))
  (chord (+ (* bar 4) 3.5) [(keys 0) (keys 3)] 0.19 (- intensity 17)))
(defn accompany [start progression feels intensity]
  (each [i key] (pairs progression)
    (left (+ start i) key (feels (% i (length feels)))
      (+ intensity ([0 -3 2 -4] (% i 4))) false)))

# Quote a selected phrase. The first full statement is unchanged; subsequent
# phrases alter articulation, anticipation, register or the hand carrying it.
(defn quote-theme [start from until transpose intensity style hand]
  (each [at duration original] source/theme
    (when (and (>= at from) (< at until))
      (def key (+ original transpose))
      (def anticipate (if (and (= style :rag) (> at from) (= (% at 4) 0)) -0.18 0))
      (def t (+ (* start 4) (- at from) anticipate))
      (def d (* (if (= style :rag) 0.8 1)
        (min duration (if (> duration 2) 1.65 (+ duration 0.04)))))
      (def swing (if (= style :plain) 0.04 0.12))
      (note t d key intensity hand swing)
      (when (and (= style :octaves) (or (= (% at 4) 0) (= (% at 4) 1)))
        (note t (* d 0.8) (- key 12) (- intensity 17) hand swing))
      (when (and (= style :rag) (= (% at 4) 1))
        (note (- t 0.12) 0.075 (- key 1) (- intensity 26) hand)))))
(defn answer [start]
  (each [at duration key] source/answer
    (def octave (if (and (>= at 16) (< at 24)) 12 0))
    (note (+ (* start 4) at) (+ duration 0.02) (+ key octave)
      (if (>= at 24) 83 92) 1 0.14))
  # Unequal replies, with genuine empty space between calls.
  (run (+ (* start 4) 7.1) [62 65 66 67] 0.2 67)
  (chord (+ (* start 4) 15.3) [65 67 71] 0.18 72 1)
  (run (+ (* start 4) 23) [86 83 79 78 77] 0.18 74)
  (chord (+ (* start 4) 27.45) [66 69 72] 0.18 65 1))

# New cantabile: short cells derived from the hook's falling thirds, with
# long answers and rests. These are not another eight bars of the same quote.
(def trio
  [[[0 0.8 75] [1 1.65 72] [3.25 0.42 74]]
   [[0 1.2 74] [1.5 1.55 70] [3.5 0.3 69]]
   [[0.5 1.25 79] [2 0.65 77] [3 0.75 75]]
   [[0 1.6 78] [2.25 0.4 76] [3 0.8 74]]
   [[0 0.65 79] [1 1.4 75] [2.75 0.35 74] [3.5 0.35 72]]
   [[0 0.6 74] [0.75 0.55 77] [1.5 1.1 81] [3 0.7 79]]
   [[0.25 1.2 79] [1.75 0.5 77] [2.5 1.0 75]]
   [[0 0.6 74] [1 0.5 72] [2 0.5 69] [3 0.42 66]]])

# 1–4: Spanish-tinged pickup, fragments of the hook, dominant turnaround.
(each [i key] (pairs [:Gm :Cm :G :D]) (left i key :habanera 64 false))
(run 0.3 [67 70 74 79 78 77 74] 0.25 75)
(chord 2.5 [70 74 79] 0.42 77 1)
(run 4.55 [72 75 79 78 77 75] 0.27 75)
(note 8 0.45 76 88) (note 9 0.42 72 83)
(run 10.7 [71 72 73 74] 0.25 79)
(chord 12 [66 69 72 76] 0.4 82 1)
(run 14 [69 70 71 72 73 74 75 76] 0.23 78)

# 5–12: the complete familiar theme, exposed over habanera.
(accompany 4 [:C :G :D :G7 :C :G :D :G] [:habanera] 65)
(quote-theme 4 0 32 0 87 :plain 1)

# 13–20: an anticipated ragtime statement, then a much lighter Charleston.
# Inversions move the bass line; the last bar leaves room for the next entry.
(accompany 12 [:C :GB :DFs :G7] [:stride :broken :stride :charleston] 70)
(accompany 16 [:C :GB :D :G] [:charleston :broken :stride :stop] 66)
(quote-theme 12 0 16 0 93 :rag 1)
(quote-theme 16 16 32 0 89 :plain 1)
(chord 50.1 [64 67] 0.2 60 1)
(run 62.3 [67 69 70 71 74] 0.24 70)
(run 78 [62 64 65 66 67 69 70] 0.23 69)

# 21–28: the source's answering riff jumps register, with sparse responses.
(left 20 :C :charleston 69 false)
(left 21 :G :charleston 67 false)
(walking 22 [38 42 45 44] :D 76)
(walking 23 [43 42 41 37] :G7 73)
(left 24 :C :stride 72 false)
(left 25 :GB :broken 67 false)
(left 26 :D :stop 72 false)
(left 27 :G7 :stop 64 false)
(answer 20)

# 29–36: quieter new melody in the minor/relative-major area, longer lines,
# rolled accompaniment and a gentler tempo; no literal repetition of A.
(accompany 28 [:Cm :Gm :Eb :D :Cm :Bb :Eb :D] [:open :broken] 53)
(each [bar phrase] (pairs trio)
  (each [at duration key] phrase
    (note (+ (* (+ 28 bar) 4) at) duration key (+ 76 ([0 -3 2 0] (% bar 4))) 1)))
(note 119.25 0.35 62 53 0)
(note 127.2 0.38 57 55 0)
(run 142.9 [66 67 69 70] 0.24 69)

# 37–44: a new eight-bar strain around B-flat, using a chromatic diminished
# passing chord and walking bass before a chain of dominants returns to G.
(left 36 :Eb :stride 66 false)
(left 37 :Edim :charleston 67 false)
(walking 38 [41 45 46 47] :BbF 75)
(walking 39 [48 45 43 41] :F7 74)
(left 40 :Bb :stride 72 false)
(left 41 :G7 :broken 70 false)
(left 42 :A7 :charleston 72 false)
(left 43 :D :stop 76 false)
(def bridge
  [[[0 79] [0.75 82] [1.5 84] [2.5 82] [3 79] [3.5 77]]
   [[0.5 79] [1 82] [1.5 85] [2.5 82] [3.25 79]]
   [[0 77] [0.5 82] [1.5 86] [2.5 84] [3 82] [3.5 81]]
   [[0 81] [1.5 79] [2 77] [2.75 75] [3.5 72]]
   [[0 74] [0.5 77] [1.5 82] [2.25 81] [2.75 79] [3.5 77]]
   [[0 71] [0.75 74] [1.5 77] [2.5 76] [3 74] [3.5 72]]
   [[0 73] [1 76] [1.5 79] [2.5 78] [3 76] [3.5 74]]
   [[0 78] [0.75 76] [1.5 74] [2 72] [2.5 69] [3 66]]])
(each [bar phrase] (pairs bridge)
  (each [at key] phrase
    (note (+ (* (+ 36 bar) 4) at) (if (= at 1.5) 0.6 0.31) key
      (+ 85 (if (= (% at 1) 0) 4 -2)) 1 0.13)))

# 45–48: unequal stop-time calls. A low response interrupts the treble run;
# the final pause is longer than the preceding ones.
(each [i key] (pairs [:E7 :A7 :D :D]) (left (+ 44 i) key :stop 71 false))
(run 176.75 [76 74 71 68 67 64] 0.25 82)
(run 181 [49 52 55 58 57] 0.3 88 0)
(chord 183.25 [67 73 76] 0.21 67 1)
(run 184.75 [74 72 69 66] 0.32 86)
(chord 188 [66 69 72 75] 0.24 87 1)
(run 191.2 [59 60 62] 0.24 81 0)

# 49–56: the theme passes between registers and hands, two bars at a time.
# Soft right-hand chords make room for the melody in the left hand.
(quote-theme 48 0 8 -12 99 :plain 0)
(each [bar keys] (pairs [[67 69 72] [67 71 74]])
  (chord (+ 193.5 (* bar 4)) keys 0.35 47 1)
  (chord (+ 195.5 (* bar 4)) keys 0.22 44 1))
(accompany 50 [:D :G7] [:broken :charleston] 65)
(quote-theme 50 8 16 0 94 :plain 1)
(accompany 52 [:C :G] [:charleston :broken] 62)
(quote-theme 52 16 24 12 87 :plain 1)
(accompany 54 [:D :G] [:stride :stop] 73)
(quote-theme 54 24 32 0 98 :octaves 1)
(run 222.25 [62 65 66 67 69 70 71] 0.22 78)

# 57–64: tutti piano, octave melody, a small tempo lift and walking bass.
# A rising register leads into a descending flourish before the final tag.
(accompany 56 [:C :GB :D :G7] [:stride :broken :stride :stride] 76)
(walking 60 [36 40 43 42] :C 86)
(walking 61 [43 42 41 40] :G 84)
(left 62 :D :stride 78 false)
(left 63 :G :stop 81 false)
(quote-theme 56 0 16 0 101 :octaves 1)
(quote-theme 60 16 32 12 99 :octaves 1)
(run 238.4 [67 69 70 71 74] 0.25 75)
(run 254.1 [86 83 79 78 77 74] 0.25 84)

# 65–68: recognizable three-part tag, slowing into a dry two-chord finish.
(left 64 :C :habanera 73 false)
(left 65 :G :habanera 71 false)
(left 66 :D :stop 76 false)
(note 256 0.5 76 103) (note 257 0.5 72 96)
(chord 258.7 [64 67 72 76] 0.3 88 1)
(note 260 0.48 74 99) (note 261 0.5 71 92)
(run 262 [74 73 72 71] 0.32 83)
(note 264 0.5 72 94) (note 265 0.5 69 88)
(chord 266 [66 69 72 74] 0.3 92 1)
(bass 268 31 0.46 102 true)
(chord 268 [55 59 62 67 71] 0.44 92 1)
(bass 270 31 1.8 110 true)
(chord 270 [55 59 62] 1.7 86)
(chord 270 [67 71 74 79] 1.65 106 1)

# Coalesce unisons at the same attack, then release each key before its next
# strike. Crossing hands must not create zero-length or overlapping MIDI notes.
(sort performance (fn [a b] (< (a 0) (b 0))))
(def last-note @{})
(def voiced @[])
(each event performance
  (def previous (last-note (event 2)))
  (if (and previous (< (- (event 0) (previous 0)) 0.025))
    (do
      (put previous 1 (max (previous 1) (event 1)))
      (when (> (event 3) (previous 3))
        (put previous 3 (event 3))
        (put previous 4 (event 4))))
    (do
      (when (and previous (> (previous 1) (event 0)))
        (put previous 1 (- (event 0) 0.006)))
      (array/push voiced event)
      (put last-note (event 2) event))))
(each [start end key velocity _] voiced
  (array/push items [start end [:note piano key velocity]]))

(def beats (+ (clock-beat 272) 6))
(daw/schedule 0 bpm (p/events beats items))
(daw/end (/ (* beats 60) bpm) {:format :pcm16 :normalize 0.94})

# Optional event export for an editable MIDI of the same performance.
(when-let [path (os/getenv "CAMPAGNOLA_EXPORT")]
  (spit path (string/join (map |(string/join (map string $) ",") voiced) "\n")))
