#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <locale.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <threads.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>
#include "sorce.h"
#include "compgen.h"
#include "drun.h"
#include "files.h"
#include "config.h"
#include "entry.h"
#include "input.h"
#include "log.h"
#include "nelem.h"
#include "lock.h"
#include "scale.h"
#include "shm.h"
#include "string_vec.h"
#include "string_vec.h"
#include "unicode.h"
#include "viewporter.h"
#include "xmalloc.h"

#undef MAX
#undef MIN
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static const char *mime_type_text_plain = "text/plain";
static const char *mime_type_text_plain_utf8 = "text/plain;charset=utf-8";

static uint32_t gettime_ms() {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);

	uint32_t ms = t.tv_sec * 1000;
	ms += t.tv_nsec / 1000000;
	return ms;
}


/* Read all of stdin into a buffer. */
static char *read_stdin(bool normalize) {
	const size_t block_size = BUFSIZ;
	size_t num_blocks = 1;
	size_t buf_size = block_size;

	char *buf = xmalloc(buf_size);
	for (size_t block = 0; ; block++) {
		if (block == num_blocks) {
			num_blocks *= 2;
			buf = xrealloc(buf, num_blocks * block_size);
		}
		size_t bytes_read = fread(
				&buf[block * block_size],
				1,
				block_size,
				stdin);
		if (bytes_read != block_size) {
			if (!feof(stdin) && ferror(stdin)) {
				log_error("Error reading stdin.\n");
			}
			buf[block * block_size + bytes_read] = '\0';
			break;
		}
	}
	if (normalize) {
		if (utf8_validate(buf)) {
			char *tmp = utf8_normalize(buf);
			free(buf);
			buf = tmp;
		} else {
			log_error("Invalid UTF-8 in stdin.\n");
		}
	}
	return buf;
}

static void zwlr_layer_surface_configure(
		void *data,
		struct zwlr_layer_surface_v1 *zwlr_layer_surface,
		uint32_t serial,
		uint32_t width,
		uint32_t height)
{
	struct sorce *sorce = data;
	if (width == 0 || height == 0) {
		/* Compositor is deferring to us, so don't do anything. */
		log_debug("Layer surface configure with no width or height.\n");
		return;
	}
	log_debug("Layer surface configure, %u x %u.\n", width, height);

	/*
	 * Resize the main window.
	 * We want actual pixel width / height, so we have to scale the
	 * values provided by Wayland.
	 */
	if (sorce->window.fractional_scale != 0) {
		sorce->window.surface.width = scale_apply(width, sorce->window.fractional_scale);
		sorce->window.surface.height = scale_apply(height, sorce->window.fractional_scale);
	} else {
		sorce->window.surface.width = width * sorce->window.scale;
		sorce->window.surface.height = height * sorce->window.scale;
	}

	zwlr_layer_surface_v1_ack_configure(
			sorce->window.zwlr_layer_surface,
			serial);
}

static void zwlr_layer_surface_close(
		void *data,
		struct zwlr_layer_surface_v1 *zwlr_layer_surface)
{
	struct sorce *sorce = data;
	sorce->closed = true;
	log_debug("Layer surface close.\n");
}

static const struct zwlr_layer_surface_v1_listener zwlr_layer_surface_listener = {
	.configure = zwlr_layer_surface_configure,
	.closed = zwlr_layer_surface_close
};

static void wl_keyboard_keymap(
		void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t format,
		int32_t fd,
		uint32_t size)
{
	struct sorce *sorce = data;
	assert(format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1);

	char *map_shm = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	assert(map_shm != MAP_FAILED);

	if (sorce->late_keyboard_init) {
		log_debug("Delaying keyboard configuration.\n");
		sorce->xkb_keymap_string = xstrdup(map_shm);
	} else {
		log_debug("Configuring keyboard.\n");
		struct xkb_keymap *xkb_keymap = xkb_keymap_new_from_string(
				sorce->xkb_context,
				map_shm,
				XKB_KEYMAP_FORMAT_TEXT_V1,
				XKB_KEYMAP_COMPILE_NO_FLAGS);
		munmap(map_shm, size);
		close(fd);

		struct xkb_state *xkb_state = xkb_state_new(xkb_keymap);
		xkb_keymap_unref(sorce->xkb_keymap);
		xkb_state_unref(sorce->xkb_state);
		sorce->xkb_keymap = xkb_keymap;
		sorce->xkb_state = xkb_state;
		log_debug("Keyboard configured.\n");
	}
	munmap(map_shm, size);
	close(fd);
}

static void wl_keyboard_enter(
		void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t serial,
		struct wl_surface *surface,
		struct wl_array *keys)
{
	/* Deliberately left blank */
}

static void wl_keyboard_leave(
		void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t serial,
		struct wl_surface *surface)
{
	/* Deliberately left blank */
}

static void wl_keyboard_key(
		void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t serial,
		uint32_t time,
		uint32_t key,
		uint32_t state)
{
	struct sorce *sorce = data;

	/*
	 * If this wasn't a keypress (i.e. was a key release), just update key
	 * repeat info and return.
	 */
	uint32_t keycode = key + 8;
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
		if (keycode == sorce->repeat.keycode) {
			sorce->repeat.active = false;
		} else {
			sorce->repeat.next = gettime_ms() + sorce->repeat.delay;
		}
		return;
	}

	/* A rate of 0 disables key repeat */
	if (xkb_keymap_key_repeats(sorce->xkb_keymap, keycode) && sorce->repeat.rate != 0) {
		sorce->repeat.active = true;
		sorce->repeat.keycode = keycode;
		sorce->repeat.next = gettime_ms() + sorce->repeat.delay;
	}
	input_handle_keypress(sorce, keycode);
}

static void wl_keyboard_modifiers(
		void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t serial,
		uint32_t mods_depressed,
		uint32_t mods_latched,
		uint32_t mods_locked,
		uint32_t group)
{
	struct sorce *sorce = data;
	if (sorce->xkb_state == NULL) {
		return;
	}
	xkb_state_update_mask(
			sorce->xkb_state,
			mods_depressed,
			mods_latched,
			mods_locked,
			0,
			0,
			group);
}

static void wl_keyboard_repeat_info(
		void *data,
		struct wl_keyboard *wl_keyboard,
		int32_t rate,
		int32_t delay)
{
	struct sorce *sorce = data;
	sorce->repeat.rate = rate;
	sorce->repeat.delay = delay;
	if (rate > 0) {
		log_debug("Key repeat every %u ms after %u ms.\n",
				1000 / rate,
				delay);
	} else {
		log_debug("Key repeat disabled.\n");
	}
}

static const struct wl_keyboard_listener wl_keyboard_listener = {
	.keymap = wl_keyboard_keymap,
	.enter = wl_keyboard_enter,
	.leave = wl_keyboard_leave,
	.key = wl_keyboard_key,
	.modifiers = wl_keyboard_modifiers,
	.repeat_info = wl_keyboard_repeat_info,
};

static void wl_pointer_enter(
		void *data,
		struct wl_pointer *pointer,
		uint32_t serial,
		struct wl_surface *surface,
		wl_fixed_t surface_x,
		wl_fixed_t surface_y)
{
	struct sorce *sorce = data;
	if (sorce->hide_cursor) {
		/* Hide the cursor by setting its surface to NULL. */
		wl_pointer_set_cursor(sorce->wl_pointer, serial, NULL, 0, 0);
	}
}

static void wl_pointer_leave(
		void *data,
		struct wl_pointer *pointer,
		uint32_t serial,
		struct wl_surface *surface)
{
	/* Deliberately left blank */
}

static void wl_pointer_motion(
		void *data,
		struct wl_pointer *pointer,
		uint32_t time,
		wl_fixed_t surface_x,
		wl_fixed_t surface_y)
{
	/* Deliberately left blank */
}

