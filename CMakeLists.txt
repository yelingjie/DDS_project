# ESP-IDF component entry. Add this directory to EXTRA_COMPONENT_DIRS, or
# place it in your project's components/pubsub directory.
idf_component_register(
    SRCS "src/pubsub.c" "port/pubsub_port.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "port"
    REQUIRES freertos
)
