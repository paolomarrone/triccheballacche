# Common parameter metadata. Formats provide descriptions; scores never instantiate DSPs.
(defn plugin/finite? [x]
  (and (number? x) (= (- x x) 0)))

(defn plugin/in-range? [p value]
  (and (plugin/finite? value) (<= (p :min) value (p :max))
       (or (not (p :integer)) (= value (math/floor value)))))

(defn plugin/value [p value]
  (assert (and (= (p :direction) :input) (plugin/in-range? p value))
    (string "invalid value for " (p :name)))
  value)

(defn plugin/parameters [parameters]
  (assert (and (indexed? parameters) (<= (length parameters) host/max-params)) "too many or invalid parameters")
  (def ids @{})
  (freeze
    (seq [[i p] :pairs parameters]
      (def id (p :id))
      (assert (and (string? id) (> (length id) 0) (not (ids id))) "invalid or duplicate parameter ID")
      (put ids id true)
      (assert (or (= (p :direction) "input") (= (p :direction) "output")) "invalid parameter direction")
      (def row
        {:index i
         :native-id (or (p :nativeId) i)
         :automatable (not (= (p :automatable) false))
         :name (keyword id)
         :label (or (p :name) id)
         :unit (or (p :unit) "")
         :min (if (p :isBypass) 0 (p :minimum))
         :max (if (p :isBypass) 1 (p :maximum))
         :default (if (p :isBypass) 0 (or (p :initialValue) (p :defaultValue)))
         :integer (not (not (or (p :integer) (p :toggled) (p :isBypass))))
         :direction (keyword (p :direction))
         :map (keyword (or (p :map) "linear"))
         :scale-points (p :scalePoints)})
      (assert (and (plugin/finite? (row :min))
                   (plugin/finite? (row :max))
                   (plugin/in-range? row (row :default)))
        "invalid parameter range/default")
      row)))


(def- plugin/cache @{})

(defn plugin/default-paths [format]
  (distinct
    (if (= format :perone)
      ["plugins" (or (os/getenv "BRICKWORKS_PERONE") "build/library/brickworks")
       (or (os/getenv "MDA_PERONE") "build/library/mda")
       (or (os/getenv "ASID_PERONE") "../asid/plugin/perone/build/asid.perone")
       (or (os/getenv "TIBIA_PERONE") "../tibia/out/perone/c/build/tibia-test.perone")]
      (do
        (def variable (case format :clap "CLAP_PATH" :lv2 "LV2_PATH" :vst3 "VST3_PATH" (error "unsupported plugin format")))
        (def custom (os/getenv variable))
        (def suffix (string format))
        (def defaults [(string (or (os/getenv "HOME") "") "/." suffix)
                      (string "/usr/local/lib/" suffix) (string "/usr/lib/" suffix)])
        (filter |(> (length $) 0)
          (if (and (= format :lv2) custom) (string/split ":" custom)
            (array/concat @[] (string/split ":" (or custom "")) defaults)))))))

# Preferences contain only discovery paths; explicit score references are unchanged.
(defn plugin/check-paths [paths]
  (assert (dictionary? paths) "Plugin paths must be an object")
  (eachp [format roots] paths
    (assert (index-of format [:perone :clap :lv2 :vst3]) "Unknown plugin format")
    (assert (and (indexed? roots) (<= (length roots) 64)) "Expected at most 64 paths per format")
    (each root roots
      (assert (and (string? root) (< 0 (length root) 4096)
                   (not (string/find "\0" root)) (not (string/find "\n" root))) "Invalid plugin path")))
  paths)

(defn plugin/settings []
  (def value (if (and host/settings-path (os/stat host/settings-path))
    (json/decode (slurp host/settings-path) true) @{}))
  (assert (dictionary? value) "Settings must be an object")
  (plugin/check-paths (get value :plugin-paths @{}))
  value)

(var- plugin/search-paths nil)
(defn plugin/paths [format]
  (unless plugin/search-paths (set plugin/search-paths (or ((plugin/settings) :plugin-paths) @{})))
  (or (plugin/search-paths format) (plugin/default-paths format)))

(defn plugin/expand-path [path]
  (if (or (= path "~") (string/has-prefix? "~/" path))
    (string (or (os/getenv "HOME") (error "HOME is not set")) (string/slice path 1)) path))

(defn plugin/files [format path &opt depth]
  (default depth 0)
  (def expanded (plugin/expand-path path))
  (def root (if (= expanded "/") expanded (string/trimr expanded "/")))
  (def files @[])
  (def kind (os/stat root :mode))
  (def name (last (string/split "/" root)))
  (cond
    (and (> (length name) (+ 1 (length (string format))))
         (string/has-suffix? (string "." format) name)
         (if (= format :clap) (= kind :file) (= kind :directory)))
    (when (or (not= format :perone) (= :file (os/stat (string root "/product.json") :mode)))
      (array/push files root))
    (and (< depth 8) (= kind :directory))
    (each name (sort (os/dir root))
      (unless (string/has-prefix? "." name)
        (def path (string root "/" name))
        (when (or (not (= :directory (os/stat path :mode)))
                  (= :directory (os/lstat path :mode))
                  (string/has-suffix? (string "." format) name))
          (array/concat files (plugin/files format path (+ depth 1)))))))
  files)

(defn plugin/sample-rate [description]
  (each p (get-in description [:product :parameters])
    (when (p :sampleRate)
      (each key [:minimum :maximum :defaultValue]
        (put p key (* (p key) host/sample-rate)))
      (eachp [label value] (or (p :scalePoints) {})
        (put (p :scalePoints) label (* value host/sample-rate)))))
  description)

(def- plugin/descriptions @{})
(defn plugin/scan [path]
  (or (plugin/descriptions path)
      (let [entries (json/decode (host/scan path) true)]
        # Normalize once, before any caller sees or caches the description.
        (each entry entries (unless (entry :error) (plugin/sample-rate entry)))
        (put plugin/descriptions path entries)
        entries)))

(defn plugin/find [format id]
  (assert (string? id) "plugin reference needs :id or :path")
  (var found nil)
  (each root (plugin/paths format)
    (unless found
      (each path (plugin/files format root)
        (unless found
          (try
            (set found (find |(= ($ :id) id) (plugin/scan path)))
            ([err] (eprint "Plugin: " path ": " err)))))))
  (or found (error (string "plugin not found: " id "; check Settings or the plugin search path"))))

(defn plugin/read [reference]
  (or (plugin/cache reference)
      (let [description
            (if (string? reference)
              (perone/read reference)
              (do
                (def format (reference :format))
                (assert (or (= format :clap) (= format :lv2) (= format :vst3)) "unsupported plugin format")
                (def path (reference :path))
                (def id (reference :id))
                (def entry
                  (if path
                    (do
                      (assert (string? path) "plugin :path must be a string")
                      (def entries (plugin/scan path))
                      (assert (or id (= 1 (length entries))) "bundle has multiple plugins; specify :id")
                      (or (if id (find |(= ($ :id) id) entries) (entries 0))
                          (error "plugin ID not found")))
                    (plugin/find format id)))
                (assert (not (entry :error)) (string "plugin unavailable: " (entry :id) ": " (entry :error)))
                (assert (= (string format) (get-in entry [:product :format])) "plugin format does not match reference")
                entry))
            parameters (plugin/parameters (get-in description [:product :parameters]))
            bundle (freeze (merge description
              {:parameters parameters
               :defaults (map |(if (= ($ :direction) :input) ($ :default) nil) parameters)}))]
        (put plugin/cache reference bundle)
        bundle)))
