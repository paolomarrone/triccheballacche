# Perone bundle metadata; parameter validation belongs to plugin/read.
(defn- perone/load [path]
  (def product ((freeze (json/decode (slurp (string path "/product.json")) true)) :product))
  (def name (product :bundleName))
  (assert (and (string? name) (peg/match '(sequence (some (choice (range "az" "AZ" "09") "_" "-")) -1) name))
    "invalid bundleName")
  (assert (or (not (get-in product [:transport :sync])) (get-in product [:transport :syncOptional]))
    "required transport sync is not supported by this host")
  (each key [:uiToDspSize :dspToUiSize]
    (def size (or (get-in product [:messaging key]) 0))
    (assert (and (number? size) (= size (math/floor size)) (<= 0 size host/max-message))
      "unsupported plugin message size"))
  (var input 0)
  (var output 0)
  (var midi -1)
  (var offset 0)
  (var slots 0)
  (def ids @{})
  (assert (indexed? (product :buses)) "invalid buses")
  (eachp [i b] (product :buses)
    (def id (b :id))
    (assert (and (string? id) (> (length id) 0) (not (ids id))) "invalid or duplicate bus ID")
    (put ids id true)
    (def out (= (b :direction) "output"))
    (assert (or out (= (b :direction) "input")) "invalid bus direction")
    (if (= (b :type) "midi")
      (do
        (assert (and (not out) (= midi -1)) "expected at most one MIDI input")
        (set midi i))
      (do
        (def channels (get {"mono" 1 "stereo" 2} (b :channels)))
        (assert (and (= (b :type) "audio") channels (not (b :cv))) "unsupported audio bus")
        (if (b :sidechain)
          (assert (and (not out) (b :optional)) "only disconnected optional sidechains are supported")
          (if out
            (do
              (assert (= output 0) "multiple audio outputs")
              (set output channels))
            (do
              (assert (= input 0) "multiple audio inputs")
              (set input channels)
              (set offset slots))))
        (unless out (+= slots channels)))))
  (assert (and (> output 0) (<= slots host/max-inputs)) "unsupported audio layout")
  # Perone controls use float32; the common host control domain is double.
  (each p (product :parameters)
    (each key [:minimum :maximum :defaultValue]
      (def value (p key))
      (assert (and (number? value) (<= (math/abs value) 3.4028234663852886e38))
        "Perone parameter is outside float32 range")))
  (def binary (string path "/" host/platform "/" name))
  {:id name
   :product (merge product {:format "perone" :id name})
   :binary (string binary host/binary-suffix)
   :ui-binary (when (product :ui) (string binary "-ui" host/binary-suffix))
   :layout {:input-channels input
            :output-channels output
            :midi-bus midi
            :input-offset offset
            :input-slots slots
            :dsp-to-ui-size (or (get-in product [:messaging :dspToUiSize]) 0)
            :ui-to-dsp-size (or (get-in product [:messaging :uiToDspSize]) 0)}})

# One immutable description per bundle in this evaluation. A new Run rereads metadata.
(def- perone/cache @{})
(defn perone/read
  "Read and cache an immutable bundle description, preserving product metadata and original indices."
  [path]
  (or (perone/cache path)
      (let [bundle (freeze (perone/load path))]
        (put perone/cache path bundle)
        bundle)))
