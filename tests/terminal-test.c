/*
 * Copyright © 2014 Red Hat, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <unistd.h>
#include <time.h>
#include <linux/input.h>

#include "weston-test-client-helper.h"

/* relative position where to grab the client when dragging*/
#define GRAB_SHIFT_X 50
#define GRAB_SHIFT_Y 40

static void
drag_and_check(struct client *client, int x, int y)
{
	wl_test_get_geometry(client->test->wl_test, client->surface->wl_surface);
	client_roundtrip(client);

	fprintf(stderr, "dragging from %dx%d to %dx%d\n",
		client->test->geometry.x, client->test->geometry.y,
		x, y);
	fflush(stderr);

	simulate_drag(client,
		      client->test->geometry.x + GRAB_SHIFT_X,
		      client->test->geometry.y + GRAB_SHIFT_Y,
		      x + GRAB_SHIFT_X, y + GRAB_SHIFT_Y);

	wl_test_get_geometry(client->test->wl_test, client->surface->wl_surface);
	client_roundtrip(client);

	assert(!window_is_maximized(client->toytoolkit->window));
	assert(!window_is_fullscreen(client->toytoolkit->window));
	assert(client->test->geometry.x == x);
	assert(client->test->geometry.y == y);
}

struct terminal;
struct terminal *testing_terminal_create(struct display *display);

TEST(terminal_tst)
{
	int argc;
	char *argv[] = {NULL};
	struct display *d = display_create(&argc, argv);
	assert(d);
	struct terminal *terminal = testing_terminal_create(d);
	display_run(d);
}

