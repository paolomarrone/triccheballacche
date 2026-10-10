# Unsaved Janet modules use the standard resolver, cache and circular-import checks.
# The overlay belongs to one evaluation; the filesystem is never modified.
(def- documents (json/decode host/documents-json))
(assert (dictionary? documents) "project documents must be an object")
(defn- absolute [path]
  (def parts @[])
  (each part (string/split "/" (if (string/has-prefix? "/" path) path (string (os/cwd) "/" path)))
    (case part
      "" nil "." nil
      ".." (when (length parts) (array/pop parts))
      (array/push parts part)))
  (string "/" (string/join parts "/")))
(def- sources @{})
(eachp [path text] documents
  (assert (and (string? path) (string? text) (not (string/find "\0" text))) "invalid project document")
  (put sources (absolute path) text))

(defn- load-document [path args]
  (def loading (dyn :module-loading module/loading))
  (put loading path true)
  (defer (put loading path nil)
    (def options (table ;args))
    (def env (or (options :env) ((dyn :module-make-env make-env))))
    (var text (sources (absolute path)))
    (var failure nil)
    (defn diagnostic [f & args]
      (def buffer @"")
      (with-dyns [:err buffer] (f ;args))
      (put env :exit true)
      (set failure [(string buffer)]))
    (merge-into options @{:env env :source path
      :chunks (fn [buf _] (when text (buffer/push-string buf text) (set text nil)))
      :on-status (fn [f value]
                   (unless (= :dead (fiber/status f))
                     (put env :exit true)
                     (set failure [value f])))
      :on-parse-error (partial diagnostic bad-parse)
      :on-compile-error (partial diagnostic bad-compile)})
    (def result (run-context options))
    (when failure
      (if (get failure 1) (propagate (failure 0) (failure 1)) (error (failure 0))))
    result))

(def- paths @[])
(each [pattern kind check] module/paths
  (when (and (= kind :source) (string? pattern))
    (array/push paths
      [(fn [path]
         (def expanded (string (module/expand-path path pattern)))
         (when (has-key? sources (absolute expanded)) expanded))
       load-document check])))
(put (curenv) :module-paths (array/concat paths module/paths))
