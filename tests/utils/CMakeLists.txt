install(PROGRAMS daemon.py
        # DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}
        # TODO: why is an executable going into "etc"?
        DESTINATION etc
        COMPONENT scidb-dev-tools
  )

install(PROGRAMS box_of_points.py
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT scidb-dev-tools
  )
