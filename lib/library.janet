# Native catalog and discovery preferences. Inspection runs in the scanner process.
(def files @[])
(def plugins @[])
(def sources @[])
(def seen @{})

(defn examples [root]
  (when (= :directory (os/stat root :mode))
    (each name (sort (os/dir root))
      (unless (string/has-prefix? "." name)
        (def path (string root "/" name))
        (case (os/lstat path :mode)
          :directory (examples path)
          :file (when (string/has-suffix? ".janet" name) (array/push files {:path path})))))))

(defn inspect [format path]
  (def resolved (os/realpath path))
  (unless (seen resolved)
    (put seen resolved true)
    (try
      (do
        (def entries
          (if (= format :perone)
            (let [entry (plugin/read path)]
              (assert (= :file (os/stat (entry :binary) :mode)) "Missing native DSP binary")
              [entry])
            (plugin/scan format path)))
        (each entry entries
          (array/push plugins {:format (string format) :path (if (= format :perone) path (entry :binary))
            :id (entry :id) :name (or (get-in entry [:product :name]) path)
            :error (entry :error) :ui-error (entry :ui-error)
            :kind (unless (entry :error)
              (cond
                (= (get-in entry [:layout :output-channels]) 0) "MIDI"
                (> (get-in entry [:layout :input-channels]) 0) "Effect"
                "Instrument"))})))
      ([err] (array/push plugins {:format (string format) :path path :name path :error (string err)})))))

(defn catalog []
  (var settings-error nil)
  (def settings (try (plugin/settings) ([err] (set settings-error (string err)) @{})))
  (when host/settings-input
    (def paths (plugin/check-paths (json/decode host/settings-input true)))
    (put settings :plugin-paths paths)
    (host/save-settings (string (json/encode settings)))
    (set settings-error nil))
  (each format [:perone :clap :lv2 :vst3]
    (def defaults (plugin/default-paths format))
    (def paths (or (get-in settings [:plugin-paths format]) defaults))
    (def roots @[])
    (each path paths
      (def root @{:path path})
      (try
        (do
          (def expanded (plugin/expand-path path))
          (if (os/stat expanded)
            (do
              (def bundles (plugin/files format expanded))
              (each bundle bundles (inspect format bundle)))
            (put root :error "Not found")))
        ([err] (put root :error (string err))))
      (array/push roots root))
    (array/push sources {:format (string format) :paths paths :defaults defaults :roots roots}))
  (examples "examples")
  {:sources sources :settingsPath host/settings-path :settingsError settings-error :editable (not (not host/settings-path))
   :files files :plugins plugins})

(string (json/encode (try (catalog) ([err] {:error (string err)}))))
