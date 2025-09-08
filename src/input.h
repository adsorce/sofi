#ifndef INPUT_H
#define INPUT_H

#include <xkbcommon/xkbcommon.h>
#include "sorce.h"

void input_handle_keypress(struct sorce *sorce, xkb_keycode_t keycode);
void input_refresh_results(struct sorce *sorce);

#endif /* INPUT_H */
