# GHOSTWAVE architecture

GHOSTWAVE is a native C++17 first-person habitat application. Its default mode
supports continued exploration, feeding, refuge rest, and persistent records.
Rendering and sound interpret sensory information; they do not define the
electrical model.

## Boundaries

| Layer | Files | Contract |
| --- | --- | --- |
| Electrical model | src/electrosense.hpp, src/electrosense.cpp | SI electroquasistatic calculations and signal mixing |
| Habitat | src/simulation.hpp, src/simulation.cpp | Movement, populations, water, interactions, records, events, and saves |
| Sensory bridge | src/sensory_frame.hpp | Converts current world state to a bounded SensoryFrame |
| Presentation | src/main.cpp, src/habitat_render.hpp, and assets | Camera, regional terrain, authored signal display, menus, chart, and controls |
| Audio | src/audio.hpp | Atomic publication and callback-owned synthesis |

CMake combines habitat and electrical code into ghostwave_core. The three
headless test executables link it. The audio suite compiles the production
synthesizer with an offline device API double; it needs no raylib library or
audio hardware. raylib belongs to the graphical application, outside the core's
public model API. The optional field_lab executable exports dimensioned
electrical sweeps and independently checks each row's analytical identity.

[SCIENCE.md](SCIENCE.md) distinguishes analytical results, numerical continuations,
physical approximations, and uncalibrated behavior. Passing mathematical identity
tests does not establish biological validity.

## Time, input, and events

Simulation::step clears the previous event batch, ignores invalid time or inactive
run states, bounds work to 100 ms, and subdivides it into steps no longer than
1/120 second. Focus, burst, and automatic-tuning edges apply once per outer step;
movement and interaction continue on each substep. Pause stops simulation time.

Events cover focus, burst, research, region discovery, feeding, species
observation, refuge rest, returning home, and challenge results. Consumers process
them before the next step. Explicit actions such as returnHome can also publish
events between ticks, so event novelty cannot depend only on elapsed time.

Camera-relative horizontal and vertical intent share one normalization. The
application applies Input::propulsionScale after normalization, preserving raw
movement intent for refuge-rest decisions. New habitats start at 1/30 of the
configured propulsion speed: 0.10 m/s with the shipped tuning. T or R3 enables
travel assist at 3 m/s and allows 8.5 m/s bursts. These speeds are authored
controls, not a physiological calibration. A burst preserves its launch heading
and integrates its exact remaining duration. Travel distance accumulates actual
three-dimensional displacement after current, boundaries, and collision response.
Finite-height cylinders resolve both side and cap contacts. Generated corridors
leave routes between regions, refuges, and sites.

## Habitat and ecology

The default world is 14,400 × 9,600 simulation units. The presentation and sensory
bridge use 60 units per metre: 240 × 160 m horizontally. The default body length of
12 units is 0.20 m. Altitude is separate; the bridge maps (world x, altitude,
world y) to Y-up metres.

The deterministic worldSeed builds six 4,800-unit regions:

| Region | Authored conditions |
| --- | --- |
| Rootwater Refuge | Slow current, sheltered roots, starting refuge |
| Silt Forest | Suspended sediment and submerged cover |
| Basalt Narrows | Stronger current and higher conductivity |
| Flooded Gallery | Shaded, low-conductivity water |
| Open Channel | Faster flow and open swimming space |
| Deep Basin | Cooler water and scattered shelter |

The population contains 24 fish, 72 prey items, six refuges, and 12 research sites.
The default seed generates 192 finite-height obstacles; bounded placement attempts
can produce fewer for other seeds to retain clearance.

Conductivity, turbidity, temperature, oxygen, and current blend across regions.
Current slows near the bed. These are authored fields, not a hydrodynamic or
water-quality solver. An accelerated day cycle changes activity.

Brown ghosts, glass knifefish, pulse knifefish, and non-electric catfish follow
qualitative foraging, sheltering, avoidance, territorial, and nearby-shoaling
rules. Appetite controls prey pursuit and feeding; satiated fish leave prey
available. Nearby glass knifefish can align and maintain shoaling distance.
Their carrier/pulse-rate ranges differ. Neighbours do not retune to track the
player's frequency.

Feeding consumes prey and starts its recovery timer. Still, quiet movement inside
a refuge enables rest and sheltered station-keeping. Rest reduces fatigue;
condition recovery requires nourishment. Full-fed fish do not consume prey merely
because interaction remains held. Nourishment, fatigue, and condition use tuned
game rates, not physiological measurements. Sustained close observation records
each species once. Regional and research records persist. Nearby research sites
reveal continuously in free exploration without requiring a focus action.

Free exploration is the default and continues after research completion. The
simulation also contains a `surveyMode` challenge variant with separate win/loss
and interference-damage rules. It is exercised by the tests but cannot be turned
on from the game.

## Electrical model and live bridge

The library implements a current dipole in a homogeneous complex conducting
medium, uniform-incident spherical responses, spheroid polarizability, surface
sample arrays, and coherent wave mixing. It returns voltages, field components,
phase, and approximation flags.

