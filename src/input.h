#ifndef INPUT_H
#define INPUT_H

#include <xkbcommon/xkbcommon.h>
#include "sofi.h"

void input_handle_keypress(struct sofi *sofi, xkb_keycode_t keycode);
void input_refresh_results(struct sofi *sofi);

#endif /* INPUT_H */