static void wl_pointer_button(
		void *data,
		struct wl_pointer *pointer,
		uint32_t serial,
		uint32_t time,
		uint32_t button,
		enum wl_pointer_button_state state)
{
	/* Deliberately left blank */
}

static void wl_pointer_axis(
		void *data,
		struct wl_pointer *pointer,
		uint32_t time,
		enum wl_pointer_axis axis,
		wl_fixed_t value)
{
	/* Deliberately left blank */
}

static void wl_pointer_frame(void *data, struct wl_pointer *pointer)
{
	/* Deliberately left blank */
}

static void wl_pointer_axis_source(
		void *data,
		struct wl_pointer *pointer,
		enum wl_pointer_axis_source axis_source)
{
	/* Deliberately left blank */
}

static void wl_pointer_axis_stop(
		void *data,
		struct wl_pointer *pointer,
		uint32_t time,
		enum wl_pointer_axis axis)
{
	/* Deliberately left blank */
}

static void wl_pointer_axis_discrete(
		void *data,
		struct wl_pointer *pointer,
		enum wl_pointer_axis axis,
		int32_t discrete)
{
	/* Deliberately left blank */
}

static const struct wl_pointer_listener wl_pointer_listener = {
	.enter = wl_pointer_enter,
	.leave = wl_pointer_leave,
	.motion = wl_pointer_motion,
	.button = wl_pointer_button,
	.axis = wl_pointer_axis,
	.frame = wl_pointer_frame,
	.axis_source = wl_pointer_axis_source,
	.axis_stop = wl_pointer_axis_stop,
	.axis_discrete = wl_pointer_axis_discrete
};

static void wl_seat_capabilities(
		void *data,
		struct wl_seat *wl_seat,
		uint32_t capabilities)
{
	struct sorce *sorce = data;

	bool have_keyboard = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;
	bool have_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;

	if (have_keyboard && sorce->wl_keyboard == NULL) {
		sorce->wl_keyboard = wl_seat_get_keyboard(sorce->wl_seat);
		wl_keyboard_add_listener(
				sorce->wl_keyboard,
				&wl_keyboard_listener,
				sorce);
		log_debug("Got keyboard from seat.\n");
	} else if (!have_keyboard && sorce->wl_keyboard != NULL) {
		wl_keyboard_release(sorce->wl_keyboard);
		sorce->wl_keyboard = NULL;
		log_debug("Released keyboard.\n");
	}

	if (have_pointer && sorce->wl_pointer == NULL) {
		sorce->wl_pointer = wl_seat_get_pointer(sorce->wl_seat);
		wl_pointer_add_listener(
				sorce->wl_pointer,
				&wl_pointer_listener,
				sorce);
		log_debug("Got pointer from seat.\n");
	} else if (!have_pointer && sorce->wl_pointer != NULL) {
		wl_pointer_release(sorce->wl_pointer);
		sorce->wl_pointer = NULL;
		log_debug("Released pointer.\n");
	}
}

static void wl_seat_name(void *data, struct wl_seat *wl_seat, const char *name)
{
	/* Deliberately left blank */
}

static const struct wl_seat_listener wl_seat_listener = {
	.capabilities = wl_seat_capabilities,
	.name = wl_seat_name,
};

static void wl_data_offer_offer(
		void *data,
		struct wl_data_offer *wl_data_offer,
		const char *mime_type)
{
	struct clipboard *clipboard = data;

	/* Only accept plain text, and prefer utf-8. */
	if (!strcmp(mime_type, mime_type_text_plain)) {
		if (clipboard->mime_type != NULL) {
			clipboard->mime_type = mime_type_text_plain;
		}
	} else if (!strcmp(mime_type, mime_type_text_plain_utf8)) {
		clipboard->mime_type = mime_type_text_plain_utf8;
	}
}

static void wl_data_offer_source_actions(
		void *data,
		struct wl_data_offer *wl_data_offer,
		uint32_t source_actions)
{
	/* Deliberately left blank */
}

static void wl_data_offer_action(
		void *data,
		struct wl_data_offer *wl_data_offer,
		uint32_t action)
{
	/* Deliberately left blank */
}

static const struct wl_data_offer_listener wl_data_offer_listener = {
	.offer = wl_data_offer_offer,
	.source_actions = wl_data_offer_source_actions,
	.action = wl_data_offer_action
};

static void wl_data_device_data_offer(
		void *data,
		struct wl_data_device *wl_data_device,
		struct wl_data_offer *wl_data_offer)
{
	struct clipboard *clipboard = data;
	clipboard_reset(clipboard);
	clipboard->wl_data_offer = wl_data_offer;
	wl_data_offer_add_listener(
			wl_data_offer,
			&wl_data_offer_listener,
			clipboard);
}

static void wl_data_device_enter(
		void *data,
		struct wl_data_device *wl_data_device,
		uint32_t serial,
		struct wl_surface *wl_surface,
		int32_t x,
		int32_t y,
		struct wl_data_offer *wl_data_offer)
{
	/* Drag-and-drop is just ignored for now. */
	wl_data_offer_accept(
			wl_data_offer,
			serial,
			NULL);
	wl_data_offer_set_actions(
			wl_data_offer,
			WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE,
			WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE);
}

static void wl_data_device_leave(
		void *data,
		struct wl_data_device *wl_data_device)
{
	/* Deliberately left blank */
}

static void wl_data_device_motion(
		void *data,
		struct wl_data_device *wl_data_device,
		uint32_t time,
		int32_t x,
		int32_t y)
{
	/* Deliberately left blank */
}

static void wl_data_device_drop(
		void *data,
		struct wl_data_device *wl_data_device)
{
	/* Deliberately left blank */
}

static void wl_data_device_selection(
		void *data,
		struct wl_data_device *wl_data_device,
		struct wl_data_offer *wl_data_offer)
{
	struct clipboard *clipboard = data;
	if (wl_data_offer == NULL) {
		clipboard_reset(clipboard);
	}
}

static const struct wl_data_device_listener wl_data_device_listener = {
	.data_offer = wl_data_device_data_offer,
	.enter = wl_data_device_enter,
	.leave = wl_data_device_leave,
	.motion = wl_data_device_motion,
	.drop = wl_data_device_drop,
	.selection = wl_data_device_selection
};

static void output_geometry(
		void *data,
		struct wl_output *wl_output,
		int32_t x,
		int32_t y,
		int32_t physical_width,
		int32_t physical_height,
		int32_t subpixel,
		const char *make,
		const char *model,
		int32_t transform)
{
	struct sorce *sorce = data;
	struct output_list_element *el;
	wl_list_for_each(el, &sorce->output_list, link) {
		if (el->wl_output == wl_output) {
			el->transform = transform;
		}
	}
}

static void output_mode(
		void *data,
		struct wl_output *wl_output,
		uint32_t flags,
		int32_t width,
		int32_t height,
		int32_t refresh)
{
	struct sorce *sorce = data;
	struct output_list_element *el;
	wl_list_for_each(el, &sorce->output_list, link) {
		if (el->wl_output == wl_output) {
			if (flags & WL_OUTPUT_MODE_CURRENT) {
				el->width = width;
				el->height = height;
			}
		}
	}
}

static void output_scale(
		void *data,
		struct wl_output *wl_output,
		int32_t factor)
{
	struct sorce *sorce = data;
	struct output_list_element *el;
	wl_list_for_each(el, &sorce->output_list, link) {
		if (el->wl_output == wl_output) {
			el->scale = factor;
		}
	}
}

static void output_name(
		void *data,
		struct wl_output *wl_output,
		const char *name)
{
	struct sorce *sorce = data;
	struct output_list_element *el;
	wl_list_for_each(el, &sorce->output_list, link) {
		if (el->wl_output == wl_output) {
			el->name = xstrdup(name);
		}
	}
}

