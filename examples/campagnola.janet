# Campagnola Stomp — solo honky-tonk piano, 124 BPM, 4/4, 68 bars + decay.
# Gigione's instrumental hook meets habanera, stride and a minor-key trio.
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

# Small reproducible variations of timing and touch; a mild long-short lilt.
# The final four bars broaden, with a fermata before the last two chords.
(defn clock-beat [t]
  (+ 0.08 t (if (> t 256) (* 0.018 (math/pow (- t 256) 2)) 0)))
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
   :Eb [39 46 [51 55 58 62]] :Adim [33 39 [51 54 57 60]]})
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
    :stride
      (do
        (bass t root 0.55 (+ intensity 12) true)
        (chord (+ t 1.015) keys 0.31 intensity)
        (bass (+ t 2) (if walking (+ root 12) fifth) 0.52 (+ intensity 6) false)
        (chord (+ t 3.014) keys 0.3 (- intensity 3))
        (when walking (note (+ t 3.56) 0.19 (+ fifth 1) (- intensity 8) 0))))
  nil)
(def progression [:C :G :D :G7 :C :G :D :G])
(defn accompany [start feel intensity]
  (each [i key] (pairs progression) (left (+ start i) key feel intensity (= (% i 2) 1))))

# Retain every onset/pitch of the eight-bar original hook. Ornamentation stays
# around its rests; later statements add lower chord tones or an octave.
(defn theme [start heat minor]
  (each [at duration original] source/theme
    (var key original)
    (when (and minor (or (= (% key 12) 4) (= (% key 12) 11))) (-- key))
    (def t (+ (* start 4) at))
    (def d (min duration (if (> duration 2) 1.65 (+ duration 0.04))))
    (note t d key (+ 87 heat) 1 (if (> heat 0) 0.12 0.04))
    (when (and (> heat 4) (= (% at 4) 0))
      (note t (* d 0.83) (- key 12) (+ 68 heat) 1 0.12))
    (when (and (> heat 9) (or (= (% at 4) 0) (= (% at 4) 1)))
      (note (- t 0.11) 0.075 (- key 1) 66 1)))
  # Responses in the spacious second beat of each of the first three bars.
  (when (> heat 0)
    (each [i keys] (pairs [[64 67] [62 67] [62 66]])
      (chord (+ (* (+ start i) 4) 2.05) keys 0.2 61 1))
    (run (+ (* start 4) 30.5) [67 69 70 71 74 75] 0.25 72)))
(defn answer [start heat]
  (each [at duration key] source/answer
    (note (+ (* start 4) at) (+ duration 0.02) key (+ 87 heat) 1 0.14)
    (when (> heat 5)
      (note (+ (* start 4) at) duration (- key 12) 71 1 0.14)))
  (each [bar keys] (pairs [[67 69 70 71] [66 67 69 71] [66 69 72 74] [71 74 76 77]
                              [67 69 70 71] [66 67 69 71] [72 71 69 66] [74 72 71 69]])
    (run (+ (* (+ start bar) 4) 3) keys 0.23 (+ 65 heat))))

# 1–4: Spanish-tinged pickup, fragments of the hook, dominant turnaround.
(each [i key] (pairs [:Gm :Cm :G :D]) (left i key :habanera 64 false))
(run 0.3 [67 70 74 79 78 77 74] 0.25 75)
(chord 2.5 [70 74 79] 0.42 77 1)
(run 4.55 [72 75 79 78 77 75] 0.27 75)
(note 8 0.45 76 88) (note 9 0.42 72 83)
(run 10.7 [71 72 73 74] 0.25 79)
(chord 12 [66 69 72 76] 0.4 82 1)
(run 14 [69 70 71 72 73 74 75 76] 0.23 78)

# 5–12: recognizable theme over habanera; 13–20: stride opens the room.
(accompany 4 :habanera 65) (theme 4 0 false)
(accompany 12 :stride 70) (theme 12 6 false)

# 21–28: the source's chromatic answering riff, decorated with little replies.
(accompany 20 :stride 69) (answer 20 2)

# 29–36: minor trio, quieter; the melodic shape survives the modal change.
(each [i key] (pairs [:Cm :Gm :D :Gm :Cm :Eb :Adim :D])
  (left (+ 28 i) key :habanera 59 false))
(theme 28 -9 true)
(run 142.2 [66 67 70 73 76 74] 0.27 71)

# 37–40: stop-time break. Short silences make the stride's return matter.
(each [i key] (pairs [:E7 :A7 :D :D]) (left (+ 36 i) key :stop 73 false))
(run 145 [76 74 71 68 67 64] 0.25 81)
(run 149 [73 76 79 78 76 73] 0.25 83)
(run 153 [74 72 69 66 65 62] 0.25 83)
(chord 156 [66 69 72 75] 0.32 90 1)
(run 158.5 [69 70 71 72 74 75] 0.23 87)

# 41–48: hot statement; 49–56: embellished chromatic answer.
(accompany 40 :stride 75) (theme 40 12 false)
(accompany 48 :stride 74) (answer 48 9)

# 57–64: last refrain, original melody kept above the bass and octave doubles.
(accompany 56 :stride 78) (theme 56 15 false)

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

# A piano key is released before the same key is struck again, even when the
# hands cross. This also avoids an old MIDI note-off cutting a newer note.
(sort performance (fn [a b] (< (a 0) (b 0))))
(def last-note @{})
(each event performance
  (when-let [previous (last-note (event 2))]
    (when (> (previous 1) (event 0))
      (put previous 1 (max (previous 0) (- (event 0) 0.006)))))
  (put last-note (event 2) event))
(each [start end key velocity _] performance
  (array/push items [start end [:note piano key velocity]]))

(def beats (+ (clock-beat 272) 6))
(daw/schedule 0 bpm (p/events beats items))
(daw/end (/ (* beats 60) bpm) {:format :pcm16 :normalize 0.94})

# Optional event export for an editable MIDI of the same performance.
(when-let [path (os/getenv "CAMPAGNOLA_EXPORT")]
  (spit path (string/join (map |(string/join (map string $) ",") performance) "\n")))
