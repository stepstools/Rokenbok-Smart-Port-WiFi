idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver)
target_add_binary_data(${COMPONENT_TARGET} "embed/favicon.ico" BINARY)
target_add_binary_data(${COMPONENT_TARGET} "embed/bankgothicbold.ttf" BINARY)
target_add_binary_data(${COMPONENT_TARGET} "embed/index.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "embed/mobile.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "embed/help.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "embed/admin.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "embed/initindex.html" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "embed/initupdate.html" TEXT)