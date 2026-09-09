# Public score API: metadata stays here; native calls receive numeric configuration.
(def daw/nodes @{})
(def daw/products @{})
(def daw/mixer-parameters
  [{:index 0 :name :gain :label "Gain" :unit "linear" :min 0 :max 4 :default 1 :integer false :direction :input :map :linear}
   {:index 1 :name :pan :label "Pan" :unit "" :min -1 :max 1 :default 0 :integer false :direction :input :map :linear}])

(defn daw/info "Return immutable parameter descriptions for a plugin or mixer." [node]
  (or (daw/nodes node) (error "invalid node handle")))
(defn daw/product "Return the full product metadata; nil for mixers." [node]
  (daw/info node)
  (daw/products node))

(defn daw/parameter [parameters key]
  (def p (if (and (number? key) (= key (math/floor key)) (<= 0 key) (< key (length parameters)))
           (parameters key)
           (find |(= ($ :name) key) parameters)))
  (or p (error (string "unknown parameter " key))))

(defn daw/plugin "Instantiate a .perone bundle with optional initial parameter values." [path &opt params]
  (default params {})
  (assert (dictionary? params) "expected initial parameter dictionary")
  (def plugin (perone/read path))
  (def defaults (array ;(plugin :defaults)))
  (eachp [key value] params
    (def p (daw/parameter (plugin :parameters) key))
    (put defaults (p :index) (perone/value p value)))
  (def id (native/plugin (plugin :binary) (plugin :layout) defaults))
  (put daw/nodes id (plugin :parameters))
  (put daw/products id (plugin :product))
  id)

(defn daw/track [source &opt options]
  (def id (native/track source options))
  (put daw/nodes id daw/mixer-parameters)
  id)
(defn daw/master [&opt options]
  (def id (native/master options))
  (put daw/nodes id [(daw/mixer-parameters 0)])
  id)
(defn daw/param [node time parameter value]
  (def p (daw/parameter (daw/info node) parameter))
  (native/param node time (p :index) (perone/value p value)))
(def daw/note native/note)
(def daw/end native/end)
