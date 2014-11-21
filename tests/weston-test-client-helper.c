/*
 * Copyright © 2012 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <linux/input.h>

#include "../shared/os-compatibility.h"
#include "../clients/window.h"
#include "weston-test-client-helper.h"

int
surface_contains(struct surface *surface, int x, int y)
{
	/* test whether a global x,y point is contained in the surface */
	int sx = surface->x;
	int sy = surface->y;
	int sw = surface->width;
	int sh = surface->height;
	return x >= sx && y >= sy && x < sx + sw && y < sy + sh;
}

static void
frame_callback_handler(void *data, struct wl_callback *callback, uint32_t time)
{
	int *done = data;

	*done = 1;

	wl_callback_destroy(callback);
}

static const struct wl_callback_listener frame_listener = {
	frame_callback_handler
};

struct wl_callback *
frame_callback_set(struct wl_surface *surface, int *done)
{
	struct wl_callback *callback;

	*done = 0;
	callback = wl_surface_frame(surface);
	wl_callback_add_listener(callback, &frame_listener, done);

	return callback;
}

static int
client_dispatch(struct client *client)
{
	if (client->toytoolkit)
		return display_dispatch(client->toytoolkit->display, -1);
	else
		return wl_display_dispatch(client->wl_display);
}

int
frame_callback_wait_nofail(struct client *client, int *done)
{
	while (!*done) {
		if (client_dispatch(client) < 0)
			return 0;
	}

	return 1;
}

void
client_roundtrip(struct client *client)
{
	struct wl_callback *cb;
	int done = 0;

	if (client->toytoolkit) {
		cb = wl_display_sync(client->wl_display);
		/* use frame_listener, it does exactly what we need */
		wl_callback_add_listener(cb, &frame_listener, &done);

		while(!done)
			assert(display_dispatch(client->toytoolkit->display, -1) > 0);
	} else
		assert(wl_display_roundtrip(client->wl_display) >= 0);
}

void
move_client(struct client *client, int x, int y)
{
	struct surface *surface = client->surface;
	int done;

	client->surface->x = x;
	client->surface->y = y;
	wl_test_move_surface(client->test->wl_test, surface->wl_surface,
			     surface->x, surface->y);
	if (!client->toytoolkit) {
		wl_surface_attach(surface->wl_surface, surface->wl_buffer, 0, 0);
		wl_surface_damage(surface->wl_surface, 0, 0,
				  surface->width, surface->height);

		frame_callback_set(surface->wl_surface, &done);
		wl_surface_commit(surface->wl_surface);
		frame_callback_wait(client, &done);
	} else {
		client_roundtrip(client);
	}
}

void
pointer_simulate_move(struct client *client, int x1, int y1, int x2, int y2)
{
	struct wl_test *wl_test = client->test->wl_test;

	wl_test_move_pointer(wl_test, x1, y1);
	client_roundtrip(client);

	while (x1 != x2 || y1 != y2) {
		if (x2 < x1)
			--x1;
		else if (x2 > x1)
			++x1;

		if (y2 < y1)
			--y1;
		else if (y2 > y1)
			++y1;

		wl_test_move_pointer(wl_test, x1, y1);
		client_roundtrip(client);
	}
}

