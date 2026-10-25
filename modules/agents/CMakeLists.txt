# MMS::agents: what trades in the market. The Agent interface, what an agent sees and
# what it can ask for, then the bots and the market maker agent.
# Sits between mm and simulation: it needs mm's types (prices, quantities, sides) and
# the market maker's strategy, and simulation depends on it.
mms_add_module(agents
    PUBLIC_DEPENDS
        MMS::core
        MMS::math
        MMS::mm
)
