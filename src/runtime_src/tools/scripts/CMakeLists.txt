set(XRT_SETUP_SCRIPTS
  setup.sh
  setup.csh)

install (FILES ${XRT_SETUP_SCRIPTS} DESTINATION ${XRT_INSTALL_DIR})

set (XRT_SCRIPTS
  xrtdeps.sh
  xbrecover)

install (PROGRAMS ${XRT_SCRIPTS} DESTINATION ${XRT_INSTALL_DIR}/bin)
