(def append array/push)
(defn fill [items node]
  (append items [0.3 0.3 [:param node :gain 0.35]])) # imported producer, alias and tail call