void
pointer_simulate_drag(struct client *client, int x1, int y1, int x2, int y2)
{
	struct wl_test *wl_test = client->test->wl_test;

	pointer_simulate_move(client, x1 - 50, y1 - 50, x1, y1);

	wl_test_send_button(wl_test, BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
	client_roundtrip(client);

	pointer_simulate_move(client, x1, y1, x2, y2);

	wl_test_send_button(wl_test, BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
	client_roundtrip(client);
}

#define MSEC_TO_USEC(n) ((n) * 1000)

void
pointer_click(struct client *client, uint32_t button)
{
	struct wl_test *wl_test = client->test->wl_test;

	wl_test_send_button(wl_test, button, WL_POINTER_BUTTON_STATE_PRESSED);
	wl_display_flush(client->wl_display);

	usleep(MSEC_TO_USEC(30));

	wl_test_send_button(wl_test, button, WL_POINTER_BUTTON_STATE_RELEASED);
	client_roundtrip(client);
}

int
get_n_egl_buffers(struct client *client)
{
	client->test->n_egl_buffers = -1;

	wl_test_get_n_egl_buffers(client->test->wl_test);
	wl_display_roundtrip(client->wl_display);

	return client->test->n_egl_buffers;
}

static void
store_pointer_enter(struct pointer *pointer, struct wl_surface *wl_surface,
		     wl_fixed_t x, wl_fixed_t y)
{
	pointer->focus = wl_surface;
	pointer->x = wl_fixed_to_int(x);
	pointer->y = wl_fixed_to_int(y);

	fprintf(stderr, "test-client: got pointer enter %d %d, surface %p\n",
		pointer->x, pointer->y, pointer->focus);
}

static void
pointer_handle_enter(void *data, struct wl_pointer *wl_pointer,
		     uint32_t serial, struct wl_surface *wl_surface,
		     wl_fixed_t x, wl_fixed_t y)
{
	struct pointer *pointer = data;

	store_pointer_enter(pointer, wl_surface, x, y);
}

static void
store_pointer_leave(struct pointer *pointer, struct wl_surface *wl_surface)
{
	assert(pointer->focus == wl_surface &&
		"Got leave for another wl_surface");

	pointer->focus = NULL;

	fprintf(stderr, "test-client: got pointer leave, surface %p\n",
		wl_surface);
}

static void
pointer_handle_leave(void *data, struct wl_pointer *wl_pointer,
		     uint32_t serial, struct wl_surface *wl_surface)
{
	struct pointer *pointer = data;

	store_pointer_leave(pointer, wl_surface);
}

static void
store_pointer_motion(struct pointer *pointer, wl_fixed_t x, wl_fixed_t y)
{
	pointer->x = wl_fixed_to_int(x);
	pointer->y = wl_fixed_to_int(y);

	fprintf(stderr, "test-client: got pointer motion %d %d\n",
		pointer->x, pointer->y);
}

static void
pointer_handle_motion(void *data, struct wl_pointer *wl_pointer,
		      uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
	struct pointer *pointer = data;

	store_pointer_motion(pointer, x, y);
}

static void
store_pointer_button(struct pointer *pointer, uint32_t button, uint32_t state)
{
	pointer->button = button;
	pointer->state = state;

	fprintf(stderr, "test-client: got pointer button %u %u\n",
		button, state);
}

static void
pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
		      uint32_t serial, uint32_t time, uint32_t button,
		      uint32_t state)
{
	struct pointer *pointer = data;

	store_pointer_button(pointer, button, state);
}

static void
store_pointer_axis(struct pointer *pointer, uint32_t axis, wl_fixed_t value)
{
	fprintf(stderr, "test-client: got pointer axis %u %f\n",
		axis, wl_fixed_to_double(value));
}

static void
pointer_handle_axis(void *data, struct wl_pointer *wl_pointer,
		    uint32_t time, uint32_t axis, wl_fixed_t value)
{
	store_pointer_axis((struct pointer *) data, axis, value);
}

static const struct wl_pointer_listener pointer_listener = {
	pointer_handle_enter,
	pointer_handle_leave,
	pointer_handle_motion,
	pointer_handle_button,
	pointer_handle_axis,
};

static void
keyboard_handle_keymap(void *data, struct wl_keyboard *wl_keyboard,
		       uint32_t format, int fd, uint32_t size)
{
	close(fd);

	fprintf(stderr, "test-client: got keyboard keymap\n");
}

static void
store_keyboard_enter(struct keyboard *keyboard, struct wl_surface *wl_surface)
{
	assert(keyboard->focus == NULL);
	keyboard->focus = wl_surface;

	fprintf(stderr, "test-client: got keyboard enter, surface %p\n",
		keyboard->focus);
}

static void
keyboard_handle_enter(void *data, struct wl_keyboard *wl_keyboard,
		      uint32_t serial, struct wl_surface *wl_surface,
		      struct wl_array *keys)
{
	struct keyboard *keyboard = data;

	store_keyboard_enter(keyboard, wl_surface);
}

static void
store_keyboard_leave(struct keyboard *keyboard, struct wl_surface *wl_surface)
{
	assert(keyboard->focus == wl_surface);
	keyboard->focus = NULL;

	fprintf(stderr, "test-client: got keyboard leave, surface %p\n",
		wl_surface);
}
static void
keyboard_handle_leave(void *data, struct wl_keyboard *wl_keyboard,
		      uint32_t serial, struct wl_surface *wl_surface)
{
	struct keyboard *keyboard = data;

	store_keyboard_leave(keyboard, wl_surface);
}

static void
store_keyboard_key(struct keyboard *keyboard, uint32_t key, uint32_t state)
{
	keyboard->key = key;
	keyboard->state = state;

	fprintf(stderr, "test-client: got keyboard key %u %u\n", key, state);
}

static void
keyboard_handle_key(void *data, struct wl_keyboard *wl_keyboard,
		    uint32_t serial, uint32_t time, uint32_t key,
		    uint32_t state)
{
	struct keyboard *keyboard = data;

	store_keyboard_key(keyboard, key, state);
}

static void
store_keyboard_modifiers(struct keyboard *keyboard, uint32_t mods_depressed,
			  uint32_t mods_latched, uint32_t mods_locked,
			  uint32_t group)
{
	keyboard->mods_depressed = mods_depressed;
	keyboard->mods_latched = mods_latched;
	keyboard->mods_locked = mods_locked;
	keyboard->group = group;

	fprintf(stderr, "test-client: got keyboard modifiers %x %x %x %x\n",
		mods_depressed, mods_latched, mods_locked, group);
}

static void
keyboard_handle_modifiers(void *data, struct wl_keyboard *wl_keyboard,
			  uint32_t serial, uint32_t mods_depressed,
			  uint32_t mods_latched, uint32_t mods_locked,
			  uint32_t group)
{
	struct keyboard *keyboard = data;

	store_keyboard_modifiers(keyboard, mods_depressed, mods_latched,
				 mods_locked, group);
}

static const struct wl_keyboard_listener keyboard_listener = {
	keyboard_handle_keymap,
	keyboard_handle_enter,
	keyboard_handle_leave,
	keyboard_handle_key,
	keyboard_handle_modifiers,
};

static void
store_surface_enter(struct surface *surface, struct wl_output *output)
{
	assert(surface->output == NULL);
	surface->output = output;

	fprintf(stderr, "test-client: got surface enter output %p\n",
		surface->output);
}

static void
surface_enter(void *data,
	      struct wl_surface *wl_surface, struct wl_output *output)
{
	struct surface *surface = data;

	store_surface_enter(surface, output);
}

static void
store_surface_leave(struct surface *surface, struct wl_output *output)
{
	assert(surface->output == output);
	surface->output = NULL;

	fprintf(stderr, "test-client: got surface leave output %p\n",
		output);
}

static void
surface_leave(void *data,
	      struct wl_surface *wl_surface, struct wl_output *output)
{
	struct surface *surface = data;

	store_surface_leave(surface, output);
}

static const struct wl_surface_listener surface_listener = {
	surface_enter,
	surface_leave
};

struct wl_buffer *
create_shm_buffer(struct client *client, int width, int height, void **pixels)
{
	struct wl_shm *shm = client->wl_shm;
	int stride = width * 4;
	int size = stride * height;
	struct wl_shm_pool *pool;
	struct wl_buffer *buffer;
	int fd;
	void *data;

	fd = os_create_anonymous_file(size);
	assert(fd >= 0);

	data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		close(fd);
		assert(data != MAP_FAILED);
	}

	pool = wl_shm_create_pool(shm, fd, size);
	buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
					   WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);

	close(fd);

	if (pixels)
		*pixels = data;

	return buffer;
}

