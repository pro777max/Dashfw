#!/bin/bash
BSP_FILE="managed_components/espressif__esp32_p4_function_ev_board/esp32_p4_function_ev_board.c"

if [ -f "\" ]; then
    echo "FORCE PATCHING BSP to ALWAYS use EK79007..."
    # ???????? ??? ????????? 1280_800 ?? 1024_600 ? ????? ????? BSP
    sed -i 's/CONFIG_BSP_LCD_TYPE_1280_800/CONFIG_BSP_LCD_TYPE_1024_600/g' "\"
    # ????? ???????? #if CONFIG_BSP_LCD_TYPE_1024_600 ?? #if 1 ??? ????????
    sed -i 's/#if CONFIG_BSP_LCD_TYPE_1024_600/#if 1/g' "\"
    echo "BSP patched successfully! EK79007 is now FORCED."
else
    echo "BSP file not found yet. It will be created by idf.py."
fi
