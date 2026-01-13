/* power.h - Power management utilities
 *
 * Copyright (C) 2025  Preload-NG Team
 */

#ifndef POWER_H
#define POWER_H

#include <glib.h>

/* Returns TRUE if the system is running on battery power */
gboolean preload_on_battery(void);

#endif /* POWER_H */