static void
shm_format(void *data, struct wl_shm *wl_shm, uint32_t format)
{
	struct client *client = data;

	if (format == WL_SHM_FORMAT_ARGB8888)
		client->has_argb = 1;
}

struct wl_shm_listener wl_shm_listener = {
	shm_format
};

static void
test_handle_pointer_position(void *data, struct wl_test *wl_test,
			     wl_fixed_t x, wl_fixed_t y)
{
	struct test *test = data;
	test->pointer_x = wl_fixed_to_int(x);
	test->pointer_y = wl_fixed_to_int(y);

	fprintf(stderr, "test-client: got global pointer %d %d\n",
		test->pointer_x, test->pointer_y);
}

static void
test_handle_n_egl_buffers(void *data, struct wl_test *wl_test, uint32_t n)
{
	struct test *test = data;

	test->n_egl_buffers = n;
}

static void
test_handle_geometry(void *data, struct wl_test *wl_test,
		     struct wl_surface *surface,
		     uint32_t width, uint32_t height,
		     int32_t x, int32_t y)
{
	struct test *test = data;

	test->geometry.width = width;
	test->geometry.height = height;
	test->geometry.x = x;
	test->geometry.y = y;

	fprintf(stderr, "test-client: got geometry w: %u, h: %u, x: %d y: %d\n",
		width, height, x, y);
}

