# Native library discovery. Return file contents without loading DSPs or evaluating scores.
(def files @[])
(defn add [path]
  (array/push files {:path path :text (slurp path)}))
(defn children [path]
  (if (= :directory (os/stat path :mode)) (sort (os/dir path)) []))
(defn bundle [path]
  (when (= :file (os/stat (string path "/product.json") :mode))
    (try
      (let [info (perone/read path)]
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
(def brickworks (or (os/getenv "BRICKWORKS_PERONE") "../brickworks/build/perone"))
(def asid (or (os/getenv "ASID_PERONE") "../asid/plugin/perone/build/asid.perone"))
(def tibia (or (os/getenv "TIBIA_PERONE") "../tibia/out/perone/c/build/tibia-test.perone"))
(collection "plugins" (fn [_] true))
(collection brickworks
  |(or (string/has-prefix? "fx_" $) (string/has-prefix? "synth_" $)))
(bundle asid)
(bundle tibia)
(examples "examples")
(string (json/encode {:paths ["plugins" brickworks asid tibia] :files files}))
