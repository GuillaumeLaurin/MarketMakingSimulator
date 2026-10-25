# mms: the command line. Arguments in, calls on MMS::application, text out. It holds no
# logic of its own. A graphical front end would be apps/gui, a sibling of this one that
# never depends on it.
include(MmsResourceAndManifest)

add_executable(mms_app main.cpp)
add_executable(MMS::app ALIAS mms_app)
set_target_properties(mms_app PROPERTIES OUTPUT_NAME mms)

target_link_libraries(mms_app
    PRIVATE
        "$<BUILD_INTERFACE:mms_common>"
        MMS::application
        CLI11::CLI11
        fmt::fmt)

mms_resource_and_manifest(mms_app NAME "Market Making Simulator" TYPE Executable)

if(MMS_INSTALL)
    install(TARGETS mms_app RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()