/* power.c - Power management utilities
 *
 * Copyright (C) 2025  Preload-NG Team
 */

#include "power.h"
#include "log.h"
#include <stdio.h>
#include <string.h>

gboolean preload_on_battery(void) {
    /* Check /sys/class/power_supply/BAT* /status */
    
    char path[128];
    int i;
    
    for (i = 0; i < 10; i++) {
        snprintf(path, sizeof(path), "/sys/class/power_supply/BAT%d/status", i);
        if (g_file_test(path, G_FILE_TEST_EXISTS)) {
            gchar *content = NULL;
            if (g_file_get_contents(path, &content, NULL, NULL)) {
                /* Trim whitespace */
                g_strstrip(content);
                /* Status can be "Discharging", "Charging", "Full", "Not charging", "Unknown" */
                if (g_strcmp0(content, "Discharging") == 0) {
                    g_debug("[Power] Battery %d is discharging. Power saving mode active.", i);
                    g_free(content);
                    return TRUE;
                }
                g_free(content);
            }
        }
    }
    
    return FALSE;
}