static const struct wl_test_listener test_listener = {
	test_handle_pointer_position,
	test_handle_n_egl_buffers,
	test_handle_geometry,
};

static void
seat_handle_capabilities(void *data, struct wl_seat *seat,
			 enum wl_seat_capability caps)
{
	struct input *input = data;
	struct pointer *pointer;
	struct keyboard *keyboard;

	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !input->pointer) {
		pointer = xzalloc(sizeof *pointer);
		pointer->wl_pointer = wl_seat_get_pointer(seat);
		wl_pointer_set_user_data(pointer->wl_pointer, pointer);
		wl_pointer_add_listener(pointer->wl_pointer, &pointer_listener,
					pointer);
		input->pointer = pointer;
	} else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && input->pointer) {
		wl_pointer_destroy(input->pointer->wl_pointer);
		free(input->pointer);
		input->pointer = NULL;
	}

	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !input->keyboard) {
		keyboard = xzalloc(sizeof *keyboard);
		keyboard->wl_keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_set_user_data(keyboard->wl_keyboard, keyboard);
		wl_keyboard_add_listener(keyboard->wl_keyboard, &keyboard_listener,
					 keyboard);
		input->keyboard = keyboard;
	} else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && input->keyboard) {
		wl_keyboard_destroy(input->keyboard->wl_keyboard);
		free(input->keyboard);
		input->keyboard = NULL;
	}
}

static const struct wl_seat_listener seat_listener = {
	seat_handle_capabilities,
};

static void
store_output_geometry(struct output *output, int x, int y)
{
	assert(output->wl_output);

	output->x = x;
	output->y = y;
}

static void
output_handle_geometry(void *data,
		       struct wl_output *wl_output,
		       int x, int y,
		       int physical_width,
		       int physical_height,
		       int subpixel,
		       const char *make,
		       const char *model,
		       int32_t transform)
{
	struct output *output = data;

	store_output_geometry(output, x, y);
}

static void
store_output_mode(struct output *output, uint32_t flags, int width, int height)
{
	if (flags & WL_OUTPUT_MODE_CURRENT) {
		output->width = width;
		output->height = height;
	}
}

static void
output_handle_mode(void *data,
		   struct wl_output *wl_output,
		   uint32_t flags,
		   int width,
		   int height,
		   int refresh)
{
	struct output *output = data;

	store_output_mode(output, flags, width, height);
}

static const struct wl_output_listener output_listener = {
	output_handle_geometry,
	output_handle_mode
};

static void
bind_test(struct client *client, struct wl_registry *registry, uint32_t id)
{
	struct test *test;
	assert(client->test == NULL && "Already has a wl_test");

	test = xzalloc(sizeof *test);
	if (registry) {
		test->wl_test =
			wl_registry_bind(registry, id,
					 &wl_test_interface, 1);
	} else {
		assert(client->toytoolkit);
		test->wl_test =
			display_bind(client->toytoolkit->display,
				     id, &wl_test_interface, 1);
	}

	wl_test_add_listener(test->wl_test, &test_listener, test);
	client->test = test;
}

