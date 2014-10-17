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

#include <time.h>

#include "weston-test-client-helper.h"

/* misc tests for test-suite */

static void
check_geometry(struct client *client)
{
	wl_test_get_geometry(client->test->wl_test, client->surface->wl_surface);
	client_roundtrip(client);

	assert(client->test->geometry.x == client->surface->x);
	assert(client->test->geometry.y == client->surface->y);
	assert(client->test->geometry.height == (unsigned int) client->surface->height);
	assert(client->test->geometry.width == (unsigned int) client->surface->width);
}

static void
move_and_check(struct client *client, int x, int y)
{
	move_client(client, x, y);
	check_geometry(client);
	assert(client->test->geometry.x == x);
	assert(client->test->geometry.y == y);
}

static void
move_client_tst(struct client *client)
{
	int x, y, i;

	check_geometry(client);

	move_and_check(client, 100, 100);
	move_and_check(client, 200, 250);
	move_and_check(client, 500, 500);
	move_and_check(client, 0, 0);
	move_and_check(client, 500, 500);
	move_and_check(client, 100, 300);
	move_and_check(client, 132, 123);

	srand(time(NULL));

	assert(client->output->width > 0);
	assert(client->output->height > 0);

	/* some random fun */
	for (i = 0; i < 50; ++i) {
		x = rand() % client->output->width;
		y = rand() % client->output->height;

		move_and_check(client, x, y);
	}
}

/* first test if move_client function works */
TEST(move_client_test)
{
	struct client *client = client_create(100, 100, 200, 200);

	move_client_tst(client);
}

TEST(move_toytoolkit_client_test)
{
	struct client *client = toytoolkit_client_create(10, 10, 200, 200);

	move_client_tst(client);
}
