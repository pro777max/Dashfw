#!/bin/bash
BSP_FILE="managed_components/espressif__esp32_p4_function_ev_board/esp32_p4_function_ev_board.c"

if [ -f "\" ]; then
    echo "FORCE PATCHING BSP to ALWAYS use EK79007..."
    # ?????? #if CONFIG_BSP_LCD_TYPE_1024_600 ?? #if 1, ????? ??????? ?????? ???? ????????
    sed -i 's/#if CONFIG_BSP_LCD_TYPE_1024_600/#if 1 \/\/ FORCED EK79007/g' "\"
    echo "BSP patched successfully! EK79007 is now mandatory."
else
    echo "BSP file not found yet."
fi
