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

#define MSEC_TO_USEC(n) ((n) * 1000)

TEST(move_client_by_pointer_test)
{
	int x, y, i;
	const int width = 300;
	const int height = 300;
	struct client *client = toytoolkit_client_create(0, 0, width, height);

	srand(time(NULL));

	/* random stuff */
	for (i = 0; i < 10; ++i) {
		x = rand() % (client->output->width - width);
		y = rand() % (client->output->height - height);

		move_client_by_dragging(client, x, y);
		/* sleep a while so that we won't do double click */
		usleep(MSEC_TO_USEC(300));
	}
}

#define GRAB_SHIFT_X 50
#define GRAB_SHIFT_Y 40

TEST(focus_tests_one_client)
{
	struct client *c1 = toytoolkit_client_create(100, 100, 300, 200);
	int dw, dh;

	/* if we'll get a motion, then we're out of frame */
	c1->input->pointer->x = -1;
	c1->input->pointer->y = -1;

	/* just for sure ... */
	wl_test_get_geometry(c1->test->wl_test, c1->surface->wl_surface);
	client_roundtrip(c1);
	assert(c1->test->geometry.x == 100);
	assert(c1->test->geometry.y == 100);

	/* move pointer away from c1 */
	pointer_simulate_move(c1, 0, 0, 50, 50);
	assert(c1->input->pointer->focus == NULL);
	assert(c1->input->keyboard->focus == NULL);

	/* move pointer to the c1's top-left corner (to bar) */
	pointer_simulate_move(c1, 50, 50,
			      100 + GRAB_SHIFT_X, 100 + GRAB_SHIFT_Y);

	/* make sure we're in frame */
	assert(c1->input->pointer->x == -1);
	assert(c1->input->pointer->y == -1);

	/* frame is not an input region ... */
	assert(c1->input->pointer->focus == NULL);
	assert(c1->input->keyboard->focus == NULL);

	pointer_click(c1, BTN_LEFT);
	client_roundtrip(c1);
	assert(c1->input->pointer->focus == NULL);
	assert(c1->input->keyboard->focus == c1->surface->wl_surface);

	/* move away from window */
	pointer_simulate_move(c1, 100, 100, 50, 50);
	assert(c1->input->pointer->focus == NULL);
	assert(c1->input->keyboard->focus == c1->surface->wl_surface);

	/* move to input region */
	window_get_decoration_size(c1->toytoolkit->window, &dw, &dh);
	pointer_simulate_move(c1, 50, 50, 110 + dw, 110 + dh);
	assert(c1->input->pointer->focus == c1->surface->wl_surface);
	assert(c1->input->keyboard->focus == c1->surface->wl_surface);
	assert(c1->input->pointer->x == dw + 10);
	assert(c1->input->pointer->y == dh + 10);

	/* move away again */
	pointer_simulate_move(c1, 110, 110, 50, 50);
	assert(c1->input->keyboard->focus == c1->surface->wl_surface);
	assert(c1->input->pointer->focus == NULL);
}

TEST(maximize_client_by_double_click)
{
	struct client *client = toytoolkit_client_create(100, 100, 300, 200);

	assert(!window_is_maximized(client->toytoolkit->window));

	/* if we'll get some motion, then we're out of frame */
	client->input->pointer->x = -1;
	client->input->pointer->y = -1;

	/* move pointer to the client's top-left corner (to bar) */
	pointer_simulate_move(client, 80, 80, 100 + GRAB_SHIFT_X,
			      100 + GRAB_SHIFT_Y);
	/* make sure we're in frame */
	assert(client->input->pointer->x == -1);
	assert(client->input->pointer->y == -1);

	/* do double click */
	pointer_click(client, BTN_LEFT);

	/* human usually moves with the pointer too */
	wl_test_move_pointer(client->test->wl_test,
		      client->test->pointer_x + 4, client->test->pointer_y + 2);
	wl_test_move_pointer(client->test->wl_test,
		      client->test->pointer_x + 5, client->test->pointer_y + 4);

	client_roundtrip(client);
	/* make sure we're still in frame */
	assert(client->input->pointer->x == -1);
	assert(client->input->pointer->y == -1);

	pointer_click(client, BTN_LEFT);

	/* we need one more roundtrip, because resize is only scheduled
	 * by the other click */
	client_roundtrip(client);

	assert(window_is_maximized(client->toytoolkit->window));
	assert(!window_is_fullscreen(client->toytoolkit->window));
	assert(!window_is_resizing(client->toytoolkit->window));
}