static void output_description(
		void *data,
		struct wl_output *wl_output,
		const char *description)
{
	/* Deliberately left blank */
}

static void output_done(void *data, struct wl_output *wl_output)
{
	log_debug("Output configuration done.\n");
}

static const struct wl_output_listener wl_output_listener = {
	.geometry = output_geometry,
	.mode = output_mode,
	.done = output_done,
	.scale = output_scale,
#ifndef NO_WL_OUTPUT_NAME
	.name = output_name,
	.description = output_description,
#endif
};

static void registry_global(
		void *data,
		struct wl_registry *wl_registry,
		uint32_t name,
		const char *interface,
		uint32_t version)
{
	struct sorce *sorce = data;
	//log_debug("Registry %u: %s v%u.\n", name, interface, version);
	if (!strcmp(interface, wl_compositor_interface.name)) {
		sorce->wl_compositor = wl_registry_bind(
				wl_registry,
				name,
				&wl_compositor_interface,
				4);
		log_debug("Bound to compositor %u.\n", name);
	} else if (!strcmp(interface, wl_seat_interface.name)) {
		sorce->wl_seat = wl_registry_bind(
				wl_registry,
				name,
				&wl_seat_interface,
				7);
		wl_seat_add_listener(
				sorce->wl_seat,
				&wl_seat_listener,
				sorce);
		log_debug("Bound to seat %u.\n", name);
	} else if (!strcmp(interface, wl_output_interface.name)) {
		struct output_list_element *el = xmalloc(sizeof(*el));
		if (version < 4) {
			el->name = xstrdup("");
			log_warning("Using an outdated compositor, "
					"output selection will not work.\n");
		} else {
			version = 4;
		}
		el->wl_output = wl_registry_bind(
				wl_registry,
				name,
				&wl_output_interface,
				version);
		wl_output_add_listener(
				el->wl_output,
				&wl_output_listener,
				sorce);
		wl_list_insert(&sorce->output_list, &el->link);
		log_debug("Bound to output %u.\n", name);
	} else if (!strcmp(interface, wl_shm_interface.name)) {
		sorce->wl_shm = wl_registry_bind(
				wl_registry,
				name,
				&wl_shm_interface,
				1);
		log_debug("Bound to shm %u.\n", name);
	} else if (!strcmp(interface, wl_data_device_manager_interface.name)) {
		sorce->wl_data_device_manager = wl_registry_bind(
				wl_registry,
				name,
				&wl_data_device_manager_interface,
				3);
		log_debug("Bound to data device manager  %u.\n", name);
	} else if (!strcmp(interface, zwlr_layer_shell_v1_interface.name)) {
		if (version < 3) {
			log_warning("Using an outdated compositor, "
					"screen anchoring may not work.\n");
		} else {
			version = 3;
		}
		sorce->zwlr_layer_shell = wl_registry_bind(
				wl_registry,
				name,
				&zwlr_layer_shell_v1_interface,
				version);
		log_debug("Bound to zwlr_layer_shell_v1 %u.\n", name);
	} else if (!strcmp(interface, wp_viewporter_interface.name)) {
		sorce->wp_viewporter = wl_registry_bind(
				wl_registry,
				name,
				&wp_viewporter_interface,
				1);
		log_debug("Bound to wp_viewporter %u.\n", name);
	} else if (!strcmp(interface, wp_fractional_scale_manager_v1_interface.name)) {
		sorce->wp_fractional_scale_manager = wl_registry_bind(
				wl_registry,
				name,
				&wp_fractional_scale_manager_v1_interface,
				1);
		log_debug("Bound to wp_fractional_scale_manager_v1 %u.\n", name);
	}
}

static void registry_global_remove(
		void *data,
		struct wl_registry *wl_registry,
		uint32_t name)
{
	/* Deliberately left blank */
}

static const struct wl_registry_listener wl_registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

static void surface_enter(
		void *data,
		struct wl_surface *wl_surface,
		struct wl_output *wl_output)
{
	log_debug("Surface entered output.\n");
}

static void surface_leave(
		void *data,
		struct wl_surface *wl_surface,
		struct wl_output *wl_output)
{
	/* Deliberately left blank */
}

static const struct wl_surface_listener wl_surface_listener = {
	.enter = surface_enter,
	.leave = surface_leave
};

/*
 * These "dummy_*" functions are callbacks just for the dummy surface used to
 * select the default output if there's more than one.
 */
static void dummy_layer_surface_configure(
		void *data,
		struct zwlr_layer_surface_v1 *zwlr_layer_surface,
		uint32_t serial,
		uint32_t width,
		uint32_t height)
{
	zwlr_layer_surface_v1_ack_configure(
			zwlr_layer_surface,
			serial);
}

static void dummy_layer_surface_close(
		void *data,
		struct zwlr_layer_surface_v1 *zwlr_layer_surface)
{
}

static const struct zwlr_layer_surface_v1_listener dummy_layer_surface_listener = {
	.configure = dummy_layer_surface_configure,
	.closed = dummy_layer_surface_close
};

static void dummy_fractional_scale_preferred_scale(
		void *data,
		struct wp_fractional_scale_v1 *wp_fractional_scale,
		uint32_t scale)
{
	struct sorce *sorce = data;
	sorce->window.fractional_scale = scale;
}

static const struct wp_fractional_scale_v1_listener dummy_fractional_scale_listener = {
	.preferred_scale = dummy_fractional_scale_preferred_scale
};

static void dummy_surface_enter(
		void *data,
		struct wl_surface *wl_surface,
		struct wl_output *wl_output)
{
	struct sorce *sorce = data;
	struct output_list_element *el;
	wl_list_for_each(el, &sorce->output_list, link) {
		if (el->wl_output == wl_output) {
			sorce->default_output = el;
			break;
		}
	}
}

static void dummy_surface_leave(
		void *data,
		struct wl_surface *wl_surface,
		struct wl_output *wl_output)
{
	/* Deliberately left blank */
}

static const struct wl_surface_listener dummy_surface_listener = {
	.enter = dummy_surface_enter,
	.leave = dummy_surface_leave
};


static void usage(bool err)
{
	fprintf(err ? stderr : stdout, "%s",
"Usage: sorce [options]\n"
"\n"
"Basic options:\n"
"  -h, --help                           Print this message and exit.\n"
"  -c, --config <path>                  Specify a config file.\n"
"      --prompt-text <string>           Prompt text.\n"
"      --width <px|%>                   Width of the window.\n"
"      --height <px|%>                  Height of the window.\n"
"      --output <name>                  Name of output to display window on.\n"
"      --anchor <position>              Location on screen to anchor window.\n"
"      --horizontal <true|false>        List results horizontally.\n"
"\n"
"All options listed in \"man 5 sorce\" are also accpted in the form \"--key=value\".\n"
	);
}

