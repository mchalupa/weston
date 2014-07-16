/*
 * Copyright (c) 2014 Red Hat, Inc.
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

#include "config.h"

#include <linux/input.h>
#include "weston-test-client-helper.h"
#include "xdg-shell-client-protocol.h"

static struct client *
setup(void)
{
	struct client *client;

	client = client_create(100, 100, 200, 200);
	assert(client);

	if (!client->xdg_shell)
		skip("Need xdg-shell for this test\n");

	wl_test_get_geometry(client->test->wl_test, client->surface->wl_surface);
	client_roundtrip(client);

	assert(client->test->geometry.x == 100);
	assert(client->test->geometry.y == 100);
	assert(client->test->geometry.width == 200);
	assert(client->test->geometry.height == 200);

	return client;
}

TEST(create_xdg_client_test)
{
	/* sort of sanity test. Create client and check if it is on
	 * the right place and has the right size */
	setup();
}

TEST(simple_maximize_test)
{
	struct client *client = setup();

	xdg_surface_set_maximized(client->surface->xdg_surface);
	client_roundtrip(client);

	wl_test_get_geometry(client->test->wl_test, client->surface->wl_surface);
	client_roundtrip(client);

	assert(client->test->geometry.x == 0);
	assert(client->test->geometry.y == 0);
	/* hmm, what should be the height? Anyway, the width should be the same
	 * as of the output */
	assert(client->test->geometry.width = client->output->width);

	xdg_surface_unset_maximized(client->surface->xdg_surface);
	client_roundtrip(client);

	wl_test_get_geometry(client->test->wl_test, client->surface->wl_surface);
	client_roundtrip(client);

	/* the old size and position should get recovered */
	assert(client->test->geometry.x == 100);
	assert(client->test->geometry.y == 100);
	assert(client->test->geometry.width == 200);
	assert(client->test->geometry.height == 200);
}

TEST(simple_fullscreen_test)
{
	struct client *client = setup();

	xdg_surface_set_fullscreen(client->surface->xdg_surface,
				   client->output->wl_output);
	client_roundtrip(client);

	wl_test_get_geometry(client->test->wl_test, client->surface->wl_surface);
	client_roundtrip(client);

	assert(client->test->geometry.x == 0);
	assert(client->test->geometry.y == 0);
	assert(client->test->geometry.width = client->output->width);
	assert(client->test->geometry.height = client->output->height);

	xdg_surface_unset_fullscreen(client->surface->xdg_surface);
	client_roundtrip(client);

	wl_test_get_geometry(client->test->wl_test, client->surface->wl_surface);
	client_roundtrip(client);

	/* the old size and position should get recovered */
	assert(client->test->geometry.x == 100);
	assert(client->test->geometry.y == 100);
	assert(client->test->geometry.width == 200);
	assert(client->test->geometry.height == 200);
}
