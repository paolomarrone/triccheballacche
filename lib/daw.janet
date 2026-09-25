# Public score API: metadata stays here; native calls receive numeric configuration.

(def daw/nodes @{})
(def daw/products @{})
(def daw/mixer-parameters
  [{:index 0 :name :gain :label "Gain" :unit "linear"
    :min 0 :max 4 :default 1 :integer false :direction :input :map :linear}
   {:index 1 :name :pan :label "Pan" :unit ""
    :min -1 :max 1 :default 0 :integer false :direction :input :map :linear}])

(defn daw/info
  "Return immutable parameter descriptions for a plugin or mixer."
  [node]
  (or (daw/nodes node) (error "invalid node handle")))

(defn daw/product
  "Return the full product metadata; nil for mixers."
  [node]
  (daw/info node)
  (daw/products node))

(defn daw/parameter [parameters key]
  (def p
    (if (and (number? key) (= key (math/floor key)) (<= 0 key) (< key (length parameters)))
      (parameters key)
      (find |(= ($ :name) key) parameters)))
  (or p (error (string "unknown parameter " key))))

(defn daw/plugin
  "Instantiate a Perone bundle. An optional leading keyword gives the node a stable live identity."
  [id-or-path & args]
  (def named (keyword? id-or-path))
  (assert (<= (length args) (if named 2 1)) "too many plugin arguments")
  (def path (if named (args 0) id-or-path))
  (def params (or (get args (if named 1 0)) {}))
  (assert (dictionary? params) "expected initial parameter dictionary")
  (def plugin (perone/read path))
  (def defaults (array ;(plugin :defaults)))
  (eachp [key value] params
    (def p (daw/parameter (plugin :parameters) key))
    (put defaults (p :index) (perone/value p value)))
  (def id (native/plugin (plugin :binary) (plugin :layout) defaults))
  (when named (native/key id (string id-or-path)))
  (put daw/nodes id (plugin :parameters))
  (put daw/products id (plugin :product))
  id)

(defn daw/track [source &opt options]
  (def id (native/track source options))
  (put daw/nodes id daw/mixer-parameters)
  id)

(defn daw/master [signal &opt options]
  (def id (native/master signal options))
  (put daw/nodes id daw/mixer-parameters)
  id)

(def daw/through native/through)
(def daw/output native/output)

(defn daw/mix
  "Sum signals into a stereo mixer. Reusing a signal shares its audio, including DSP state."
  [signals &opt options]
  (def id (native/mix signals options))
  (put daw/nodes id daw/mixer-parameters)
  id)

(defn daw/param [node time parameter value]
  (def p (daw/parameter (daw/info node) parameter))
  (native/param node time (p :index) (perone/value p value)))

(def daw/note native/note)
(def daw/end native/end)

(defn daw/schedule
  "Emit a beat pattern at start seconds and bpm. Values are [:note node pitch velocity] or [:param node key value]."
  [start bpm pattern]
  (assert (and (number? start) (<= 0 start 3600)) "start must be between 0 and 3600 seconds")
  (assert (and (number? bpm) (= (- bpm bpm) 0) (> bpm 0)) "BPM must be finite and positive")
  (assert (and (dictionary? pattern) (= (length pattern) 2)
               (number? (pattern :length)) (>= (pattern :length) 0)
               (indexed? (pattern :events))) "expected a pattern")
  (def unit (/ 60 bpm))
  (def end (+ start (* unit (pattern :length))))
  (assert (<= start end 3600) "pattern end must be within 3600 seconds")
  (each item (pattern :events)
    (assert (and (indexed? item) (= (length item) 3)) "expected [start end value]")
    (def [a b command] item)
    (assert (and (number? a) (number? b) (<= a b)
                 (indexed? command) (= (length command) 4)) "invalid scheduled event")
    (def time (+ start (* unit a)))
    (def until (+ start (* unit b)))
    (assert (<= 0 time until 3600) "event must be within 0..3600 seconds")
    (def [kind node key value] command)
    (case kind
      :note (do
        (assert (< a b) "note must have positive duration")
        (daw/note node time (- until time) key value))
      :param (do
        (assert (= a b) "parameter must be a point event")
        (daw/param node time key value))
      (error "expected :note or :param command")))
  end)

(var- daw/bpm 120)
(defn daw/tempo
  "Set the score tempo in quarter notes per minute. Live revisions keep the current tempo."
  [bpm]
  (assert (and (perone/finite? bpm) (> bpm 0)) "BPM must be finite and positive")
  (set daw/bpm bpm))

(defn daw/score
  "Prepare finite or repeating patterns. :duration bounds export in seconds; :quantum sets the update grid in beats."
  [pattern &opt options]
  (default options {})
  (assert (dictionary? options) "expected score options")
  (each key (keys options)
    (assert (find |(= key $) [:duration :quantum]) "unknown score option"))
  (assert (and (dictionary? pattern)
               (or (indexed? (pattern :streams)) (indexed? (pattern :events)))) "expected a pattern")
  (def sources (or (pattern :streams) [{:offset 0 :period nil :events (pattern :events)}]))
  (def unit (/ 60 daw/bpm))
  (native/sequence daw/bpm (or (options :quantum) 4))
  (eachp [si source] sources
    (def period (* unit (or (source :period) 0)))
    (each [a b command] (source :events)
      (assert (and (indexed? command) (= (length command) 4)) "expected a note or parameter command")
      (def [kind node key value] command)
      (def start (* unit (+ (source :offset) a)))
      (def end (* unit (+ (source :offset) b)))
      (case kind
        :note (native/cue node start end period -1 key value si)
        :param (do
          (assert (= a b) "parameter must be a point event")
          (def p (daw/parameter (daw/info node) key))
          (native/cue node start end period (p :index) (perone/value p value) 0 si))
        (error "expected :note or :param"))))
  (native/seal (or (options :duration) (when (pattern :length) (* unit (pattern :length))))))