/* Option parsing with getopt. */
const struct option long_options[] = {
	{"help", no_argument, NULL, 'h'},
	{"config", required_argument, NULL, 'c'},
	{"include", required_argument, NULL, 0},
	{"anchor", required_argument, NULL, 0},
	{"exclusive-zone", required_argument, NULL, 0},
	{"background-color", required_argument, NULL, 0},
	{"corner-radius", required_argument, NULL, 0},
	{"font", required_argument, NULL, 0},
	{"font-size", required_argument, NULL, 0},
	{"font-features", required_argument, NULL, 0},
	{"font-variations", required_argument, NULL, 0},
	{"num-results", required_argument, NULL, 0},
	{"selection-color", required_argument, NULL, 0},
	{"selection-match-color", required_argument, NULL, 0},
	{"selection-padding", required_argument, NULL, 0},
	{"selection-background", required_argument, NULL, 0},
	{"selection-background-padding", required_argument, NULL, 0},
	{"selection-background-corner-radius", required_argument, NULL, 0},
	{"outline-width", required_argument, NULL, 0},
	{"outline-color", required_argument, NULL, 0},
	{"text-cursor", required_argument, NULL, 0},
	{"text-cursor-style", required_argument, NULL, 0},
	{"text-cursor-color", required_argument, NULL, 0},
	{"text-cursor-background", required_argument, NULL, 0},
	{"text-cursor-corner-radius", required_argument, NULL, 0},
	{"text-cursor-thickness", required_argument, NULL, 0},
	{"prompt-text", required_argument, NULL, 0},
	{"prompt-padding", required_argument, NULL, 0},
	{"prompt-color", required_argument, NULL, 0},
	{"prompt-background", required_argument, NULL, 0},
	{"prompt-background-padding", required_argument, NULL, 0},
	{"prompt-background-corner-radius", required_argument, NULL, 0},
	{"placeholder-text", required_argument, NULL, 0},
	{"placeholder-color", required_argument, NULL, 0},
	{"placeholder-background", required_argument, NULL, 0},
	{"placeholder-background-padding", required_argument, NULL, 0},
	{"placeholder-background-corner-radius", required_argument, NULL, 0},
	{"input-color", required_argument, NULL, 0},
	{"input-background", required_argument, NULL, 0},
	{"input-background-padding", required_argument, NULL, 0},
	{"input-background-corner-radius", required_argument, NULL, 0},
	{"default-result-color", required_argument, NULL, 0},
	{"default-result-background", required_argument, NULL, 0},
	{"default-result-background-padding", required_argument, NULL, 0},
	{"default-result-background-corner-radius", required_argument, NULL, 0},
	{"alternate-result-color", required_argument, NULL, 0},
	{"alternate-result-background", required_argument, NULL, 0},
	{"alternate-result-background-padding", required_argument, NULL, 0},
	{"alternate-result-background-corner-radius", required_argument, NULL, 0},
	{"result-spacing", required_argument, NULL, 0},
	{"min-input-width", required_argument, NULL, 0},
	{"border-width", required_argument, NULL, 0},
	{"border-color", required_argument, NULL, 0},
	{"text-color", required_argument, NULL, 0},
	{"width", required_argument, NULL, 0},
	{"height", required_argument, NULL, 0},
	{"margin-top", required_argument, NULL, 0},
	{"margin-bottom", required_argument, NULL, 0},
	{"margin-left", required_argument, NULL, 0},
	{"margin-right", required_argument, NULL, 0},
	{"padding-top", required_argument, NULL, 0},
	{"padding-bottom", required_argument, NULL, 0},
	{"padding-left", required_argument, NULL, 0},
	{"padding-right", required_argument, NULL, 0},
	{"clip-to-padding", required_argument, NULL, 0},
	{"horizontal", required_argument, NULL, 0},
	{"hide-cursor", required_argument, NULL, 0},
	{"history", required_argument, NULL, 0},
	{"history-file", required_argument, NULL, 0},
	{"fuzzy-match", required_argument, NULL, 0},
	{"matching-algorithm", required_argument, NULL, 0},
	{"require-match", required_argument, NULL, 0},
	{"auto-accept-single", required_argument, NULL, 0},
	{"print-index", required_argument, NULL, 0},
	{"hide-input", required_argument, NULL, 0},
	{"hidden-character", required_argument, NULL, 0},
	{"physical-keybindings", required_argument, NULL, 0},
	{"drun-launch", required_argument, NULL, 0},
	{"drun-print-exec", required_argument, NULL, 0},
	{"terminal", required_argument, NULL, 0},
	{"hint-font", required_argument, NULL, 0},
	{"multi-instance", required_argument, NULL, 0},
	{"ascii-input", required_argument, NULL, 0},
	{"output", required_argument, NULL, 0},
	{"scale", required_argument, NULL, 0},
	{"late-keyboard-init", optional_argument, NULL, 'k'},
	{NULL, 0, NULL, 0}
};
const char *short_options = ":hc:";

static void parse_args(struct sorce *sorce, int argc, char *argv[])
{

	bool load_default_config = true;
	int option_index = 0;

	/* Handle errors ourselves. */
	opterr = 0;

	/* First pass, just check for config file, help, and errors. */
	optind = 1;
	int opt = getopt_long(argc, argv, short_options, long_options, &option_index);
	while (opt != -1) {
		if (opt == 'h') {
			usage(false);
			exit(EXIT_SUCCESS);
		} else if (opt == 'c') {
			config_load(sorce, optarg);
			load_default_config = false;
		} else if (opt == ':') {
			log_error("Option %s requires an argument.\n", argv[optind - 1]);
			usage(true);
			exit(EXIT_FAILURE);
		} else if (opt == '?') {
			if (optopt) {
				log_error("Unknown option -%c.\n", optopt);
			} else {
				log_error("Unknown option %s.\n", argv[optind - 1]);
			}
			usage(true);
			exit(EXIT_FAILURE);
		}
		opt = getopt_long(argc, argv, short_options, long_options, &option_index);
	}
	if (load_default_config) {
		config_load(sorce, NULL);
	}

	/* Second pass, parse everything else. */
	optind = 1;
	opt = getopt_long(argc, argv, short_options, long_options, &option_index);
	while (opt != -1) {
		if (opt == 0) {
			if (!config_apply(sorce, long_options[option_index].name, optarg)) {
				exit(EXIT_FAILURE);
			}
		} else if (opt == 'k') {
			/*
			 * Backwards compatibility for --late-keyboard-init not
			 * taking an argument.
			 */
			if (optarg) {
				if (!config_apply(sorce, long_options[option_index].name, optarg)) {
					exit(EXIT_FAILURE);
				}
			} else {
				sorce->late_keyboard_init = true;
			}
		}
		opt = getopt_long(argc, argv, short_options, long_options, &option_index);
	}

	if (optind < argc) {
		log_error("Unexpected non-option argument '%s'.\n", argv[optind]);
		usage(true);
		exit(EXIT_FAILURE);
	}
}

static bool do_submit(struct sorce *sorce)
{
	struct entry *entry = &sorce->window.entry;
	uint32_t selection = entry->selection + entry->first_result;
	char *res = entry->results.buf[selection].string;

	if (sorce->window.entry.results.count == 0) {
		/* Always require a match in drun and files modes. */
		if (sorce->require_match || entry->mode == TOFI_MODE_DRUN || entry->mode == TOFI_MODE_FILES) {
			return false;
		} else {
			printf("%s\n", entry->input_utf8);
			return true;
		}
	}

	if (entry->mode == TOFI_MODE_FILES) {
		files_launch(res);
		return true;
	} else if (entry->mode == TOFI_MODE_DRUN) {
		/*
		 * At this point, the list of apps is history sorted rather
		 * than alphabetically sorted, so we can't use
		 * desktop_vec_find_sorted().
		 */
		struct desktop_entry *app = NULL;
		for (size_t i = 0; i < entry->apps.count; i++) {
			if (!strcmp(res, entry->apps.buf[i].name)) {
				app = &entry->apps.buf[i];
				break;
			}
		}
		if (app == NULL) {
			log_error("Couldn't find application file! This shouldn't happen.\n");
			return false;
		}
		char *path = app->path;
		if (sorce->drun_launch) {
			drun_launch(path);
		} else {
			drun_print(path, sorce->default_terminal);
		}
	} else {
		if (entry->mode == TOFI_MODE_PLAIN && sorce->print_index) {
			for (size_t i = 0; i < entry->commands.count; i++) {
				if (res == entry->commands.buf[i].string) {
					printf("%zu\n", i + 1);
					break;
				}
			}
		} else {
			printf("%s\n", res);
		}
	}
	if (sorce->use_history) {
		history_add(
				&entry->history,
				entry->results.buf[selection].string);
		if (sorce->history_file[0] == 0) {
			history_save_default_file(&entry->history, entry->mode == TOFI_MODE_DRUN);
		} else {
			history_save(&entry->history, sorce->history_file);
		}
	}
	return true;
}

