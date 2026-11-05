# Electric sense: model, evidence, and limits

GHOSTWAVE uses **Apteronotus leptorhynchus**, the brown ghost knifefish, as its
biological reference. The electric-sense library is a dimensioned, testable
electroquasistatic approximation. The river, its population, and the audiovisual
experience are an authored simulation. This is not a complete organism model,
a validated reconstruction of an individual fish, or a claim about what a fish
consciously sees.

## Biological foundation

Brown ghosts continuously generate a wave-type electric organ discharge (EOD).
A representative baseline of 720 Hz lies within the approximately 650–1000 Hz
species range reported in experimental work. Frequency varies between
individuals and with conditions, including temperature; the selected value does
not identify the simulated animal's sex. The organ is neurogenic and its timing
is controlled by the pacemaker system. [Kawasaki & Zupanc, 2024](https://doi.org/10.1007/s00359-024-01719-0)

An object with electrical properties different from water perturbs the ongoing
field. That perturbation is sampled across the skin, not at a pair of forward
looking eyes. Changes in amplitude and timing are informative. A sphere model
can predict important object-dependent effects, but actual skin voltages and
the fish's own field are much more complicated than a single dipole.
[Rasnow, 1996](https://www.rasnowpeak.com/brian/pub/objects.pdf)

Measured brown-ghost fields have spatially varying waveforms and phases. An
anatomically detailed inverse finite-element study used a 21 cm fish at 810 Hz
and 230 microSiemens/cm. Its distributed electric-organ model cannot be replaced
by a point dipole without losing near-field structure. We therefore use 20 cm
as a convenient model length, not a calibrated reconstruction of that animal.
[Shifman, Longtin & Lewis, 2015](https://doi.org/10.1038/srep15780)

Close wave-type emitters produce beats. The amplitude-envelope period depends
on their frequency difference, and spatial geometry also matters. Brown ghosts
have an **upward-only jamming avoidance response**, so the game only shifts
the player's frequency upward.
The reduced library calculates physical signal mixing, not a neural decision
or a universal threshold at which perception fails.
[Shifman & Lewis, 2018](https://doi.org/10.1098/rsif.2017.0633)

## Units and default parameters

The public API is `ghost::electro` in `src/electrosense.hpp`. It has no renderer,
audio engine, game-state, or random-number dependency. All phasors are **peak**
amplitudes with the convention `Re{A exp(+i 2 pi f t)}`.

| Quantity | API unit / default | Status |
| --- | --- | --- |
| Position, object radius, fish length | metres / fish length 0.20 m | Chosen geometry |
| Carrier frequency | Hz / 720 Hz | Biologically plausible baseline |
| Water conductivity | S/m / 0.01 S/m | Chosen freshwater condition |
| Water relative permittivity | dimensionless / 80 | Bulk approximation |
| Dipole moment | A m / 2e-6 A m | Uncalibrated source scale |
| Source core radius | metres / 0.002 m | Numerical continuation radius |
| Default sphere radius | metres / 0.004 m | Example target size |
| Default sphere conductivity | S/m / 0.03 S/m | Illustrative material |
| Default sphere relative permittivity | dimensionless / 80 | Resistive example |
| Potential and electric field | V and V/m | Physical model output |
| Phase | radians | Complex-signal phase |
| Normalization floor | V / 1e-6 V | Numerical guard, **not a detection threshold** |

`1 microSiemens/cm = 1e-4 S/m`. A world length must be converted explicitly:
`metres = worldUnits / 60`. The default player body length is 12 world units,
or 0.20 m. Changing its body length does not change the world's unit conversion.
Screen coordinates are never metres. Source moment, receiver location, and conductivity jointly set voltage;
changing a display gain is not a change in electrical source strength.

Freshwater and relative-permittivity values have the right order of magnitude
for this approximation. They do not specify a surveyed river. A mathematical
electrolocation treatment motivates the complex-admittivity formulation and
explains why body/skin boundary conditions matter.
[Ammari, Boulier & Garnier, 2013](https://arxiv.org/abs/1203.0938)

## Implemented physics

For conductivity `sigma`, relative permittivity `epsilon_r`, angular frequency
`omega = 2 pi f`, and vacuum permittivity `epsilon_0`, the material admittivity is

```text
gamma = sigma + i omega epsilon_0 epsilon_r       [S/m]
```

The medium is homogeneous, isotropic, linear, and unbounded. Fields respond
quasistatically to the current configuration. There is no slow traveling echo
or propagation delay in this model. Frequency enters material response and
signal phase; it does not produce a visible outgoing sensing shell.

### Source field

For displacement `r` from a current dipole of complex moment `p`:

```text
phi(r) = (p . r) / (4 pi gamma |r|^3)                    [V]
E(r)   = [3 r (p . r) / |r|^5 - p / |r|^3] / (4 pi gamma) [V/m]
```

The potential is odd and the field even under inversion. Along a fixed ray,
potential falls as `r^-2` and field as `r^-3`. The axial field is twice the
equatorial magnitude. These are statements about a point dipole, not universal
distance laws for a fish's near field.

At `r < coreRadiusM`, a polynomial potential replaces the singular solution
and matches its value and first derivative at the core boundary. The field
remains the negative potential gradient. The result is explicitly flagged
`regularized`; it must not be interpreted as the electric organ's interior.
With a zero core, sampling exactly at the source throws an exception.

### Small spherical object

The undisturbed field is evaluated at the object's centre and treated as
uniform over the object. For radius `a`, incident field `E0`, and admittivities
`gamma_o`, `gamma_w`:

```text
K = (gamma_o - gamma_w) / (gamma_o + 2 gamma_w)
delta_phi(r) = a^3 K (E0 . r) / |r|^3       outside the sphere
delta_phi(r) = K (E0 . r)                    inside the sphere
```

This is the homogeneous-sphere result underlying Rasnow's object model. The
contrast vanishes for matched materials, tends to `+1` for a good conductor,
and to `-1/2` for an ideal resistive insulator. The bounded interior solution
preserves potential continuity and normal complex-current continuity for a
uniform incident field. [Rasnow, equation 1 and appendix](https://www.rasnowpeak.com/brian/pub/objects.pdf)

Do not infer material solely from the sign of a displayed patch: source and
receiver geometry also determine sign. Capacitance is represented by effective
bulk permittivity. A large value can generate quadrature response; it is not a
measured membrane capacitance, a circuit capacitor in farads, or a classifier
that automatically distinguishes living things.

`SphereResponse` caches the incident field and contrast. Its `sizeDistanceRatio`
and `sourceCoreOverlap` expose obvious violations of the small-object
approximation. A ratio below 0.2 is only a diagnostic convention. It is not a
proven error bound. Multiple targets add their first-order responses; targets
do not polarize one another.

An active return travels through **two geometric factors**: the fish's field
at the target, then the target perturbation at the receptor. For a fixed small
target on the source axis and a receiver at the source position, this model's
return potential scales as `r^-5`. This analytical limiting case explains why
one must not apply a single inverse-cube rule to all active object detection.

### Oriented objects

`Spheroid` represents an ellipsoid with one symmetry axis and two equal
transverse semi-axes. The library uses its quasistatic polarizability tensor:

```text
p_j = V gamma_w (gamma_o-gamma_w)
      / [gamma_w + N_j (gamma_o-gamma_w)] E0_j
N_parallel + 2 N_transverse = 1
```

An elongated target therefore changes response when rotated relative to the
incident field. The spherical limit reduces to the sphere result. The
implementation retains only the induced far-field dipole; near an elongated
surface it is not the exact ellipsoid solution. Sampling inside its bounding
sphere uses the same flagged numerical continuation as a source. No claim is
made about an exact field around a detailed rock, root, or fish mesh.

### Surface samples and other emitters

`makeBodyReceptors` generates an oriented ellipsoid with 12 longitudinal rings
and 8 azimuthal samples by default. These 96 samples are a numerical array,
not anatomical receptor counts or measured receptor density. The normals are
available for presentation and later boundary-condition work.

`sampleReceptors` returns the extracellular baseline potential, complex object
perturbation, other-emitter signal, amplitude change, phase change, and
narrowband RMS magnitude. The baseline is referenced to infinity. Treating the
surface perturbation as a transdermal proxy omits internal-body potentials,
skin impedance, body scattering, and neural encoding. Near a baseline null,
`carrierAvailable` is false and normalized contrast is set to zero; use the
absolute voltages instead. A total-field null has no defined phase, so its
reported phase shift is also zero and `phaseAvailable` is false.

For each external wave emitter, `sampleReceptors` evaluates the field using
that emitter's own frequency and rotates it into the player's carrier frame.
Phases in `Dipole` are the phases at time zero. If callers already supply
instantaneous oscillator phases, pass `timeSeconds = 0` to avoid advancing
them twice. Other emitters are not scattered by the passive targets.

`mixSignals` computes the analytic sum directly. Two tones have envelope

```text
sqrt(A^2 + B^2 + 2 A B cos(2 pi (f2-f1) t + phase_difference))
```

The beat frequency is `abs(f2-f1)`. Equal-frequency sources have a constant
phase-dependent sum, not an automatic time-varying beat. Equal amplitudes can
cancel. For unequal amplitudes, modulation depth is `min(A,B)/max(A,B)`.
Envelope divided by `sqrt(2)` is a narrowband RMS interpretation, not the exact
finite-window RMS of arbitrary broadband or DC signals.

## Connection to the running simulation

`src/sensory_frame.hpp` supplies `evaluateSenses(simulation, forward, clock)`
without graphics or audio dependencies. Its electrical coordinate order is
`(world x, altitude, world y) / 60`, so the Y axis points upward. It uses the
current carrier frequency and `simulation.water.conductivity` in S/m. Source
moments are oriented along three-dimensional movement; a stationary neighbour
uses a deterministic heading because no measured body orientation is available.

The bridge uses 48 surface samples, at most 12 nearby passive targets, and at
most 8 nearby wave emitters. Available food is represented by 2.5–4 mm spheres.
Fish bodies use oriented spheroids. Their chosen conductivity is 0.03 S/m;
the third prey class and fish bodies use effective relative permittivity
100,000 to illustrate reactive material response. These are **uncalibrated
material assignments**, not measurements of those organisms.

Unlike the primitive library's infinity-referenced voltage, bridge perturbations
are surface-minus-body-centre differential voltages. This explicitly chosen
virtual reference removes spatially constant potential; distant external
signals then follow the dipole field's inverse-cube limit. It remains a probe
approximation, not a solution of the fish's internal conductivity and skin.
Baseline, passive-target, external-source, and total RMS voltages are exposed
separately, with no display gain. Aggregate distortion is the ratio of the
complex perturbation's spatial L2 norm to the baseline's norm. It is not a
calibrated probability of detection or an injury measure.

Targets beyond 2 m and emitters beyond 8 m are computationally culled; neither
number is an electrolocation range. Budget truncation is flagged. Objects
intersecting the source or sampled body are excluded and marked; other
small-target approximation violations are retained with a warning flag.
Large terrain cylinders are not replaced by spheres. `nearbyTerrainOmitted`
reports when such an omitted boundary lies within 1 m. Consequently the
physical array cannot supply a complete electrical image of a rock wall.

Pulse-type neighbours and catfish do not enter the wave-emitter sum. Their
passive body response can still contribute. A persistent `SensoryClock` uses
trapezoidal frequency integration between observations to keep oscillator
phases continuous through retuning; it resets with the simulation timeline.
This clock is not synchronized to audio. The simpler stateless overload is a
snapshot assuming the current frequencies were constant; its
`phaseIntegrated` flag is false.

## Presentation and ecological boundaries

An electric image is a pattern distributed over the skin. A first-person
screen is an interpretive display. Signed amplitude can drive warm/cool
contrast, phase can drive line displacement, and beat envelopes can drive
slow texture movement or sonified roughness. Palette, screen placement,
filaments, apparent object contours and darkness are artistic choices, not a
claim about fish experience. The display is driven by the demodulated
envelope, not by the 720 Hz carrier itself.

The game keeps what the player sees directly separate from nearby presence
inferred from the electric model, and keeps the electric range short even
though the map is large. A black-ghost study found centimetre-scale detection of small water fleas under
its experimental conditions; this is a **different species** and does not
calibrate brown-ghost detection thresholds. Its sensory-volume methods
illustrate how target size, water conductivity, movement, and neural criteria
affect range. [Snyder et al., 2007](https://doi.org/10.1371/journal.pbio.0050301)

The six habitat regions and their mixed populations are fictional composites.
`Eigenmannia` is identified at genus level; its representative 300–400 Hz wave
EOD can be represented by the wave-emitter model. `Gymnotus` is
pulse-type: repetition rate is not a sinusoidal carrier frequency. Experimental
work on `G. carapo` reports approximately 2 ms multiphasic pulses and a spectral
peak around 1.86 kHz, distinct from the rate at which pulses repeat. A wave-only
model cannot faithfully simulate those pulses by substituting their repetition
rate. [Rodríguez-Cattáneo et al., 2008](https://pmc.ncbi.nlm.nih.gov/articles/PMC2323572/)

The pulse-neighbour's 35–60 pulses/second is an illustrative range supported
by earlier `G. carapo` experiments. Its synthetic sonification is not a measured
discharge recording or an implemented broadband electrosensory model.
[Bennett & Grundfest, 1959](https://doi.org/10.1085/jgp.42.5.1067)

Normal free swimming keeps the player's field continuously active, uses only
upward automatic JAR shifts, and does not treat interference as health damage.
Neighbours follow simple social and foraging rules; none of them retunes to
follow the player's frequency. The test-only survey mode keeps older
challenge mechanics.

Food availability, activity cycles, schooling, territoriality, refuge seeking,
metabolic meters, current following, and survey objectives are behavioral
rules unless calibrated and independently validated. Any attention pulse,
long-lived scan outline, voluntary whole-field silence, instant broad retuning,
signal-induced health damage, or frequency-tracking pursuer is a gameplay
abstraction. Brown ghosts are weakly electric; the model does not include a
strong-electric shock weapon.

## Accuracy and approximation ledger

| Component | Implemented / verified | Missing or uncalibrated |
| --- | --- | --- |
| SI conversion and complex material response | Explicit units and admittivity | Spatially varying/anisotropic water |
| Primary electric field | Analytical 3D current dipole | Distributed organ, harmonics, body/skin boundary solver |
| Passive spherical targets | Exact uniform-incident sphere response | Incident-field gradients, dense mutual scattering |
| Oriented elongated target | Spheroidal polarizability, dipole far field | Higher multipoles and detailed surface solution |
| Receiver array | 3D surface voltages and relative changes | Measured anatomy, transdermal calibration, spiking |
| Wave interference | Coherent multi-emitter sum | Neural filtering, adaptation, detection/JAR behavior |
| Pulse-type neighbours | Must be handled separately | Pulse waveform/harmonic propagation and receptor response |
| Environment | Scene objects represented by chosen primitives | Banks, sediment, free surface, conductivity boundaries |
| Movement and ecology | Separate game-state rules | Fluid-structure dynamics, physiological energy budget |
| Sensory art | Signal-driven visual/aural interpretation | Empirical evidence for fish subjective experience |

### Gameplay parameters in physical units

The shipped configuration uses a coherent spatial scale. New habitats start in
**Observation** mode for controlled nearby inspection; **T / controller R3**
toggles **Travel Assist** for crossing the large world. Both movement settings
are authored controls, not measured brown-ghost swimming speeds. Their actual
conversions must not be mistaken for physiological calibration:

| Current parameter | Physical conversion / interpretation | Evidence status |
| --- | --- | --- |
| `body_length = 12` | 0.20 m | Model geometry |
| Observation: `swim_speed = 180`, propulsion scale `1/30` | Nominal 0.10 m/s; 0.5 model body lengths/s | Default authored inspection setting |
| Travel Assist: `swim_speed = 180`, propulsion scale `1` | Nominal 3 m/s; 15 model body lengths/s | Explicitly accelerated exploration setting |
| Travel Assist: `dash_speed = 510` | 8.5 m/s; 42.5 model body lengths/s | Authored burst mechanic; disabled in Observation |
| Feeding proximity of 48 world units | 0.8 m centre-distance rule | Convenience interaction radius, not measured prey detection |
| Free-mode automatic shift of 12 Hz | Upward tuning step | Correct direction for this species; magnitude and trigger uncalibrated |
| `day_length = 720` | One simulated day per 12 real minutes | Compressed activity cycle |
| Water 0.004–0.024 S/m | 40–240 microSiemens/cm | Chosen regional conditions |
| Energy, nourishment, fatigue, integrity | Normalized game-state quantities | Not joules, ATP, physiological reserves, or measured injury |

These nominal propulsion speeds assume full input, zero fatigue, and no quiet
modifier. Quiet swimming halves propulsion, fatigue reduces it, acceleration
takes time, and water current changes displacement relative to the world.
The Observation multiplier is applied after normalizing three-dimensional
movement intent, so diagonal swimming does not bypass it and slow intentional
movement does not accidentally become refuge rest. Returning to Observation
cancels an active burst and limits residual propulsion velocity for usability;
that transition is also a control aid, not a fitted swimming response.

Slow swimming and hovering are biologically important. A free-swimming
brown-ghost experiment reported an active group's average **wall-relative**
speed of 1.7 cm/s. This small tank study is not a species maximum-speed estimate
and does not calibrate the game's locomotion. [Fotowat, Harrison & Krahe, 2013](https://doi.org/10.1523/JNEUROSCI.0998-13.2013)

Regional temperature and oxygen are environmental scenario values. There is no
fitted temperature-to-EOD-frequency model, oxygen-dependent electric-organ
energy budget, or hydrodynamic propulsion solver. `Simulation::jam` and its
frequency window remain a heuristic game mechanic; only the `electro` routines
and explicitly defined `SensoryFrame` voltages carry the physical measurement
interpretation described above.

## Reproducible headless field laboratory

`src/field_lab.cpp` is an optional C++17 executable that exercises the electrical
library without the world simulation, graphics, assets, or an audio device.
Build it with CMake:

```sh
cmake -S . -B build-lab -DGHOSTWAVE_BUILD_GAME=OFF -DGHOSTWAVE_BUILD_FIELD_LAB=ON
cmake --build build-lab --config Release --target field_lab
./build-lab/field_lab --output-dir build-lab/experiment --seed 5489
```

For a multi-configuration Windows generator, run
`build-lab/Release/field_lab.exe` instead. A direct compiler build is also
possible, with no game dependency:

```sh
g++ -std=c++17 -O2 -Isrc src/electrosense.cpp src/field_lab.cpp -o field_lab
./field_lab --output-dir build/experiment --seed 5489
```

`--help` lists the options. The output directory is created if needed. A run
replaces only its five named output files and writes no wall-clock timestamp:

| File | Rows | Experiment |
| --- | --- | --- |
| `distance.csv` | 33 | 0.03–1.2 m, logarithmically spaced; resistive isolated-sphere return |
| `conductivity.csv` | 13 | 0.001–0.1 S/m, including the exact material-match null at 0.03 S/m |
| `material_frequency.csv` | 33 | 10–10,000 Hz material response with effective relative permittivity 100,000 |
| `interference.csv` | 2,313 | Nine frequency differences from −40 to +40 Hz; 257 envelope samples over one second |
| `manifest.json` | — | Geometry, constants, conventions, seed meaning, row counts, and limitations |

Every dimensional CSV heading carries its unit. Complex responses are provided
as real/imaginary voltages and magnitude/phase. `return_phase_available = 0`
marks an exact material-match null, where phase is undefined. The optional
unsigned 32-bit seed controls only the other emitter's initial phase through an
explicit integer recurrence. No random noise is added. Repeating the same
executable and seed produces byte-identical files; different compilers or math
libraries may round the last floating-point digits differently.

The passive experiments place a 1.5 mm sphere on the axis of a 2e-6 A m current
dipole. They read **only the secondary potential** at the source position;
they do not sample or interpret the singular primary point-source voltage
there. This deliberately simple geometry has a closed-form `r^-5` return and
is a regression experiment, not a transdermal recording or a fish's perceptual
range. A small radius-to-distance ratio does not validate the missing body,
skin, walls, or incident-field gradients.

The interference experiment instead uses two finite-distance source dipoles
and a specified electrode/reference pair. It exports the analytic **envelope**
at 256 samples/s, not an undersampled 720 Hz carrier waveform. The broad
material-frequency sweep is a mathematical parameter study; frequencies
outside the species range do not imply that a brown ghost emits them.

Each row is checked against its collinear-sphere or two-tone
envelope identity with relative tolerance `1e-11`; failure exits nonzero. When
`BUILD_TESTING` and the laboratory option are enabled, CTest also runs a seeded
export. This verifies implementation and reproducibility, not empirical
accuracy of the fish simulation.

The standalone suite `tests/electrosense_tests.cpp` checks known analytical
results, scaling, symmetries, gradient consistency, boundary current,
capacitive phase, oriented-object response, source cancellation, and numerical
guards. These tests verify the stated implementation. They **do not validate
the animal, the habitat, or a perceptual range**.

For empirical calibration, first replace the source approximation with measured
3D EOD maps or a solved body/skin model; fit source amplitude independently of
display gain; measure material spectra; then compare receptor perturbations and
behavioral detection against held-out trials. A full electroquasistatic PDE
solver would also need mesh convergence, boundary sensitivity, and multiple
scattering tests. None of those validation steps is claimed by this build.
