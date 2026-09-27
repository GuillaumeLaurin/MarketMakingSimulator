# MMS::mm provides the market-making domain logic: quoting strategies,
# inventory & risk, P&L accounting. Functional / stateless-in-time: given a
# market state produce quotes, given a fill update the position. No simulation
# loop, no time driver, no I/O; those belong to MMS::simulation and the
# application layer.
mms_add_module(mm
    PUBLIC_DEPENDS
        MMS::core
        MMS::math
)