static void read_clipboard(struct sorce *sorce)
{
	struct entry *entry = &sorce->window.entry;

	/* Make a copy of any text after the cursor. */
	uint32_t *end_text = NULL;
	size_t end_text_length = entry->input_utf32_length - entry->cursor_position;
	if (end_text_length > 0) {
		end_text = xcalloc(end_text_length, sizeof(*entry->input_utf32));
		memcpy(end_text,
				&entry->input_utf32[entry->cursor_position],
				end_text_length * sizeof(*entry->input_utf32));
	}
	/* Buffer for 4 UTF-8 bytes plus a null terminator. */
	char buffer[5];
	memset(buffer, 0, N_ELEM(buffer));
	errno = 0;
	bool eof = false;
	while (entry->cursor_position < N_ELEM(entry->input_utf32)) {
		for (size_t i = 0; i < 4; i++) {
			/*
			 * Read input 1 byte at a time. This is slow, but easy,
			 * and speed of pasting shouldn't really matter.
			 */
			int res = read(sorce->clipboard.fd, &buffer[i], 1);
			if (res == 0) {
				eof = true;
				break;
			} else if (res == -1) {
				if (errno == EAGAIN) {
					/*
					 * There was no more data to be read,
					 * but EOF hasn't been reached yet.
					 *
					 * This could mean more than a pipe's
					 * capacity (64k) of data was sent, in
					 * which case we'd potentially skip
					 * a character, but we should hit the
					 * input length limit long before that.
					 */
					input_refresh_results(sorce);
					sorce->window.surface.redraw = true;
					return;
				}
				log_error("Failed to read clipboard: %s\n", strerror(errno));
				clipboard_finish_paste(&sorce->clipboard);
				return;
			}
			uint32_t unichar = utf8_to_utf32_validate(buffer);
			if (unichar == (uint32_t)-2) {
				/* The current character isn't complete yet. */
				continue;
			} else if (unichar == (uint32_t)-1) {
				log_error("Invalid UTF-8 character in clipboard: %s\n", buffer);
				break;
			} else {
				entry->input_utf32[entry->cursor_position] = unichar;
				entry->cursor_position++;
				break;
			}
		}
		memset(buffer, 0, N_ELEM(buffer));
		if (eof) {
			break;
		}
	}
	entry->input_utf32_length = entry->cursor_position;

	/* If there was any text after the cursor, re-insert it now. */
	if (end_text != NULL) {
		for (size_t i = 0; i < end_text_length; i++) {
			if (entry->input_utf32_length == N_ELEM(entry->input_utf32)) {
				break;
			}
			entry->input_utf32[entry->input_utf32_length] = end_text[i];
			entry->input_utf32_length++;
		}
		free(end_text);
	}
	entry->input_utf32[MIN(entry->input_utf32_length, N_ELEM(entry->input_utf32) - 1)] = U'\0';

	clipboard_finish_paste(&sorce->clipboard);

	input_refresh_results(sorce);
	sorce->window.surface.redraw = true;
}

