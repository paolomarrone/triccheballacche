# Optional source tracing. Install once, before compiling the score and its imports.
# Bindings and annotations live only until the host closes Janet after preparation.
# Keep annotations outside musical values. Janet may coalesce equal constants, so equal
# patterns collect possible origins instead of claiming a source identity that cannot survive compilation.
(def self (dyn :current-file))
(def module (require "./pattern"))

(defn install [env entry]
  (assert (nil? (root-env :trace/push)) "source tracing is already installed")
  (def originals @{})
  (each name '[events steps curve serial parallel map stretch reverse loop]
    (put originals name (get-in module [name :value])))
  (def streams (get-in module ['streams :value]))
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

  # Store location IDs by source and event, without duplicating musical times or values.
  # map, stretch and reverse keep this order, while loop joins all sources.
  (defn uniform [pattern source]
    (seq [stream :in (pattern :streams)]
      (map (fn [_] [source]) (stream :events))))

  (defn provenance [pattern fallback]
    (if-let [stored (annotations pattern)]
      stored
      (do
        (each stream (pattern :streams)
          (+= fallback-events (length (stream :events))))
        (uniform pattern fallback))))

  (eachp [name f] originals
    (def wrapped
      (fn [& args]
        # Capture before invoking f; a user helper may already have disappeared in a tail call.
        (def source (origin))
        (def result (f ;args))
        (def trace
          (cond
            (= name 'events)
              (if (empty? (args 1)) []
                [(seq [[i item] :pairs (args 1)]
                  (def slot (get-in appended [(args 1) i]))
                  (def saved (and slot (= (slot 0) item)))
                  (when saved (++ pushed-events))
                  [(if saved (slot 1) source)])])
            (or (= name 'serial) (= name 'parallel))
              (seq [pattern :in (args 0) stream :in (provenance pattern source)] stream)
            (or (= name 'map) (= name 'stretch)) (provenance (args 1) source)
            (= name 'reverse) (provenance (args 0) source)
            (= name 'loop) [(seq [stream :in (provenance (args 0) source) origins :in stream] origins)]
            (uniform result source)))
        (def previous (annotations result))
        (put annotations result
          (if previous
            (seq [[si stream] :pairs trace]
              (seq [[ei origins] :pairs stream]
                (distinct (tuple ;((previous si) ei) ;origins))))
            trace))
        result))
    (put module name (table/setproto @{:value wrapped} (module name))))

  (defn replace [name wrap]
    (def binding (env name))
    (put env name (table/setproto @{:value (wrap (binding :value))} binding)))

  # Both entry points emit sources in the same order; the report records their
  # absolute template times. The audio score owns repetition and sample rounding.
  (defn scheduled [f args pattern start bpm]
    (def source (origin))
    (def sources (streams pattern))
    (def traces (provenance pattern source))
    (def orders @{})
    (each stream sources
      (each [_ _ command] (stream :events)
        (def node (command 1))
        (unless (orders node) (put orders node (native/event-count node)))))
    (def result (f ;args))
    (def unit (/ 60 bpm))
    (eachp [si stream] sources
      (eachp [ei [a b command]] (stream :events)
        (def node (command 1))
        (def order (orders node))
        (array/push emitted
          [(+ start (* unit (+ (stream :offset) a)))
           (+ start (* unit (+ (stream :offset) b)))
           ((traces si) ei) (command 0) node order])
        (put orders node (+ order 1))))
    result)

  (replace 'daw/schedule
    (fn [schedule]
      (fn [start bpm pattern]
        (scheduled schedule [start bpm pattern] pattern start bpm))))
  (replace 'daw/score
    (fn [score]
      (fn [pattern &opt options]
        (scheduled score [pattern options] pattern 0 (get-in env ['daw/bpm :ref 0])))))

  # schedule was compiled before these replacements, so scheduled notes are not recorded twice.
  (replace 'daw/note
    (fn [note]
      (fn [node time duration pitch &opt velocity channel]
        (def source (origin))
        (def order (native/event-count node))
        (def result (note node time duration pitch (or velocity 100) (or channel 0)))
        (array/push emitted [time (+ time duration) [source] :note node order])
        result)))
  (replace 'daw/midi-event
    (fn [send]
      (fn [node time data]
        (def source (origin))
        (def order (native/event-count node))
        (def result (send node time data))
        (array/push emitted [time time [source] :midi node order])
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
