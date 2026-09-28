# Immutable musical data. Times use quarter-note beats; nil length means unbounded.

(defn- finite? [x] (and (number? x) (= (- x x) 0)))

(defn streams
  "Validate a pattern and return its sources without expanding repetitions."
  [pattern]
  (assert (and (dictionary? pattern) (<= 1 (length pattern) 2)
               (or (nil? (pattern :length))
                   (and (finite? (pattern :length)) (>= (pattern :length) 0)))
               (indexed? (pattern :streams))) "expected a pattern")
  (each key (keys pattern)
    (assert (or (= key :length) (= key :streams)) "unknown pattern field"))
  (each source (pattern :streams)
    (assert (and (dictionary? source) (<= 2 (length source) 3) (finite? (source :offset))
                 (or (nil? (source :period))
                     (and (nil? (pattern :length)) (finite? (source :period)) (> (source :period) 0)))
                 (indexed? (source :events))) "invalid event source")
    (each key (keys source)
      (assert (or (= key :offset) (= key :period) (= key :events)) "unknown source field"))
    (each item (source :events)
      (assert (and (indexed? item) (= (length item) 3)) "expected [start end value]")
      (def [a b _] item)
      (assert (and (finite? a) (finite? b) (<= a b)
                   (finite? (+ (source :offset) a)) (finite? (+ (source :offset) b)))
        "expected finite start <= end")))
  (pattern :streams))

(defn- composed [duration sources]
  (def pattern (freeze {:length duration :streams sources}))
  (streams pattern)
  pattern)

(defn events
  "Build an immutable pattern from length and [start end value] events, preserving insertion order."
  [duration items]
  (assert (and (finite? duration) (>= duration 0)) "length must be finite and nonnegative")
  (assert (indexed? items) "expected an event sequence")
  (composed duration (if (empty? items) [] [{:offset 0 :period nil :events items}])))

(defn flatten
  "Return a finite phrase's [start end value] events at absolute beat offsets, in source order."
  [pattern]
  (def sources (streams pattern))
  (assert (pattern :length) "expected a finite phrase")
  (freeze
    (seq [source :in sources [a b value] :in (source :events)]
      [(+ (source :offset) a) (+ (source :offset) b) value])))

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
  "Concatenate declared lengths, retaining pickups and overhangs. Only the last pattern may be unbounded."
  [patterns]
  (assert (indexed? patterns) "expected a pattern sequence")
  (var offset 0)
  (def sources @[])
  (each p patterns
    (assert offset "an unbounded pattern must be last in serial")
    (each source (streams p)
      (array/push sources (merge source {:offset (+ offset (source :offset))})))
    (set offset (when (p :length) (+ offset (p :length)))))
  (composed offset sources))

(defn parallel
  "Overlay patterns at zero, in list order. Each loop keeps its own period."
  [patterns]
  (assert (indexed? patterns) "expected a pattern sequence")
  (var duration 0)
  (def sources @[])
  (each p patterns
    (each source (streams p) (array/push sources source))
    (set duration (when (and duration (p :length)) (max duration (p :length)))))
  (composed duration sources))

(defn map
  "Transform template values once, preserving timing and insertion order."
  [f pattern]
  (composed (pattern :length)
    (seq [source :in (streams pattern)]
      (merge source {:events
        (seq [[a b value] :in (source :events)] [a b (f value)])}))))

(defn stretch
  "Multiply event times, offsets, periods and length by a positive factor."
  [factor pattern]
  (assert (and (finite? factor) (> factor 0)) "stretch factor must be finite and positive")
  (composed (when (pattern :length) (* factor (pattern :length)))
    (seq [source :in (streams pattern)]
      {:offset (* factor (source :offset))
       :period (when (source :period) (* factor (source :period)))
       :events (seq [[a b value] :in (source :events)] [(* factor a) (* factor b) value])})))

(defn reverse
  "Reflect a finite phrase around its declared length. Reverse before looping."
  [pattern]
  (def sources (streams pattern))
  (def duration (pattern :length))
  (assert duration "expected a finite phrase")
  (composed duration
    (seq [source :in sources]
      {:offset (- (source :offset))
       :period nil
       :events (seq [[a b value] :in (source :events)] [(- duration b) (- duration a) value])})))

(defn query
  "Return whole events overlapping [from,to), with stable [source,event,cycle] identities.
  Points belong to the half-open interval. A loop starts at cycle zero. No generator state is consumed."
  [pattern from to &opt limit]
  (default limit 65536)
  (assert (and (finite? from) (finite? to) (<= from to)
               (number? limit) (= limit (math/floor limit)) (>= limit 0)) "invalid query bounds")
  (def result @[])
  (when (< from to)
    (eachp [si source] (streams pattern)
      (def offset (source :offset))
      (def period (source :period))
      (eachp [ei [a b value]] (source :events)
        (def start (+ offset a))
        (def end (+ offset b))
        (def point (= a b))
        (def first
          (if period
            (max 0 (- (math/floor (/ (- from (if point start end)) period)) 1)) 0))
        # Include neighboring candidates, then test their actual endpoints. Division
        # rounding at a boundary must not make window partitioning lose an event.
        (def last (if period (+ 1 (math/ceil (/ (- to start) period))) 1))
        (assert (<= (max first last) 4503599627370496) "query exceeds exact cycle range")
        (var cycle first)
        (while (< cycle last)
          (def shift (if period (* cycle period) 0))
          (def begin (+ start shift))
          (def finish (+ end shift))
          (when (and (< begin to) (if point (>= begin from) (> finish from)))
            (assert (< (length result) limit) "pattern query event limit exceeded")
            (array/push result {:id [si ei cycle] :start begin :end finish :value value}))
          (++ cycle)))))
  (freeze result))

(defn loop :shadow
  "Repeat a finite positive-length phrase forever, preserving pickups and overhangs."
  [pattern]
  (def items (flatten pattern))
  (assert (> (pattern :length) 0) "loop requires a finite positive-length pattern")
  # A phrase repeats as a whole: its endpoint precedes the next cycle's start,
  # even when the phrase was assembled from several sources.
  (composed nil [{:offset 0 :period (pattern :length) :events items}]))