int main(int argc, char *argv[])
{
	/* Call log_debug to initialise the timers we use for perf checking. */
	log_debug("This is sorce.\n");

	/*
	 * Set the locale to the user's default, so we can deal with non-ASCII
	 * characters.
	 */
	setlocale(LC_ALL, "");

	/* Default options. */
	struct sorce sorce = {
		.window = {
			.scale = 1,
			.width = 1280,
			.height = 720,
			.exclusive_zone = -1,
			.entry = {
				.font_name = "Sans",
				.font_size = 24,
				.prompt_text = "run: ",
				.hidden_character_utf8 = u8"*",
				.padding_top = 8,
				.padding_bottom = 8,
				.padding_left = 8,
				.padding_right = 8,
				.clip_to_padding = true,
				.border_width = 12,
				.outline_width = 4,
				.background_color = {0.106f, 0.114f, 0.118f, 1.0f},
				.foreground_color = {1.0f, 1.0f, 1.0f, 1.0f},
				.border_color = {0.976f, 0.149f, 0.447f, 1.0f},
				.outline_color = {0.031f, 0.031f, 0.0f, 1.0f},
				.placeholder_theme.foreground_color = {1.0f, 1.0f, 1.0f, 0.66f},
				.placeholder_theme.foreground_specified = true,
				.selection_theme.foreground_color = {0.976f, 0.149f, 0.447f, 1.0f},
				.selection_theme.foreground_specified = true,
				.cursor_theme.thickness = 2
			}
		},
		.anchor =  ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
			| ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
			| ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
			| ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
		.use_history = true,
		.require_match = true,
		.use_scale = true,
		.physical_keybindings = true,
	};
	wl_list_init(&sorce.output_list);
	if (getenv("TERMINAL") != NULL) {
		snprintf(
				sorce.default_terminal,
				N_ELEM(sorce.default_terminal),
				"%s",
				getenv("TERMINAL"));
	}

	parse_args(&sorce, argc, argv);
	log_debug("Config done.\n");

	if (!sorce.multiple_instance && lock_check()) {
		log_error("Another instance of sorce is already running.\n");
		exit(EXIT_FAILURE);
	}

	/*
	 * Initial Wayland & XKB setup.
	 * The first thing to do is connect a listener to the global registry,
	 * so that we can bind to the various global objects and start talking
	 * to Wayland.
	 */

	log_debug("Connecting to Wayland display.\n");
	sorce.wl_display = wl_display_connect(NULL);
	if (sorce.wl_display == NULL) {
		log_error("Couldn't connect to Wayland display.\n");
		exit(EXIT_FAILURE);
	}
	sorce.wl_registry = wl_display_get_registry(sorce.wl_display);
	if (!sorce.late_keyboard_init) {
		log_debug("Creating xkb context.\n");
		sorce.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		if (sorce.xkb_context == NULL) {
			log_error("Couldn't create an XKB context.\n");
			exit(EXIT_FAILURE);
		}
	}
	wl_registry_add_listener(
			sorce.wl_registry,
			&wl_registry_listener,
			&sorce);

	/*
	 * After this first roundtrip, the only thing that should have happened
	 * is our registry_global() function being called and setting up the
	 * various global object bindings.
	 */
	log_debug("First roundtrip start.\n");
	log_indent();
	wl_display_roundtrip(sorce.wl_display);
	log_unindent();
	log_debug("First roundtrip done.\n");

	/*
	 * The next roundtrip causes the listeners we set up in
	 * registry_global() to be called. Notably, the output should be
	 * configured, telling us the scale factor and size.
	 */
	log_debug("Second roundtrip start.\n");
	log_indent();
	wl_display_roundtrip(sorce.wl_display);
	log_unindent();
	log_debug("Second roundtrip done.\n");

	{
		/*
		 * Determine the output we're going to appear on, and get its
		 * fractional scale if supported.
		 *
		 * This seems like an ugly solution, but as far as I know
		 * there's no way to determine the default output other than to
		 * call get_layer_surface with NULL as the output and see which
		 * output our surface turns up on.
		 *
		 * Additionally, determining fractional scale factors can
		 * currently only be done by attaching a wp_fractional_scale to
		 * a surface and displaying it.
		 *
		 * Here we set up a single pixel surface, perform the required
		 * two roundtrips, then tear it down. sorce.default_output
		 * should then contain the output our surface was assigned to,
		 * and sorce.window.fractional_scale should have the scale
		 * factor.
		 */
		log_debug("Determining output.\n");
		log_indent();
		struct surface surface = {
			.width = 1,
			.height = 1
		};
		surface.wl_surface =
			wl_compositor_create_surface(sorce.wl_compositor);
		wl_surface_add_listener(
				surface.wl_surface,
				&dummy_surface_listener,
				&sorce);

		struct wp_fractional_scale_v1 *wp_fractional_scale = NULL;
		if (sorce.wp_fractional_scale_manager != NULL) {
			wp_fractional_scale =
				wp_fractional_scale_manager_v1_get_fractional_scale(
						sorce.wp_fractional_scale_manager,
						surface.wl_surface);
			wp_fractional_scale_v1_add_listener(
					wp_fractional_scale,
					&dummy_fractional_scale_listener,
					&sorce);
		}

		/*
		 * If we have a desired output, make sure we appear on it so we
		 * can determine the correct fractional scale.
		 */
		struct wl_output *wl_output = NULL;
		if (sorce.target_output_name[0] != '\0') {
			struct output_list_element *el;
			wl_list_for_each(el, &sorce.output_list, link) {
				if (!strcmp(sorce.target_output_name, el->name)) {
					wl_output = el->wl_output;
					break;
				}
			}
		}

		struct zwlr_layer_surface_v1 *zwlr_layer_surface =
			zwlr_layer_shell_v1_get_layer_surface(
					sorce.zwlr_layer_shell,
					surface.wl_surface,
					wl_output,
					ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
					"dummy");
		/*
		 * Workaround for Hyprland, where if this is not set the dummy
		 * surface never enters an output for some reason.
		 */
		zwlr_layer_surface_v1_set_keyboard_interactivity(
				zwlr_layer_surface,
				ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
				);
		zwlr_layer_surface_v1_add_listener(
				zwlr_layer_surface,
				&dummy_layer_surface_listener,
				&sorce);
		zwlr_layer_surface_v1_set_size(
				zwlr_layer_surface,
				1,
				1);
		wl_surface_commit(surface.wl_surface);
		log_debug("First dummy roundtrip start.\n");
		log_indent();
		wl_display_roundtrip(sorce.wl_display);
		log_unindent();
		log_debug("First dummy roundtrip done.\n");
		log_debug("Initialising dummy surface.\n");
		log_indent();
		surface_init(&surface, sorce.wl_shm);
		surface_draw(&surface);
		log_unindent();
		log_debug("Dummy surface initialised.\n");
		log_debug("Second dummy roundtrip start.\n");
		log_indent();
		wl_display_roundtrip(sorce.wl_display);
		log_unindent();
		log_debug("Second dummy roundtrip done.\n");
		surface_destroy(&surface);
		zwlr_layer_surface_v1_destroy(zwlr_layer_surface);
		if (wp_fractional_scale != NULL) {
			wp_fractional_scale_v1_destroy(wp_fractional_scale);
		}
		wl_surface_destroy(surface.wl_surface);

		/*
		 * Walk through our output list and select the one we want if
		 * the user's asked for a specific one, otherwise just get the
		 * default one.
		 */
		bool found_target = false;
		struct output_list_element *head;
		head = wl_container_of(sorce.output_list.next, head, link);

		struct output_list_element *el;
		struct output_list_element *tmp;
		if (sorce.target_output_name[0] != 0) {
			log_debug("Looking for output %s.\n", sorce.target_output_name);
		} else if (sorce.default_output != NULL) {
			snprintf(
					sorce.target_output_name,
					N_ELEM(sorce.target_output_name),
					"%s",
					sorce.default_output->name);
			/* We don't need this anymore. */
			sorce.default_output = NULL;
		}
		wl_list_for_each_reverse_safe(el, tmp, &sorce.output_list, link) {
			if (!strcmp(sorce.target_output_name, el->name)) {
				found_target = true;
				continue;
			}
			/*
			 * If we've already found the output we're looking for
			 * or this isn't the first output in the list, remove
			 * it.
			 */
			if (found_target || el != head) {
				wl_list_remove(&el->link);
				wl_output_release(el->wl_output);
				free(el->name);
				free(el);
			}
		}

		/*
		 * The only output left should either be the one we want, or
		 * the first that was advertised.
		 */
		el = wl_container_of(sorce.output_list.next, el, link);

		/*
		 * If we're rotated 90 degrees, we need to swap width and
		 * height to calculate percentages.
		 */
		switch (el->transform) {
			case WL_OUTPUT_TRANSFORM_90:
			case WL_OUTPUT_TRANSFORM_270:
			case WL_OUTPUT_TRANSFORM_FLIPPED_90:
			case WL_OUTPUT_TRANSFORM_FLIPPED_270:
				sorce.output_width = el->height;
				sorce.output_height = el->width;
				break;
			default:
				sorce.output_width = el->width;
				sorce.output_height = el->height;
		}
		sorce.window.scale = el->scale;
		sorce.window.transform = el->transform;
		log_unindent();
		log_debug("Selected output %s.\n", el->name);
	}

	/*
	 * We can now scale values and calculate any percentages, as we know
	 * the output size and scale.
	 */
	config_fixup_values(&sorce);

	/*
	 * If we were invoked as sorce-run, generate the command list.
	 * If we were invoked as sorce-drun, generate the desktop app list.
	 * Otherwise, just read standard input.
	 */
	if (strstr(argv[0], "-run")) {
		log_debug("Generating command list.\n");
		log_indent();
		sorce.window.entry.mode = TOFI_MODE_RUN;
		sorce.window.entry.command_buffer = compgen_cached();
		struct string_ref_vec commands = string_ref_vec_from_buffer(sorce.window.entry.command_buffer);
		if (sorce.use_history) {
			if (sorce.history_file[0] == 0) {
				sorce.window.entry.history = history_load_default_file(false);
			} else {
				sorce.window.entry.history = history_load(sorce.history_file);
			}
			sorce.window.entry.commands = compgen_history_sort(&commands, &sorce.window.entry.history);
			string_ref_vec_destroy(&commands);
		} else {
			sorce.window.entry.commands = commands;
		}
		log_unindent();
		log_debug("Command list generated.\n");
	} else if (strstr(argv[0], "-files")) {
		log_debug("Generating file list.\n");
		log_indent();
		sorce.window.entry.mode = TOFI_MODE_FILES;
		sorce.window.entry.command_buffer = files_generate_cached();
		sorce.window.entry.commands = string_ref_vec_from_buffer(sorce.window.entry.command_buffer);
		log_unindent();
		if (strcmp(sorce.window.entry.prompt_text, "run: ") == 0) {
			snprintf(sorce.window.entry.prompt_text, N_ELEM(sorce.window.entry.prompt_text), "run: ");
		}
	} else if (strstr(argv[0], "-drun")) {
		log_debug("Generating desktop app list.\n");
		log_indent();
		sorce.window.entry.mode = TOFI_MODE_DRUN;
		struct desktop_vec apps = drun_generate_cached();
		if (sorce.use_history) {
			if (sorce.history_file[0] == 0) {
				sorce.window.entry.history = history_load_default_file(true);
			} else {
				sorce.window.entry.history = history_load(sorce.history_file);
			}
			drun_history_sort(&apps, &sorce.window.entry.history);
		}
		struct string_ref_vec commands = string_ref_vec_create();
		for (size_t i = 0; i < apps.count; i++) {
			string_ref_vec_add(&commands, apps.buf[i].name);
		}
		sorce.window.entry.commands = commands;
		sorce.window.entry.apps = apps;
		log_unindent();
		log_debug("App list generated.\n");
	} else {
		log_debug("Reading stdin.\n");
		char *buf = read_stdin(!sorce.ascii_input);
		sorce.window.entry.command_buffer = buf;
		sorce.window.entry.commands = string_ref_vec_from_buffer(buf);
		if (sorce.use_history) {
			if (sorce.history_file[0] == 0) {
				sorce.use_history = false;
			} else {
				sorce.window.entry.history = history_load(sorce.history_file);
				string_ref_vec_history_sort(&sorce.window.entry.commands, &sorce.window.entry.history);
			}
		}
		log_debug("Result list generated.\n");
	}
	sorce.window.entry.results = string_ref_vec_copy(&sorce.window.entry.commands);

	if (sorce.auto_accept_single && sorce.window.entry.results.count == 1) {
		log_debug("Only one result, exiting.\n");
		do_submit(&sorce);
		return EXIT_SUCCESS;
	}

	/*
	 * Next, we create the Wayland surface, which takes on the
	 * layer shell role.
	 */
	log_debug("Creating main window surface.\n");
	sorce.window.surface.wl_surface =
		wl_compositor_create_surface(sorce.wl_compositor);
	wl_surface_add_listener(
			sorce.window.surface.wl_surface,
			&wl_surface_listener,
			&sorce);
	if (sorce.window.width == 0 || sorce.window.height == 0) {
		/*
		 * Workaround for compatibility with legacy behaviour.
		 *
		 * Before the fractional_scale protocol was released, there was
		 * no way for a client to know whether a fractional scale
		 * factor had been set, meaning percentage-based dimensions
		 * were incorrect. As a workaround for full-size windows, we
		 * allowed specifying 0 for the width / height, which caused
		 * zwlr_layer_shell to tell us the correct size to use.
		 *
		 * To make fractional scaling work, we have to use
		 * wp_viewporter, and no longer need to set the buffer scale.
		 * However, viewporter doesn't allow specifying 0 for
		 * destination width or height. As a workaround, if 0 size is
		 * set, don't use viewporter, warn the user and set the buffer
		 * scale here.
		 */
		log_warning("Width or height set to 0, disabling fractional scaling support.\n");
		log_warning("If your compositor supports the fractional scale protocol, percentages are preferred.\n");
		sorce.window.fractional_scale = 0;
		wl_surface_set_buffer_scale(
				sorce.window.surface.wl_surface,
				sorce.window.scale);
	} else if (sorce.wp_viewporter == NULL) {
		/*
		 * We also could be running on a Wayland compositor which
		 * doesn't support wp_viewporter, in which case we need to use
		 * the old scaling method.
		 */
		log_warning("Using an outdated compositor, "
				"fractional scaling will not work properly.\n");
		sorce.window.fractional_scale = 0;
		wl_surface_set_buffer_scale(
				sorce.window.surface.wl_surface,
				sorce.window.scale);
	}

	/* Grab the first (and only remaining) output from our list. */
	struct wl_output *wl_output;
	{
		struct output_list_element *el;
		el = wl_container_of(sorce.output_list.next, el, link);
		wl_output = el->wl_output;
	}

	sorce.window.zwlr_layer_surface = zwlr_layer_shell_v1_get_layer_surface(
			sorce.zwlr_layer_shell,
			sorce.window.surface.wl_surface,
			wl_output,
			ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
			"launcher");
	zwlr_layer_surface_v1_set_keyboard_interactivity(
			sorce.window.zwlr_layer_surface,
			ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
	zwlr_layer_surface_v1_add_listener(
			sorce.window.zwlr_layer_surface,
			&zwlr_layer_surface_listener,
			&sorce);
	zwlr_layer_surface_v1_set_anchor(
			sorce.window.zwlr_layer_surface,
			sorce.anchor);
	zwlr_layer_surface_v1_set_exclusive_zone(
			sorce.window.zwlr_layer_surface,
			sorce.window.exclusive_zone);
	zwlr_layer_surface_v1_set_margin(
			sorce.window.zwlr_layer_surface,
			sorce.window.margin_top,
			sorce.window.margin_right,
			sorce.window.margin_bottom,
			sorce.window.margin_left);
	/*
	 * No matter whether we're scaling via Cairo or not, we're presenting a
	 * scaled buffer to Wayland, so scale the window size here if we
	 * haven't already done so.
	 */
	zwlr_layer_surface_v1_set_size(
			sorce.window.zwlr_layer_surface,
			sorce.window.width,
			sorce.window.height);

	/*
	 * Set up a viewport for our surface, necessary for fractional scaling.
	 */
	if (sorce.wp_viewporter != NULL) {
		sorce.window.wp_viewport = wp_viewporter_get_viewport(
				sorce.wp_viewporter,
				sorce.window.surface.wl_surface);
		if (sorce.window.width > 0 && sorce.window.height > 0) {
			wp_viewport_set_destination(
					sorce.window.wp_viewport,
					sorce.window.width,
					sorce.window.height);
		}
	}

	/* Commit the surface to finalise setup. */
	wl_surface_commit(sorce.window.surface.wl_surface);

	/*
	 * Create a data device and setup a listener for data offers. This is
	 * required for clipboard support.
	 */
	sorce.wl_data_device = wl_data_device_manager_get_data_device(
			sorce.wl_data_device_manager,
			sorce.wl_seat);
	wl_data_device_add_listener(
			sorce.wl_data_device,
			&wl_data_device_listener,
			&sorce.clipboard);

	/*
	 * Now that we've done all our Wayland-related setup, we do another
	 * roundtrip. This should cause the layer surface window to be
	 * configured, after which we're ready to start drawing to the screen.
	 */
	log_debug("Third roundtrip start.\n");
	log_indent();
	wl_display_roundtrip(sorce.wl_display);
	log_unindent();
	log_debug("Third roundtrip done.\n");


	/*
	 * Create the various structures for our window surface. This needs to
	 * be done before calling entry_init as that performs some initial
	 * drawing, and surface_init allocates the buffers we'll be drawing to.
	 */
	log_debug("Initialising window surface.\n");
	log_indent();
	surface_init(&sorce.window.surface, sorce.wl_shm);
	log_unindent();
	log_debug("Window surface initialised.\n");

	/*
	 * Initialise the structures for rendering the entry.
	 * Cairo needs to know the size of the surface it's creating, and
	 * there's no way to resize it aside from tearing everything down and
	 * starting again, so we make sure to do this after we've determined
	 * our output's scale factor. This stops us being able to change the
	 * scale factor after startup, but this is just a launcher, which
	 * shouldn't be moving between outputs while running.
	 */
	log_debug("Initialising renderer.\n");
	log_indent();
	{
		/*
		 * No matter how we're scaling (with fractions, integers or not
		 * at all), we pass a fractional scale factor (the numerator of
		 * a fraction with denominator 120) to our setup function for
		 * ease.
		 */
		uint32_t scale = 120;
		if (sorce.use_scale) {
			if (sorce.window.fractional_scale != 0) {
				scale = sorce.window.fractional_scale;
			} else {
				scale = sorce.window.scale * 120;
			}
		}
		entry_init(
				&sorce.window.entry,
				sorce.window.surface.shm_pool_data,
				sorce.window.surface.width,
				sorce.window.surface.height,
				scale);
	}
	log_unindent();
	log_debug("Renderer initialised.\n");

	/* Perform an initial render. */
	surface_draw(&sorce.window.surface);

	/*
	 * entry_init() left the second of the two buffers we use for
	 * double-buffering unpainted to lower startup time, as described
	 * there. Here, we flush our first, finished buffer to the screen, then
	 * copy over the image to the second buffer before we need to use it in
	 * the main loop. This ensures we paint to the screen as quickly as
	 * possible after startup.
	 */
	wl_display_roundtrip(sorce.wl_display);
	log_debug("Initialising second buffer.\n");
	memcpy(
		cairo_image_surface_get_data(sorce.window.entry.cairo[1].surface),
		cairo_image_surface_get_data(sorce.window.entry.cairo[0].surface),
		sorce.window.surface.width * sorce.window.surface.height * sizeof(uint32_t)
	);
	log_debug("Second buffer initialised.\n");

	/* We've just rendered, so we don't need to do it again right now. */
	sorce.window.surface.redraw = false;

	/* If we delayed keyboard initialisation, do it now */
	if (sorce.late_keyboard_init) {
		log_debug("Creating xkb context.\n");
		sorce.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		if (sorce.xkb_context == NULL) {
			log_error("Couldn't create an XKB context.\n");
			exit(EXIT_FAILURE);
		}
		log_debug("Configuring keyboard.\n");
		struct xkb_keymap *xkb_keymap = xkb_keymap_new_from_string(
				sorce.xkb_context,
				sorce.xkb_keymap_string,
				XKB_KEYMAP_FORMAT_TEXT_V1,
				XKB_KEYMAP_COMPILE_NO_FLAGS);

		struct xkb_state *xkb_state = xkb_state_new(xkb_keymap);
		xkb_keymap_unref(sorce.xkb_keymap);
		xkb_state_unref(sorce.xkb_state);
		sorce.xkb_keymap = xkb_keymap;
		sorce.xkb_state = xkb_state;
		free(sorce.xkb_keymap_string);
		sorce.late_keyboard_init = false;
		log_debug("Keyboard configured.\n");
	}

	/*
	 * Main event loop.
	 * See the wl_display(3) man page for an explanation of the
	 * order of the various functions called here.
	 */
	while (!sorce.closed) {
		struct pollfd pollfds[2] = {{0}, {0}};
		pollfds[0].fd = wl_display_get_fd(sorce.wl_display);

		/* Make sure we're ready to receive events on the main queue. */
		while (wl_display_prepare_read(sorce.wl_display) != 0) {
			wl_display_dispatch_pending(sorce.wl_display);
		}

		/* Make sure all our requests have been sent to the server. */
		while (wl_display_flush(sorce.wl_display) != 0) {
			pollfds[0].events = POLLOUT;
			poll(&pollfds[0], 1, -1);
		}

		/*
		 * Set time to wait for poll() to -1 (unlimited), unless
		 * there's some key repeating going on.
		 */
		int timeout = -1;
		if (sorce.repeat.active) {
			int64_t wait = (int64_t)sorce.repeat.next - (int64_t)gettime_ms();
			if (wait >= 0) {
				timeout = wait;
			} else {
				timeout = 0;
			}
		}

		pollfds[0].events = POLLIN | POLLPRI;
		int res;
		if (sorce.clipboard.fd == 0) {
			res = poll(&pollfds[0], 1, timeout);
		} else {
			/*
			 * We're trying to paste from the clipboard, which is
			 * done by reading from a pipe, so poll that file
			 * descriptor as well.
			 */
			pollfds[1].fd = sorce.clipboard.fd;
			pollfds[1].events = POLLIN | POLLPRI;
			res = poll(pollfds, 2, timeout);
		}
		if (res == 0) {
			/*
			 * No events to process and no error - we presumably
			 * have a key repeat to handle.
			 */
			wl_display_cancel_read(sorce.wl_display);
			if (sorce.repeat.active) {
				int64_t wait = (int64_t)sorce.repeat.next - (int64_t)gettime_ms();
				if (wait <= 0) {
					input_handle_keypress(&sorce, sorce.repeat.keycode);
					sorce.repeat.next += 1000 / sorce.repeat.rate;
				}
			}
		} else if (res < 0) {
			/* There was an error polling the display. */
			wl_display_cancel_read(sorce.wl_display);
		} else {
			if (pollfds[0].revents & (POLLIN | POLLPRI)) {
				/* Events to read, so put them on the queue. */
				wl_display_read_events(sorce.wl_display);
			} else {
				/*
				 * No events to read - we were woken up to
				 * handle clipboard data.
				 */
				wl_display_cancel_read(sorce.wl_display);
			}
			if (pollfds[1].revents & (POLLIN | POLLPRI)) {
				/* Read clipboard data. */
				if (sorce.clipboard.fd > 0) {
					read_clipboard(&sorce);
				}
			}
			if (pollfds[1].revents & POLLHUP) {
				/*
				 * The other end of the clipboard pipe has
				 * closed, cleanup.
				 */
				clipboard_finish_paste(&sorce.clipboard);
			}
		}

		/* Handle any events we read. */
		wl_display_dispatch_pending(sorce.wl_display);

		if (sorce.window.surface.redraw) {
			entry_update(&sorce.window.entry);
			surface_draw(&sorce.window.surface);
			sorce.window.surface.redraw = false;
		}
		if (sorce.submit) {
			sorce.submit = false;
			if (do_submit(&sorce)) {
				break;
			}
		}

	}

	log_debug("Window closed, performing cleanup.\n");
#ifdef DEBUG
	/*
	 * For debug builds, try to cleanup as much as possible, to make using
	 * e.g. Valgrind easier. There's still a few unavoidable leaks though,
	 * mostly from Pango, and Cairo holds onto quite a bit of cached data
	 * (without leaking it)
	 */
	surface_destroy(&sorce.window.surface);
	entry_destroy(&sorce.window.entry);
	if (sorce.window.wp_viewport != NULL) {
		wp_viewport_destroy(sorce.window.wp_viewport);
	}
	zwlr_layer_surface_v1_destroy(sorce.window.zwlr_layer_surface);
	wl_surface_destroy(sorce.window.surface.wl_surface);
	if (sorce.wl_keyboard != NULL) {
		wl_keyboard_release(sorce.wl_keyboard);
	}
	if (sorce.wl_pointer != NULL) {
		wl_pointer_release(sorce.wl_pointer);
	}
	wl_compositor_destroy(sorce.wl_compositor);
	if (sorce.clipboard.wl_data_offer != NULL) {
		wl_data_offer_destroy(sorce.clipboard.wl_data_offer);
	}
	wl_data_device_release(sorce.wl_data_device);
	wl_data_device_manager_destroy(sorce.wl_data_device_manager);
	wl_seat_release(sorce.wl_seat);
	{
		struct output_list_element *el;
		struct output_list_element *tmp;
		wl_list_for_each_safe(el, tmp, &sorce.output_list, link) {
			wl_list_remove(&el->link);
			wl_output_release(el->wl_output);
			free(el->name);
			free(el);
		}
	}
	wl_shm_destroy(sorce.wl_shm);
	if (sorce.wp_fractional_scale_manager != NULL) {
		wp_fractional_scale_manager_v1_destroy(sorce.wp_fractional_scale_manager);
	}
	if (sorce.wp_viewporter != NULL) {
		wp_viewporter_destroy(sorce.wp_viewporter);
	}
	zwlr_layer_shell_v1_destroy(sorce.zwlr_layer_shell);
	xkb_state_unref(sorce.xkb_state);
	xkb_keymap_unref(sorce.xkb_keymap);
	xkb_context_unref(sorce.xkb_context);
	wl_registry_destroy(sorce.wl_registry);
	if (sorce.window.entry.mode == TOFI_MODE_DRUN) {
		desktop_vec_destroy(&sorce.window.entry.apps);
	}
	if (sorce.window.entry.command_buffer != NULL) {
		free(sorce.window.entry.command_buffer);
	}
	string_ref_vec_destroy(&sorce.window.entry.commands);
	string_ref_vec_destroy(&sorce.window.entry.results);
	if (sorce.use_history) {
		history_destroy(&sorce.window.entry.history);
	}
#endif
	/*
	 * For release builds, skip straight to display disconnection and quit.
	 */
	wl_display_roundtrip(sorce.wl_display);
	wl_display_disconnect(sorce.wl_display);

	log_debug("Finished, exiting.\n");
	if (sorce.closed) {
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
