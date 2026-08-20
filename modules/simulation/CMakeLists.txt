# MMS::simulation: the simulation engine.
#
# Owns the simulated world and drives it forward: tick loop, SimulationConfig,
# RNG wiring, adverse order flow, latency application, per-step sampling and the
# SimulationObserver / event interface that frontends subscribe to.
#
# Context-free by construction: NO command line, NO file or console I/O, NO
# rendering. Depends only on the domain modules + the STL. The "usage context"
# (headless CLI, GUI, notebook binding, ...) lives in the application layer
# (src/ at the repo root), never here.
mms_add_module(simulation
    PUBLIC_DEPENDS
        MMS::core
        MMS::math
        MMS::mms
)
