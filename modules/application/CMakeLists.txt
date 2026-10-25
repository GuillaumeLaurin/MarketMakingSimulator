# MMS::application: what the applications share. Where the user's files live, how a
# scenario is read, how a save is written, how a run is started. It knows no command line
# and no screen: apps/cli and, later, apps/gui are thin front ends over it.
# It needs nothing from the engine yet; dependencies are added as the code uses them.
mms_add_module(application)