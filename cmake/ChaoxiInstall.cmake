include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(
    CHAOXI_INSTALL_CMAKEDIR
    "${CMAKE_INSTALL_LIBDIR}/cmake/chaoxi"
)

# ============================================================
# Install targets
# ============================================================

install(
    TARGETS
    chaoxi_base
    chaoxi_net

    EXPORT
    chaoxiTargets

    RUNTIME
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT chaoxi_Runtime

    LIBRARY
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT chaoxi_Runtime
    NAMELINK_COMPONENT chaoxi_Development

    ARCHIVE
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT chaoxi_Development

    FILE_SET HEADERS
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT chaoxi_Development
)

if(TARGET chaoxi_v2)
    install(
        TARGETS
        chaoxi_v2

        EXPORT
        chaoxiTargets

        RUNTIME
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT chaoxi_Runtime

        LIBRARY
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT chaoxi_Runtime
        NAMELINK_COMPONENT chaoxi_Development

        ARCHIVE
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT chaoxi_Development

        FILE_SET HEADERS
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT chaoxi_Development
    )
endif()

# INTERFACE aggregate target has no binary artifact,
# but it must still be part of the export set.
install(
    TARGETS
    chaoxi

    EXPORT
    chaoxiTargets

    # FILE_SET HEADERS
    # DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    # COMPONENT chaoxi_Development
)

# ============================================================
# Install headers
# ============================================================

# install(
#     DIRECTORY
#         "${PROJECT_SOURCE_DIR}/chaoxi/base"
#         "${PROJECT_SOURCE_DIR}/chaoxi/net"

#     DESTINATION
#         "${CMAKE_INSTALL_INCLUDEDIR}/chaoxi"

#     COMPONENT
#         chaoxi_Development

#     FILES_MATCHING
#         PATTERN "*.h"
#         PATTERN "*.hpp"
# )

# if(TARGET chaoxi_v2)
#     install(
#         DIRECTORY
#             "${PROJECT_SOURCE_DIR}/chaoxi/v2"

#         DESTINATION
#             "${CMAKE_INSTALL_INCLUDEDIR}/chaoxi"

#         COMPONENT
#             chaoxi_Development

#         FILES_MATCHING
#             PATTERN "*.h"
#             PATTERN "*.hpp"
#     )
# endif()

# ============================================================
# Export targets
# ============================================================

install(
    EXPORT
    chaoxiTargets

    FILE
    chaoxiTargets.cmake

    NAMESPACE
    chaoxi::

    DESTINATION
    "${CHAOXI_INSTALL_CMAKEDIR}"

    COMPONENT
    chaoxi_Development
)

# ============================================================
# Package config
# ============================================================

if(TARGET chaoxi_v2)
    set(CHAOXI_PACKAGE_HAS_V2 ON)
else()
    set(CHAOXI_PACKAGE_HAS_V2 OFF)
endif()

configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/chaoxiConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/chaoxiConfig.cmake"

    INSTALL_DESTINATION
    "${CHAOXI_INSTALL_CMAKEDIR}"
)

# For 0.x releases, minor-version compatibility is generally safer.
# After a stable 1.x API is established, SameMajorVersion is a good choice.
if(PROJECT_VERSION_MAJOR EQUAL 0)
    set(CHAOXI_VERSION_COMPATIBILITY SameMinorVersion)
else()
    set(CHAOXI_VERSION_COMPATIBILITY SameMajorVersion)
endif()

write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/chaoxiConfigVersion.cmake"

    VERSION
    "${PROJECT_VERSION}"

    COMPATIBILITY
    "${CHAOXI_VERSION_COMPATIBILITY}"
)

install(
    FILES
    "${PROJECT_BINARY_DIR}/chaoxiConfig.cmake"
    "${PROJECT_BINARY_DIR}/chaoxiConfigVersion.cmake"

    DESTINATION
    "${CHAOXI_INSTALL_CMAKEDIR}"

    COMPONENT
    chaoxi_Development
)
