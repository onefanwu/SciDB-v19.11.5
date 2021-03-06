########################################
# BEGIN_COPYRIGHT
#
# Copyright (C) 2008-2019 SciDB, Inc.
# All Rights Reserved.
#
# SciDB is free software: you can redistribute it and/or modify
# it under the terms of the AFFERO GNU General Public License as published by
# the Free Software Foundation.
#
# SciDB is distributed "AS-IS" AND WITHOUT ANY WARRANTY OF ANY KIND,
# INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,
# NON-INFRINGEMENT, OR FITNESS FOR A PARTICULAR PURPOSE. See
# the AFFERO GNU General Public License for the complete license terms.
#
# You should have received a copy of the AFFERO GNU General Public License
# along with SciDB.  If not, see <http://www.gnu.org/licenses/agpl-3.0.html>
#
# END_COPYRIGHT
########################################

add_subdirectory(proto)

if (NOT WITHOUT_SERVER)
    set (network_src
        Authenticator.cpp
        NetworkManager.cpp
        MessageDesc.cpp
        BaseConnection.cpp
        Connection.cpp
        Session.cpp
        MessageHandleJob.cpp
        AuthMessageHandleJob.cpp
        ClientMessageHandleJob.cpp
        MessageUtils.cpp
        OrderedBcast.cpp
        InstanceAuthenticator.cpp
        Network.cpp
    )

    configure_file(test/mu_driver.py "${GENERAL_OUTPUT_DIRECTORY}/mu_driver.py" COPYONLY)
    configure_file(test/mu_random.py "${GENERAL_OUTPUT_DIRECTORY}/mu_random.py" COPYONLY)

    add_library(network_lib STATIC ${network_src})
    target_link_libraries(network_lib
      PUBLIC Boost::filesystem
             Boost::program_options
             Boost::regex
             Boost::serialization
             Boost::system
             ${LOG4CXX_LIBRARIES}
             rbac_lib
             catalog_lib
             scidb_msg_lib
             compression_lib
             qproc_lib
      )
endif()
