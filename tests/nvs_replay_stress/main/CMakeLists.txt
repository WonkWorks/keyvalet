idf_component_register(
    SRCS "stress.c"
         "../../../main/replay_record.c"
         "../../../main/replay_window.c"
    INCLUDE_DIRS "../../../main"
    REQUIRES nvs_flash)
