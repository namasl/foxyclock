# Hardware-independent clock logic, unit-tested on the host (see test/).
idf_component_register(SRCS "alarm.c" "buttons.c" "display.c" "imgscan.c" "ntpord.c" "sound.c" "webpass.c" "wifitest.c" "settings.c" "timetrust.c" "tz.c"
                       INCLUDE_DIRS "include"
                       REQUIRES json)
