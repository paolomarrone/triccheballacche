# Optional source tracing. Install once, before compiling the score and its imports.
# Bindings and annotations live only until the host closes Janet after preparation.
# Keep annotations outside musical values. Janet may coalesce equal constants, so equal
# patterns collect possible origins instead of claiming a source identity that cannot survive compilation.
(def self (dyn :current-file))
(def module (require "./pattern"))

(defn install [env entry]
  (assert (nil? (root-env :trace/push)) "source tracing is already installed")
  (def originals @{})
  (each name '[events steps curve serial parallel map stretch reverse]
    (put originals name (get-in module [name :value])))
  (def pattern-source ((disasm (originals 'events)) :source))
  (def annotations @{})
  (def locations @[])
  (def location-ids @{})
  (def emitted @[])
  (def appended @{})
  (def started (os/clock))
  (var captures 0)
  (var fallback-events 0)
  (var pushed-events 0)

  (defn origin [&opt fiber]
    (def stack (debug/stack (or fiber (fiber/current))))
    (++ captures)
    (def frames
      (freeze
        (seq [frame :in stack
              :let [source (frame :source)]
              :when (and (string? source) (frame :source-line)
                         (not (frame :c))
                         (not (find |(= source $) [self pattern-source entry "core.janet" "lib/daw.janet"])))]
          {:file source :line (frame :source-line) :column (frame :source-column)
           :tail (not (not (frame :tail)))})))
    (or (location-ids frames)
      (do
        (def id (length locations))
        (array/push locations frames)
        (put location-ids frames id)
        id)))

  # Observe event-shaped values while their producers are still on the stack.
  # Arrays have reference identity, so separate equal lists keep separate slot annotations.
  (defn pushed [items start fiber]
    (var source nil)
    (for i start (length items)
      (def item (items i))
      (when (and (indexed? item) (= (length item) 3) (number? (item 0)) (number? (item 1)))
        (default source (origin fiber))
        (unless (appended items) (put appended items @{}))
        (put (appended items) i [item source]))))

  (def push-binding (env 'trace/array-push))
  (put root-env :trace/push pushed)
  (put root-env 'array/push push-binding)
  (put env 'array/push push-binding)

  (defn provenance [pattern fallback]
    (if-let [stored (annotations pattern)]
      stored
      (do
        (+= fallback-events (length (pattern :events)))
        ((originals 'map) (fn [_] [fallback]) pattern))))

  (eachp [name f] originals
    (def wrapped
      (fn [& args]
        # Capture before invoking f; a user helper may already have disappeared in a tail call.
        (def source (origin))
        (def result (f ;args))
        (def trace
          (case name
            'events ((originals 'events) (result :length)
                      (seq [[i [a b value]] :pairs (result :events)]
                        (def slot (get-in appended [(args 1) i]))
                        (def saved (and slot (= (slot 0) ((args 1) i))))
                        (when saved (++ pushed-events))
                        [a b [(if saved (slot 1) source)]]))
            'serial (f (map |(provenance $ source) (args 0)))
            'parallel (f (map |(provenance $ source) (args 0)))
            'map (provenance (args 1) source)
            'stretch (f (args 0) (provenance (args 1) source))
            'reverse (f (provenance (args 0) source))
            ((originals 'map) (fn [_] [source]) result)))
        (def previous (annotations result))
        (put annotations result
          (if previous
            ((originals 'events) (trace :length)
              (seq [[i [a b sources]] :pairs (trace :events)]
                [a b (distinct (tuple ;(((previous :events) i) 2) ;sources))]))
            trace))
        result))
    (put module name (table/setproto @{:value wrapped} (module name))))

  (defn replace [name wrap]
    (def binding (env name))
    (put env name (table/setproto @{:value (wrap (binding :value))} binding)))

  (replace 'daw/schedule
    (fn [schedule]
      (fn [start bpm pattern]
        (def source (origin))
        (def trace (provenance pattern source))
        (def orders @{})
        (each [_ _ command] (pattern :events)
          (def node (command 1))
          (unless (orders node) (put orders node (native/event-count node))))
        (def result (schedule start bpm pattern))
        (eachp [i [a b command]] (pattern :events)
          (array/push emitted
            [(+ start (* a (/ 60 bpm))) (+ start (* b (/ 60 bpm)))
             (((trace :events) i) 2) (command 0) (command 1) (orders (command 1))])
          (update orders (command 1) + (if (= (command 0) :note) 2 1)))
        result)))

  # schedule was compiled before these replacements, so scheduled notes are not recorded twice.
  (replace 'daw/note
    (fn [note]
      (fn [node time duration pitch &opt velocity]
        (def source (origin))
        (def order (native/event-count node))
        (def result (if (nil? velocity) (note node time duration pitch) (note node time duration pitch velocity)))
        (array/push emitted [time (+ time duration) [source] :note node order])
        result)))
  (replace 'daw/param
    (fn [param]
      (fn [node time key value]
        (def source (origin))
        (def order (native/event-count node))
        (def result (param node time key value))
        (array/push emitted [time time [source] :param node order])
        result)))

  (fn []
    {:locations locations :events emitted
     :captures captures :patterns (length annotations) :fallback-events fallback-events :pushed-events pushed-events
     :seconds (- (os/clock) started)}))
