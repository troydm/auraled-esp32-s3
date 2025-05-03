# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

idf_component_register(SRCS "main.c" "https.c" "ws28xx.c" PRIV_REQUIRES spi_flash 
    INCLUDE_DIRS "." EMBED_TXTFILES "web/index.html" "certs/servercert.pem" "certs/prvtkey.pem")