static void
handle_global(void *data, struct wl_registry *registry,
	      uint32_t id, const char *interface, uint32_t version)
{
	struct client *client = data;
	struct input *input;
	struct output *output;
	struct global *global;

	global = xzalloc(sizeof *global);
	global->name = id;
	global->interface = strdup(interface);
	assert(interface);
	global->version = version;
	wl_list_insert(client->global_list.prev, &global->link);

	if (strcmp(interface, "wl_compositor") == 0) {
		client->wl_compositor =
			wl_registry_bind(registry, id,
					 &wl_compositor_interface, 1);
	} else if (strcmp(interface, "wl_seat") == 0) {
		input = xzalloc(sizeof *input);
		input->wl_seat =
			wl_registry_bind(registry, id,
					 &wl_seat_interface, 1);
		wl_seat_add_listener(input->wl_seat, &seat_listener, input);
		client->input = input;
	} else if (strcmp(interface, "wl_shm") == 0) {
		client->wl_shm =
			wl_registry_bind(registry, id,
					 &wl_shm_interface, 1);
		wl_shm_add_listener(client->wl_shm, &wl_shm_listener, client);
	} else if (strcmp(interface, "wl_output") == 0) {
		output = xzalloc(sizeof *output);
		output->wl_output =
			wl_registry_bind(registry, id,
					 &wl_output_interface, 1);
		wl_output_add_listener(output->wl_output,
				       &output_listener, output);
		client->output = output;
	} else if (strcmp(interface, "wl_test") == 0) {
		bind_test(client, registry, id);
	}
}

static const struct wl_registry_listener registry_listener = {
	handle_global
};

void
skip(const char *fmt, ...)
{
	va_list argp;

	va_start(argp, fmt);
	vfprintf(stderr, fmt, argp);
	va_end(argp);

	/* automake tests uses exit code 77. weston-test-runner will see
	 * this and use it, and then weston-test's sigchld handler (in the
	 * weston process) will use that as an exit status, which is what
	 * automake will see in the end. */
	exit(77);
}

void
expect_protocol_error(struct client *client,
		      const struct wl_interface *intf,
		      uint32_t code)
{
	int err;
	uint32_t errcode, failed = 0;
	const struct wl_interface *interface;
	unsigned int id;

	/* if the error has not come yet, make it happen */
	wl_display_roundtrip(client->wl_display);

	err = wl_display_get_error(client->wl_display);

	assert(err && "Expected protocol error but nothing came");
	assert(err == EPROTO && "Expected protocol error but got local error");

	errcode = wl_display_get_protocol_error(client->wl_display,
						&interface, &id);

	/* check error */
	if (errcode != code) {
		fprintf(stderr, "Should get error code %d but got %d\n",
			code, errcode);
		failed = 1;
	}

	/* this should be definitely set */
	assert(interface);

	if (strcmp(intf->name, interface->name) != 0) {
		fprintf(stderr, "Should get interface '%s' but got '%s'\n",
			intf->name, interface->name);
		failed = 1;
	}

	if (failed) {
		fprintf(stderr, "Expected other protocol error\n");
		abort();
	}

	/* all OK */
	fprintf(stderr, "Got expected protocol error on '%s' (object id: %d) "
			"with code %d\n", interface->name, id, errcode);
}

static void
log_handler(const char *fmt, va_list args)
{
	fprintf(stderr, "libwayland: ");
	vfprintf(stderr, fmt, args);
}

static void
client_check(struct client *client)
{
	assert(client);
	assert(client->wl_display);

	/* must have wl_test interface */
	assert(client->test);

	/* must have an output */
	assert(client->output);
}

struct client *
client_create(int x, int y, int width, int height)
{
	struct client *client;
	struct surface *surface;

	wl_log_set_handler_client(log_handler);

	/* connect to display */
	client = xzalloc(sizeof *client);
	client->wl_display = wl_display_connect(NULL);
	assert(client->wl_display);
	wl_list_init(&client->global_list);

	/* setup registry so we can bind to interfaces */
	client->wl_registry = wl_display_get_registry(client->wl_display);
	wl_registry_add_listener(client->wl_registry, &registry_listener, client);

	/* trigger global listener */
	wl_display_dispatch(client->wl_display);
	wl_display_roundtrip(client->wl_display);

	client_check(client);

	/* initialize the client surface */
	surface = xzalloc(sizeof *surface);
	surface->wl_surface =
		wl_compositor_create_surface(client->wl_compositor);
	assert(surface->wl_surface);

	wl_surface_add_listener(surface->wl_surface, &surface_listener,
				surface);

	client->surface = surface;
	wl_surface_set_user_data(surface->wl_surface, surface);

	surface->width = width;
	surface->height = height;

	/* must have WL_SHM_FORMAT_ARGB32 */
	assert(client->has_argb);
	surface->wl_buffer = create_shm_buffer(client, width, height,
					       &surface->data);

	memset(surface->data, 64, width * height * 4);

	move_client(client, x, y);

	return client;
}

