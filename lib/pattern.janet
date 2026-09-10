# Finite musical data. All times use quarter-note beats; values belong to the caller.

(defn- finite? [x] (and (number? x) (= (- x x) 0)))

(defn events
  "Build an immutable pattern from length and [start end value] events, preserving insertion order."
  [duration items]
  (assert (and (finite? duration) (>= duration 0)) "length must be finite and nonnegative")
  (assert (indexed? items) "expected an event sequence")
  (each item items
    (assert (and (indexed? item) (= (length item) 3)) "expected [start end value]")
    (def [a b _] item)
    (assert (and (finite? a) (finite? b) (<= a b)) "expected finite start <= end"))
  (freeze {:length duration :events items}))

(defn- checked [pattern]
  (assert (and (dictionary? pattern) (= (length pattern) 2)) "expected a pattern")
  (events (pattern :length) (pattern :events)))

(defn steps
  "Build equal intervals of step beats. nil is a rest, including at the end."
  [step values]
  (assert (and (finite? step) (> step 0) (indexed? values)) "expected a positive step and a sequence")
  (events (* step (length values))
    (seq [[i value] :pairs values :when (not (nil? value))]
      [(* i step) (* (+ i 1) step) value])))

(defn curve
  "Sample shape(x), x=0..1, at steps+1 points including both endpoints."
  [duration steps shape]
  (assert (and (finite? duration) (> duration 0)) "curve length must be finite and positive")
  (assert (and (finite? steps) (= steps (math/floor steps)) (<= 1 steps 0x7fffffff))
    "steps must be a positive 32-bit integer")
  (events duration
    (seq [i :range [0 (+ steps 1)]]
      (def x (/ i steps))
      (def t (* duration x))
      [t t (shape x)])))

(defn serial
  "Concatenate patterns using their declared lengths, retaining pickups and overhangs."
  [patterns]
  (assert (indexed? patterns) "expected a pattern sequence")
  (var offset 0)
  (def items @[])
  (each p patterns
    (def part (checked p))
    (each [a b value] (part :events)
      (array/push items [(+ offset a) (+ offset b) value]))
    (+= offset (part :length)))
  (events offset items))

(defn parallel
  "Overlay patterns at zero, in list order. The longest declared length wins."
  [patterns]
  (assert (indexed? patterns) "expected a pattern sequence")
  (var duration 0)
  (def items @[])
  (each p patterns
    (def part (checked p))
    (set duration (max duration (part :length)))
    (each item (part :events) (array/push items item)))
  (events duration items))

(defn map
  "Transform each value, leaving event times, length and insertion order intact."
  [f pattern]
  (def p (checked pattern))
  (events (p :length) (seq [[a b value] :in (p :events)] [a b (f value)])))

(defn stretch
  "Multiply all event times and the declared length by a positive factor."
  [factor pattern]
  (assert (and (finite? factor) (> factor 0)) "stretch factor must be finite and positive")
  (def p (checked pattern))
  (events (* factor (p :length))
    (seq [[a b value] :in (p :events)] [(* factor a) (* factor b) value])))

(defn reverse
  "Reflect [a b] to [length-b length-a], preserving insertion order and values."
  [pattern]
  (def p (checked pattern))
  (def duration (p :length))
  (events duration
    (seq [[a b value] :in (p :events)] [(- duration b) (- duration a) value])))
