# Bundle metadata and parameter semantics. Loaded by the host before any DSP runs.
(defn perone/finite? [x]
  (and (number? x) (= (- x x) 0) (<= (math/abs x) 3.4028234663852886e38)))

(defn perone/in-range? [p value]
  (and (perone/finite? value) (<= (p :min) value (p :max))
       (or (not (p :integer)) (= value (math/floor value)))))

(defn perone/value [p value]
  (assert (and (= (p :direction) :input) (perone/in-range? p value))
    (string "invalid value for " (p :name)))
  value)

(defn perone/parameters [parameters]
  (assert (and (indexed? parameters) (<= (length parameters) host/max-params)) "too many or invalid parameters")
  (def ids @{})
  (freeze (seq [[i p] :pairs parameters]
    (def id (p :id))
    (assert (and (string? id) (> (length id) 0) (not (ids id))) "invalid or duplicate parameter ID")
    (put ids id true)
    (assert (or (= (p :direction) "input") (= (p :direction) "output")) "invalid parameter direction")
    (def row {:index i :name (keyword id) :label (or (p :name) id) :unit (or (p :unit) "")
              :min (if (p :isBypass) 0 (p :minimum)) :max (if (p :isBypass) 1 (p :maximum))
              :default (if (p :isBypass) 0 (p :defaultValue))
              :integer (not (not (or (p :integer) (p :toggled) (p :isBypass))))
              :direction (keyword (p :direction)) :map (keyword (or (p :map) "linear"))
              :scale-points (p :scalePoints)})
    (assert (and (perone/finite? (row :min)) (perone/finite? (row :max))
                 (perone/in-range? row (row :default))) "invalid parameter range/default")
    row)))

(defn perone/read
  "Read a .perone bundle, preserving the product metadata and original indices."
  [path]
  (def product ((freeze (json/decode (slurp (string path "/product.json")) true)) :product))
  (def name (product :bundleName))
  (assert (and (string? name) (peg/match '(sequence (some (choice (range "az" "AZ" "09") "_" "-")) -1) name))
    "invalid bundleName")
  (assert (not (get-in product [:transport :sync])) "transport sync is not supported by this host")
  (each key [:uiToDspSize :dspToUiSize]
    (assert (= 0 (or (get-in product [:messaging key]) 0)) "messaging is not supported by this host"))
  (var input 0) (var output 0) (var midi -1) (var offset 0) (var slots 0)
  (def ids @{})
  (assert (indexed? (product :buses)) "invalid buses")
  (eachp [i b] (product :buses)
    (def id (b :id))
    (assert (and (string? id) (> (length id) 0) (not (ids id))) "invalid or duplicate bus ID")
    (put ids id true)
    (def out (= (b :direction) "output"))
    (assert (or out (= (b :direction) "input")) "invalid bus direction")
    (if (= (b :type) "midi")
      (do (assert (and (not out) (= midi -1)) "expected at most one MIDI input") (set midi i))
      (do
        (def channels (get {"mono" 1 "stereo" 2} (b :channels)))
        (assert (and (= (b :type) "audio") channels (not (b :cv))) "unsupported audio bus")
        (if (b :sidechain)
          (assert (and (not out) (b :optional)) "only disconnected optional sidechains are supported")
          (if out
            (do (assert (= output 0) "multiple audio outputs") (set output channels))
            (do (assert (= input 0) "multiple audio inputs") (set input channels) (set offset slots))))
        (unless out (+= slots channels)))))
  (assert (and (> output 0) (<= slots host/max-inputs)) "unsupported audio layout")
  (def parameters (perone/parameters (product :parameters)))
  {:product product :parameters parameters
   :binary (string path "/" host/platform "/" name ".so")
   :layout [input output midi offset slots]
   :defaults (map |(if (= ($ :direction) :input) ($ :default) nil) parameters)})
