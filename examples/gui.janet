# Perone UIs: Tibia and A-SID effects, built in their own repositories.
# The editor hosts web/custom or generic controls; native GUI libraries remain optional.
(import ../lib/pattern :as p)
(import ../lib/music)

(def tibia (or (os/getenv "TIBIA_PERONE") "../tibia/out/perone/c/build/tibia-test.perone"))
(def asid (or (os/getenv "ASID_PERONE") "../asid/plugin/perone/build/asid.perone"))
(def synth (daw/plugin "plugins/synth_mono/build/plugin.perone"
  {:vco1_wave 2 :vcf_cutoff 6000 :vca_attack 4 :vca_release 90}))
(def test (daw/plugin tibia {:gain -9 :cutoff 12000}))
(def filter (daw/plugin asid {:cutoff 65 :lfo_amount 30 :lfo_speed 35}))
(daw/track synth {:effects [test filter] :gain 0.2})

(defn phrase [root]
  (def notes (p/steps 0.5 (map |(+ root $) [0 7 12 3 10 7 15 12])))
  (p/events (notes :length)
    (map (fn [[a b pitch]] [a (- b 0.08) [:note synth pitch 95]]) (notes :events))))

(def song (p/serial (seq [i :range [0 32]] (phrase ([36 36 39 34] (% i 4))))))
# 32 control points per second at 120 BPM, with different periods for each movement.
(def phase (p/curve (song :length) (* 16 (song :length)) identity))
(defn wave [cycles]
  (p/map |(* 0.5 (- 1 (math/cos (* 2 math/pi cycles $)))) phase))
(def end
  (daw/schedule 0 120
    (p/parallel [song
      (p/map |[:param test :gain (music/lerp -15 -6 $)] (wave 4))
      (p/map |[:param test :cutoff (* 600 (math/pow 20 $))] (wave 8))
      (p/map |[:param test :tremolo (music/lerp 0 70 $)] (wave 2))
      (p/map |[:param filter :cutoff (music/lerp 20 95 $)] (wave 8))
      (p/map |[:param filter :lfo_amount (music/lerp 10 65 $)] (wave 4))
      (p/map |[:param filter :lfo_speed (music/lerp 15 80 $)] (wave 2))])))
(daw/end (+ end 1))
