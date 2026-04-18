target_include_directories(app PRIVATE include)

if ((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  target_sources_ifdef(
    CONFIG_ZMK_BEHAVIOR_CHORDIS app PRIVATE
    behaviors/chordis_engine.c
    behaviors/behavior_cdis.c
    behaviors/behavior_cdis_kana.c
    behaviors/behavior_cdis_thumb.c
    behaviors/behavior_cdis_toggle.c
    behaviors/chordis_char_combo.c
    behaviors/behavior_cdis_passthrough.c
    behaviors/behavior_cdis_passthrough_word.c
  )
endif()
