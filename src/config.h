#ifndef TOFI_CONFIG_H
#define TOFI_CONFIG_H

#include <stdbool.h>
#include "sofi.h"

void config_load(struct sofi *sofi, const char *filename);
bool config_apply(struct sofi *sofi, const char *option, const char *value);
void config_fixup_values(struct sofi *sofi);

#endif /* TOFI_CONFIG_H */
