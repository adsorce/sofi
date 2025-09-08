#ifndef TOFI_CONFIG_H
#define TOFI_CONFIG_H

#include <stdbool.h>
#include "sorce.h"

void config_load(struct sorce *sorce, const char *filename);
bool config_apply(struct sorce *sorce, const char *option, const char *value);
void config_fixup_values(struct sorce *sorce);

#endif /* TOFI_CONFIG_H */