/*
 * --- toytoolkit definitions -----------------------------------------
 */

static void
toytoolkit_key_handler(struct window *window, struct input *input,
		       uint32_t time, uint32_t key, uint32_t unicode,
		       enum wl_keyboard_key_state state, void *data)
{
	struct client *client = data;

	store_keyboard_key(client->input->keyboard, key, state);

	/* XXX modifiers? */
}

static void
toytoolkit_keyboard_focus_handler(struct window *window,
				  struct input *input, void *data)
{
	struct client *client = data;
	struct widget *widget;
	struct wl_surface *wl_surface;

	if (input) {
		widget = input_get_focus_widget(input);
		if (!widget)
			return;

		wl_surface = widget_get_wl_surface(widget);
		store_keyboard_enter(client->input->keyboard, wl_surface);
	} else {
		store_keyboard_leave(client->input->keyboard,
				     /* XXX hmm, is this right? */
				     window_get_wl_surface(window));
	}
}

static void
toytoolkit_surface_output_handler(struct window *window, struct output *output,
				  int enter, void *data)
{
	struct client *client = data;
	struct rectangle rect;

	/* if the output was not allocated when we were handling globals,
	 * do it now */
	if (client->output->width == 0 && client->output->height == 0
	    && client->output->wl_output == output_get_wl_output(output)) {
		output_get_allocation(output, &rect);
		client->output->width = rect.width;
		client->output->height = rect.height;
	}

	if (enter)
		store_surface_enter(client->surface,
				    output_get_wl_output(output));
	else
		store_surface_leave(client->surface,
				    output_get_wl_output(output));
}

static void
toytoolkit_state_changed_handler(struct window *window, void *data)
{
	struct rectangle rect;

	window_get_allocation(window, &rect);

	fprintf(stderr, "test-client: state changed - size: %dx%d %s %s %s\n",
		rect.width, rect.height,
		window_is_maximized(window) ? "maximized" : "",
		window_is_fullscreen(window) ? "fullscreen" : "",
		window_is_resizing(window) ? "resizing" : "");

	(void) data;
}

static int
toytoolkit_pointer_enter_handler(struct widget *widget, struct input *input,
				 float x, float y, void *data)
{
	struct client *client = data;

	store_pointer_enter(client->input->pointer,
			    widget_get_wl_surface(widget),
			    wl_fixed_from_double(x),
			    wl_fixed_from_double(y));
	return 0;
}

static void
toytoolkit_pointer_leave_handler(struct widget *widget, struct input *input,
				 void *data)
{
	struct client *client = data;

	store_pointer_leave(client->input->pointer,
			    widget_get_wl_surface(widget));
}

static int
toytoolkit_pointer_motion_handler(struct widget *widget, struct input *input,
				  uint32_t time, float x, float y, void *data)
{
	struct client *client = data;

	store_pointer_motion(client->input->pointer,
			     wl_fixed_from_double(x),
			     wl_fixed_from_double(y));
	return 0;
}

static void
toytoolkit_pointer_button_handler(struct widget *widget, struct input *input,
				  uint32_t time, uint32_t button,
				  enum wl_pointer_button_state state, void *data)
{

	struct client *client = data;

	store_pointer_button(client->input->pointer, button, state);
}

static void
toytoolkit_pointer_axis_handler(struct widget *widget, struct input *input,
				uint32_t time, uint32_t axis, wl_fixed_t value,
				void *data)
{
	struct client *client = data;

	store_pointer_axis(client->input->pointer, axis, value);
}

static void
toytoolkit_global_handler(struct display *display, uint32_t name,
			  const char *interface, uint32_t version, void *data)
{
	struct client *client = data;
	struct output *output;
	struct input *input;
	struct rectangle rect;

	if (strcmp(interface, "wl_test") == 0) {
		bind_test(client, NULL, name);
	} else if (strcmp(interface, "wl_compositor") == 0) {
		client->wl_compositor = display_get_compositor(display);
	} else if (strcmp(interface, "wl_output") == 0) {
		output = xzalloc(sizeof(struct output));
		client->output = output;
		output->wl_output
			= output_get_wl_output(display_get_output(display));
		output_get_allocation(display_get_output(display), &rect);
		output->width = rect.width;
		output->height = rect.height;
	} else if (strcmp(interface, "wl_seat") == 0) {
		input = display_get_input(display);

		client->input = xzalloc(sizeof(struct input));
		client->input->keyboard = xzalloc(sizeof(struct keyboard));
		client->input->pointer = xzalloc(sizeof(struct keyboard));

		client->input->wl_seat = input_get_seat(input);
		client->input->pointer->wl_pointer = input_get_wl_pointer(input);
		client->input->keyboard->wl_keyboard = input_get_wl_keyboard(input);
	}
}