The live bridge uses:

- 48 numerical body-surface samples, not anatomical receptor counts.
- Up to 12 passive targets: available prey spheres and fish-body spheroids.
- Up to eight wave emitters.
- Explicit metre, S/m, volt, and hertz conversions.
- Surface-minus-body-centre perturbations, so a constant potential offset cannot
  masquerade as a skin signal.

The 2 m target and 8 m emitter selection cutoffs are computation limits, not
biological detection thresholds. Large terrain cylinders are omitted from the
passive-target approximation and flagged. Their painted outlines are not exact
electrical solutions for rocks.

Pulse fish are excluded from the sinusoidal wave mixer. Catfish have no fictitious
EOD, although their bodies can perturb the player's field. Pulse-fish audification
is a separate synthetic biphasic train, not a broadband biological receptor model.

SensoryClock integrates carrier phase between observations, preserves continuity
through retuning, and resets on a changed world or restarted timeline. Repeated
evaluation at one simulation instant does not advance it. It is independent of
the audio clock.

The habitat's contamination, frequencyWindow, interference, and band labels are
control/feedback heuristics, separate from the SI surface calculations. In free
mode the EOD remains active during quiet movement, Q requests only an upward JAR
shift, and interference alone does not reduce condition. Broad manual retuning,
focus effects, and research objectives remain interaction abstractions.

## Presentation

The camera places the player inside the water volume. The display combines
authored habitat art with an interpretation of amplitude, timing, contrast, and
interference. Focus blooms, scan outlines, palettes, display gain, and stylized
terrain responses belong to presentation.

The quasistatic field has no slow travelling sonar echo. An attention animation
must not be read as electromagnetic propagation delay. Spatial sound is
audification for the player, not a claim that fish hear EOD as musical tones.

HUD and menus remain outside the main scene post-processing for legibility.
The body display preserves all 48 sampled skin sites in six rows. It shows
amplified signal values, without claiming anatomical receptor density. Nearby
fish ribbons follow source heading and display an authored field interpretation.
Reduced motion and sound settings are application preferences.

The habitat renderer draws a continuous world floor and nearby regional geometry.
It submits nearby collision cores before optional detail, then uses distance and
draw budgets for strata, roots, sediment, and grass. Small decorations stay below
the swimming volume; banks sit outside the playable bounds. The caller owns model,
texture, and shader lifetimes.

## Audio threading

Audio::update(sim, muted, paused, listenerYaw) runs on the application thread.
The optional yaw argument supplies head-relative stereo; the three-argument
call remains valid.

Only lock-free atomics cross into the 48 kHz stereo callback. The callback owns
oscillator phases, noise, envelopes, filters, and fixed voice pools. It does not
read simulation containers, allocate, or take a lock. Twenty-four stable source
slots preserve phase as another habitat becomes audible; inactive oscillators
advance by a block integral.

Double-precision carrier ramps avoid frequency bias from float-rounding stalls.
Wave carriers stay active during quiet free swimming and refuge rest; only the
movement ambience becomes quieter. They produce actual near-frequency beating.
Pulse neighbours use a synthetic biphasic train. Water, recording, discovery,
feeding, rest, homecoming,
and nearby alert behavior have restrained cues. Output is filtered and
soft-limited below 0.24 full scale; mute fades to exact zero. Reused event batches
do not retrigger cues.

## Persistence and assets

saveWorld writes the versioned GHOSTWAVE_HABITAT 3 format to a sibling temporary
file, flushes it, then replaces the destination. Windows uses MoveFileExW with
replacement/write-through flags; other builds use filesystem rename.

Loading accepts format versions 2 and 3, reconstructs the seeded habitat in a
candidate Simulation, validates counts and bounded finite values, restores
records, then replaces live state
only after accepting the complete file. Counters are derived from records.
Invalid or unsupported files leave the current habitat intact.

The checkpoint includes position, depth, time, current/target carrier, condition
and nutrition, discoveries, species knowledge, research, prey recovery, and fish
state including appetite. Version 2 saves initialize the added appetite value.
It is not a bit-exact process snapshot: transient input, focus/burst
effects, audiovisual phases, and player momentum are not preserved.

Settings and habitat saves live outside the installation folder. Shaders, font,
configuration and third-party notices are copied next to the executable; the
three art textures are not distributed (see `assets/art/README.md`).
Travel assist joins sound and reduced motion in application settings. Save &
Return to Title and Save & Quit require a successful write; a failure keeps the
current world available and displays the error.
Third-party notices are in `assets/licenses/`.

## Verification boundaries

Core tests cover mechanics, depth, generated connectivity, ecological interactions,
persistence, and a six-region route using movement input. Electrical tests cover
analytical identities, numerical guards, and the live bridge. Offline
audio tests cover carrier convergence, phase, pulse shape, stereo, bounded output,
mute, event deduplication, and device ownership protocol through test doubles.
They do not test real device initialization, headphone quality, or driver behavior.

These checks establish implementation contracts. They do not substitute for
interactive controls, GPU, gamepad, accessibility, or empirical biological tests.
