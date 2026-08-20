# MMS::math: stochastic price processes, RNG, numeric helpers.
# Eigen is exposed publicly (matrix/vector types may appear in the module's API).
mms_add_module(math
    PUBLIC_DEPENDS
        Eigen3::Eigen
)
