# Musical time and pitch only. Callbacks connect sequences and curves to the host.

(defn- finite? [x] (and (number? x) (= (- x x) 0)))

(defn- positive? [x] (and (finite? x) (> x 0)))

(defn seconds
  "Convert quarter-note beats to seconds at a constant BPM. Negative offsets are allowed."
  [bpm beats]
  (assert (positive? bpm) "BPM must be finite and positive")
  (assert (finite? beats) "beats must be finite")
  (* beats (/ 60 bpm)))

(defn bars
  "Duration in seconds of count bars; meter defaults to 4/4, BPM counts quarter notes."
  [bpm count &opt numerator denominator]
  (default numerator 4)
  (default denominator 4)
  (each x [numerator denominator]
    (assert (and (positive? x) (= x (math/floor x))) "meter must contain positive integers"))
  (seconds bpm (* count numerator (/ 4 denominator))))

(defn degree
  "Map a zero-based scale degree to a pitch. Intervals repeat every octave, also below zero."
  [root intervals n]
  (assert (and (finite? n) (= n (math/floor n)) (> (length intervals) 0))
    "expected an integer degree and a nonempty scale")
  (def octave (math/floor (/ n (length intervals))))
  (+ root (intervals (- n (* octave (length intervals)))) (* 12 octave)))

(defn chord
  "Return pitches by adding each semitone interval to root; voicing and order are preserved."
  [root intervals]
  (map |(+ root $) intervals))

(defn sequence
  "Call emit(time, value) at fixed steps in seconds. nil is a rest. Return the end of the sequence."
  [start step values emit]
  (assert (and (finite? start) (positive? step)) "expected a finite start and a positive step")
  (eachp [i value] values
    (unless (nil? value) (emit (+ start (* i step)) value)))
  (+ start (* (length values) step)))

(defn lerp
  "Interpolate from a to b at x; x is not clamped."
  [a b x]
  (+ a (* (- b a) x)))

(defn curve
  "Call emit(time, shape(x)) for x=0..1, including both ends. steps is the number of intervals."
  [start duration steps shape emit]
  (assert (and (finite? start) (positive? duration)) "expected a finite start and a positive duration")
  (assert (and (positive? steps) (= steps (math/floor steps)) (<= steps 0x7fffffff))
    "steps must be a positive 32-bit integer")
  (for i 0 (+ steps 1)
    (def x (/ i steps))
    (emit (+ start (* duration x)) (shape x)))
  (+ start duration))
