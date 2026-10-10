# Native discovery. CLAP inspection runs in the scanner process, never in Janet.
(def files @[])
(def plugins @[])
(defn add [path]
  (array/push files {:path path :text (slurp path)}))
(defn children [path]
  (if (= :directory (os/stat path :mode)) (sort (os/dir path)) []))
(defn bundle [path]
  (when (= :file (os/stat (string path "/product.json") :mode))
    (try
      (let [info (plugin/read path)]
        (when (= :file (os/stat (info :binary) :mode))
          (add (string path "/product.json"))))
      ([err] (eprint "Library: " path ": " err)))))
(defn collection [root accept]
  (each name (children root)
    (when (accept name)
      (def build (string root "/" name "/build"))
      (each name (children build)
        (when (string/has-suffix? ".perone" name)
          (bundle (string build "/" name)))))))
(defn examples [root]
  (each name (children root)
    (unless (string/has-prefix? "." name)
      (def path (string root "/" name))
      (case (os/lstat path :mode)
        :directory (examples path)
        :file (when (string/has-suffix? ".janet" name) (array/push files {:path path}))))))
(def brickworks (or (os/getenv "BRICKWORKS_PERONE") "build/library/brickworks"))
(def asid (or (os/getenv "ASID_PERONE") "../asid/plugin/perone/build/asid.perone"))
(def tibia (or (os/getenv "TIBIA_PERONE") "../tibia/out/perone/c/build/tibia-test.perone"))
(collection "plugins" (fn [_] true))
(collection brickworks
  |(or (string/has-prefix? "fx_" $) (string/has-prefix? "synth_" $)))
(bundle asid)
(bundle tibia)
(def clap-paths (plugin/clap-paths))
(def seen @{})
(each root clap-paths
  (each path (plugin/clap-files root)
    (try
      (each entry (json/decode (host/scan path) true)
        (def key [(entry :binary) (entry :id)])
        (unless (seen key)
          (put seen key true)
          (array/push plugins {:format "clap" :path (entry :binary) :id (entry :id)
            :name (get-in entry [:product :name])
            :kind (if (> (get-in entry [:layout :input-channels]) 0) "Effect" "Instrument")})))
      ([err] (eprint "Library: " path ": " err)))))
(examples "examples")
(string (json/encode {:paths (array/concat @[] ["plugins" brickworks asid tibia] clap-paths) :files files :plugins plugins}))