static void
toytoolkit_redraw_handler(struct widget *widget, void *data)
{
	struct client *client = data;
	struct rectangle rect;

	widget_get_allocation(widget, &rect);
	client->surface->width = rect.width;
	client->surface->height = rect.height;

	/* we must add a decoration size to get the size of
	 * server allocated area */
	window_get_decoration_size(client->toytoolkit->window,
				   &rect.width, &rect.height);
	client->surface->width += rect.width;
	client->surface->height += rect.height;
}

static void
sync_surface(struct client *client);

/* keep surface in client in sync with the one in display */
static void
surface_sync_callback(void *data, struct wl_callback *callback, uint32_t time)
{
	struct client *client = data;
	struct toytoolkit *tk = client->toytoolkit;
	assert(tk);

	wl_callback_destroy(callback);

	assert(client->surface);
	assert(client->surface->wl_surface
		== window_get_wl_surface(client->toytoolkit->window));

	client->surface->wl_buffer
		= display_get_buffer_for_surface(tk->display,
						 window_get_surface(tk->window));
	assert(client->surface->wl_buffer);

	sync_surface(client);
}

static const struct wl_callback_listener frame_cb = {
	surface_sync_callback
};

static void
sync_surface(struct client *client)
{
	struct wl_callback *cb;

	cb = wl_surface_frame(window_get_wl_surface(client->toytoolkit->window));
	assert(cb);

	wl_callback_add_listener(cb, &frame_cb, client);
}

struct client *
toytoolkit_client_create(int x, int y, int width, int height)
{
	struct display *display;
	struct client *client;
	struct toytoolkit *tk;
	char *argv[] = {"test-client", NULL};
	int argc = 1;

	client = xzalloc(sizeof *client);
	wl_list_init(&client->global_list);

	tk = xzalloc(sizeof *tk);
	client->toytoolkit = tk;

	display	= display_create(&argc, argv);
	assert(display);
	display_set_user_data(display, client);

	tk->display = display;
	tk->window = window_create(display);
	tk->widget = window_frame_create(tk->window, client);
	client->wl_display = display_get_display(display);

	/* make sure all toytoolkit handlers have been dispatched now */
	wl_display_roundtrip(client->wl_display);

	/* populate client with objects */
	display_set_global_handler(display, toytoolkit_global_handler);
	client_check(client);

	window_set_title(tk->window, "toytoolkit test-client");
	window_set_user_data(tk->window, client);
	window_set_key_handler(tk->window, toytoolkit_key_handler);
	window_set_keyboard_focus_handler(tk->window,
					  toytoolkit_keyboard_focus_handler);
	window_set_output_handler(tk->window, toytoolkit_surface_output_handler);
	window_set_state_changed_handler(tk->window,
					 toytoolkit_state_changed_handler);

	widget_set_enter_handler(tk->widget, toytoolkit_pointer_enter_handler);
	widget_set_leave_handler(tk->widget, toytoolkit_pointer_leave_handler);
	widget_set_motion_handler(tk->widget, toytoolkit_pointer_motion_handler);
	widget_set_button_handler(tk->widget, toytoolkit_pointer_button_handler);
	widget_set_axis_handler(tk->widget, toytoolkit_pointer_axis_handler);
	widget_set_redraw_handler(tk->widget, toytoolkit_redraw_handler);

	/* set surface. This surface from toytoolkit will be kept in sync
	 * with client->surface, so that we can use all the tricks
	 * as before */
	client->surface = xzalloc(sizeof *client->surface);
	client->surface->wl_surface = window_get_wl_surface(tk->window);
	client->surface->width = width;
	client->surface->height = height;

	sync_surface(client);

	window_schedule_resize(tk->window, width, height);
	move_client(client, x, y);
	client_roundtrip(client);

	return client;
}
