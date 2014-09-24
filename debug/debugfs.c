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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>
#include <pthread.h>
#include <alloca.h>
#include <signal.h>

#define FUSE_USE_VERSION 26
#include <fuse.h>

#include "compositor.h"

#ifndef DEBUG
#error "This module depends on DEBUG fetures. Use --enable-debug"
#endif

/* TODO do it conditional later */
#define wdfs_log(...)					\
	do { fprintf(stderr, "[%d] ", getpid());	\
	     fprintf(stderr, __VA_ARGS__);		\
	     fputc('\n', stderr); } while(0)

static struct weston_compositor *compositor = NULL;

struct entry;
struct entry_operations {
	int (*readdir)(struct entry *, void *buf, fuse_fill_dir_t fill,
		       off_t offset, struct fuse_file_info *fi);
	int (*getattr)(struct entry *, struct stat *stbuf);
	int (*open)(struct entry *, struct fuse_file_info *fi);
	int (*read)(struct entry *, char *buf, size_t size,
			  off_t offset, struct fuse_file_info *fi);
};

struct entry {
	char *name;
	struct debug *debug;

	struct entry *parent;
	struct wl_list childs;
	struct wl_list link;
	struct wl_listener destroy_listener;

	struct entry_operations operations;

	uint32_t flags;
	struct stat stbuf;

	void *data;
};

/* operations do not have user data, must make root global var */
static struct entry *hierarchy = NULL;

struct debug {
	char *debug_dir;

	struct wl_listener compositor_destroyed;
	struct wl_event_source *sigsegv_source;

	struct fuse_chan *fuse_chan;
	struct fuse *fuse;
	pthread_t fuse_tid;

	pthread_mutex_t mutex;

	/* so that we do not have to go through the list */
	struct entry *surfaces;
	struct wl_listener surface_created;

	struct entry *seats;
	struct wl_listener seat_created;
};

static int
entry_stat(struct entry *ent, struct stat *stbuf)
{
	*stbuf = ent->stbuf;
	return 0;
}

static void
entry_add_child(struct entry *parent, struct entry *child)
{
	child->parent = parent;
	wl_list_insert(parent->childs.next, &child->link);
}

static struct entry *
entry_init(struct entry *parent, const char *name)
{
	struct entry *ent = malloc(sizeof *ent);
	if (!ent)
		return NULL;

	memset(ent, 0, sizeof *ent);
	wl_list_init(&ent->childs);

	ent->name = strdup(name);
	if (!ent->name) {
		free(ent);
		return NULL;
	}

	if (parent) {
		ent->debug = parent->debug;
		entry_add_child(parent, ent);
	}
	/* else it's a root dir */

	ent->operations.getattr = entry_stat;

	return ent;
}

static void
_dump_tree(struct entry *itm, int indent)
{
	struct entry *e;
	int i;

	for (i = 0; i < indent; ++i)
		putchar('-');
	printf("%s\n", itm->name);
	fflush(stdout);
	wl_list_for_each(e, &itm->childs, link) {
		_dump_tree(e, indent + 4);
	}
}

static void
dump_tree(struct entry *e)
{
	//pthread_mutex_lock(&hierarchy->debug->mutex);
	wdfs_log("Dumping tree:");
	_dump_tree(e, 2);
	//pthread_mutex_unlock(&hierarchy->debug->mutex);
}

static void
free_entry_unlocked(struct entry *itm)
{
	struct entry *e, *tmp;
	/* we must free all the childs of the entry */
	wl_list_for_each_safe(e, tmp, &itm->childs, link) {
		free_entry_unlocked(e);
	}
	assert(wl_list_empty(&itm->childs));

	wdfs_log("Freeing entry %s (parent %s)", itm->name, itm->parent->name);
	wl_list_remove(&itm->link);
	free(itm->name);
	/* XXX I'm having some dangling reference somewhere, so I'm getting
	 * SIGSEGV due to this following free */
	//free(itm);
}

static void
entry_set_ro_dir(struct entry *ent)
{
	ent->stbuf.st_mode = S_IFDIR | 0555;
	ent->stbuf.st_nlink = 2;
}

static int
open_ro(struct entry *ent, struct fuse_file_info *fi)
{
	(void) ent;

	if (fi->flags & O_WRONLY || fi->flags & O_RDWR ||
		!fi->flags & O_RDONLY)
		return -EACCES;

	return 0;
}

static void
entry_set_ro_file(struct entry *ent)
{
	ent->stbuf.st_mode = S_IFREG | 0444;
	ent->stbuf.st_nlink = 1;

	ent->operations.open = open_ro;
}

static inline void
entry_set_size(struct entry *ent, size_t size)
{
	ent->stbuf.st_size = size;
}

static struct entry *
add_folder(struct entry *parent, char *name)
{
	struct entry *ent = entry_init(parent, name);
	if (!ent)
		return NULL;

	entry_set_ro_dir(ent);

	wdfs_log("Adding %s to %s", name, parent ? parent->name : "root");
	/* else it's a root dir */

	return ent;
}

static struct entry *
add_file(struct entry *parent, char *name, uint32_t flags,
	 void *read_func, size_t size)
{
	struct entry *ent = entry_init(parent, name);
	if (!ent)
		return NULL;

	assert(flags == O_RDONLY && "No support for others yet");
	entry_set_ro_file(ent);
	entry_set_size(ent, size);

	ent->flags = flags;
	ent->operations.read = read_func;

	wdfs_log("  -- adding %s to %s", name, parent ? parent->name : "root");

	return ent;
}

static struct entry *
get_chld_with_name(const char *name, struct entry *ent)
{
	struct entry *e;

	wl_list_for_each(e, &ent->childs, link) {
		if (strcmp(e->name, name) == 0)
			return e;
	}

	return NULL;
}

/* parse path and get desired entry */
static struct entry *
traverse_entries(const char *path, struct entry *root)
{
	char *name, *slash;
	size_t len;
	struct entry *ent;

	assert(path);

	/* skip root / */
	if (*path == '/')
		++path;

	if (*path == 0)
		return root;

	/* find next separator */
	slash = strchr(path, '/');
	if (slash) {
		len = slash - path;
		name = alloca(len + 1);
		strncpy(name, path, len);
		name[len] = '\0';
	} else
		/* if there's no separator, this is the end of the path */
		name = (char *) path;

	ent = get_chld_with_name(name, root);
	if (!ent)
		return NULL;

	if (!slash)
		return ent;
	else
		return traverse_entries(slash, ent);
}

static inline struct entry *
find_entry(const char *path)
{
	return traverse_entries(path, hierarchy);
}

static void
entry_readdir_childs(struct entry *root, void *buf, fuse_fill_dir_t fill)
{
	struct entry *ent;

	fill(buf, ".", NULL, 0);
	fill(buf, "..", NULL, 0);

	wl_list_for_each(ent, &root->childs, link) {
		fill(buf, ent->name, NULL, 0);
	}
}

static int
debug_readdir(const char *path, void *buf, fuse_fill_dir_t fill, off_t offset,
	      struct fuse_file_info *fi)
{
	int ret = 0;
	struct entry *ent;

	pthread_mutex_lock(&hierarchy->debug->mutex);
	if (!(ent = find_entry(path))) {
		pthread_mutex_unlock(&hierarchy->debug->mutex);
		return -ENOENT;
	}

	/* this is default for each directory */
	entry_readdir_childs(ent, buf, fill);

	/* directory can have its own handle, for example for writing out
	 * the weston structures */
	if (ent->operations.readdir)
		ret = ent->operations.readdir(ent, buf, fill, offset, fi);

	pthread_mutex_unlock(&hierarchy->debug->mutex);

	return ret;
}

static int
entry_getattr(struct entry *ent, struct stat *stbuf)
{
	if(ent->operations.getattr)
		return ent->operations.getattr(ent, stbuf);
	else
		return 0;
}

static int
debug_getattr(const char *path, struct stat *stbuf)
{
	int ret = 0;
	struct entry *ent;

	memset(stbuf, 0, sizeof *stbuf);

	pthread_mutex_lock(&hierarchy->debug->mutex);
	if (!(ent = find_entry(path))) {
		pthread_mutex_unlock(&hierarchy->debug->mutex);
		return -ENOENT;
	}

	ret = entry_getattr(ent, stbuf);

	pthread_mutex_unlock(&hierarchy->debug->mutex);

	return ret;
}

static int
entry_open(struct entry *ent, struct fuse_file_info *fi)
{
	if (ent->operations.open)
		return ent->operations.open(ent, fi);
	else
		return -EACCES;
}

static int
debug_open(const char *path, struct fuse_file_info *fi)
{
	int ret = 0;
	struct entry *ent;

	pthread_mutex_lock(&hierarchy->debug->mutex);

	if (!(ent = find_entry(path))) {
		pthread_mutex_unlock(&hierarchy->debug->mutex);
		return -ENOENT;
	}

	ret = entry_open(ent, fi);

	pthread_mutex_unlock(&hierarchy->debug->mutex);

	return ret;
}

static int
entry_read(struct entry *ent, char *buf, size_t size, off_t offset,
	   struct fuse_file_info *fi)
{
	if (ent->operations.read)
		return ent->operations.read(ent, buf, size, offset, fi);
	else
		return 0;
}

static int
debug_read(const char *path, char *buf, size_t size, off_t offset,
	   struct fuse_file_info *fi)
{
	int ret = 0;
	struct entry *ent;

	pthread_mutex_lock(&hierarchy->debug->mutex);

	if (!(ent = find_entry(path))) {
		pthread_mutex_unlock(&hierarchy->debug->mutex);
		return -ENOENT;
	}

	ret = entry_read(ent, buf, size, offset, fi);

	pthread_mutex_unlock(&hierarchy->debug->mutex);

	return ret;
}

static char *
create_debug_dir(void)
{
	int ret;
	long int path_max;
	const char *xdg_runtime_dir;
	char *debug_dir;

	xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!xdg_runtime_dir)
		return NULL;

	path_max = pathconf(xdg_runtime_dir, _PC_PATH_MAX);
	if (path_max == -1)
		path_max = 1024;

	debug_dir = malloc(path_max);
	if (!debug_dir)
		return NULL;

	/* first create parent dir, if it doesn't exist */
	if ((ret = snprintf(debug_dir, path_max, "%s/weston-debugfs/",
			    xdg_runtime_dir)) >= path_max) {
		fprintf(stderr, "Debug dir name is too long (%d >= %ld)\n",
			ret, path_max);
		goto err;
	}

	if (mkdir(debug_dir, 0700) == -1) {
		if (errno != EEXIST) {
			fprintf(stderr, "mkdir '%s': %s\n",
				debug_dir, strerror(errno));
			goto err;
		}
	}

	/* now create directory with our pid */
	path_max -= ret;
	if ((ret = snprintf(debug_dir + ret, path_max, "%d",
			    getpid())) >= path_max) {
		fprintf(stderr, "Debug dir name is too long (%d >= %ld)\n",
			ret, path_max);
		goto err;
	}

	if (mkdir(debug_dir, 0700) == -1) {
		/* this dir cannot exists */
		if (errno == EEXIST) {
			fprintf(stderr, "Directory '%s' exists.\n", debug_dir);
			goto err;
		}
	}

	return debug_dir;
err:
		free(debug_dir);
		return NULL;
}

static int
weston_surface_read_geometry(struct entry *ent, char *buf, size_t size,
			     off_t offset, struct fuse_file_info *fi)
{
	char *str;
	ssize_t len;
	struct weston_surface *surf = ent->parent->data;

	(void) fi;

	if (!surf) {
		errno = -ENOENT;
		return -1;
	}

	assert(ent->stbuf.st_size);
	str = malloc(ent->stbuf.st_size);
	if (!str) {
		errno = -ENOMEM;
		return -1;
	}

	len = snprintf(str, ent->stbuf.st_size, "width: %d, height: %d\n",
		       surf->width, surf->height);
	if (len < 0 || len >= ent->stbuf.st_size) {
		free(str);
		errno = -EIO;
		return -1;
	}

	if (offset < len) {
		if (offset + size > (size_t) len)
			size = len - offset;
		memcpy(buf, str + offset, size);
	} else
		size = 0;

	free(str);

	return size;
}

static int
weston_surface_read_state(struct entry *ent, char *buf, size_t size,
			  off_t offset, struct fuse_file_info *fi)
{
	char *str;
	ssize_t len;
	struct weston_surface *surf = ent->parent->data;

	(void) fi;

	assert(ent->stbuf.st_size);
	str = malloc(ent->stbuf.st_size);
	if (!str) {
		errno = ENOMEM;
		return -1;
	}

	len = snprintf(str, ent->stbuf.st_size,
			"role name: %s\n"
			"resource: %p\n"
			"destroy signal listeners no: %d\n"
			"ref_count: %d\n"
			"touched: %d\n"
			"renderer_state: %p\n"
			"output: %p\n"
			"output_mask: %x\n"
			"frame callback num: %d\n"
			"feedback num: %d\n"
			"keep_buffer: %d\n"
			"viewport_resource: %p\n"
			"pending state:\n"
			"    newly_attached: %d\n"
			"    buffer: %p\n"
			"    sx: %d, sy: %d\n"
			/* damage, opque, input */
			"    frame callback num: %d,\n"
			"    feedback num: %d\n"
			"configure: %p\n"
			"configure_private: %p\n"
			"subsurfaces num: %d\n",
			surf->role_name,
			surf->resource,
			wl_list_length(&surf->destroy_signal.listener_list),
			surf->ref_count,
			surf->touched,
			surf->renderer_state,
			surf->output,
			surf->output_mask,
			wl_list_length(&surf->frame_callback_list),
			wl_list_length(&surf->feedback_list),
			surf->keep_buffer,
			surf->viewport_resource,
			surf->pending.newly_attached,
			surf->pending.buffer,
			surf->pending.sx, surf->pending.sy,
			wl_list_length(&surf->pending.frame_callback_list),
			wl_list_length(&surf->pending.feedback_list),
			surf->configure, surf->configure_private,
			wl_list_length(&surf->subsurface_list));

	if (len < 0 || len >= ent->stbuf.st_size) {
		free(str);
		errno = -EIO;
		return -1;
	}

	if (offset < len) {
		if (offset + size > (size_t) len)
			size = len - offset;
		memcpy(buf, str + offset, size);
	} else
		size = 0;

	free(str);

	return size;


}

static void
surface_destroyed(struct wl_listener *listener, void *data)
{
	int found = 0;
	struct entry *e, *ent;
	struct weston_surface *surf;
	struct wl_resource **res = data;

	ent = wl_container_of(listener, ent, destroy_listener);
	surf = wl_container_of(res, surf, resource);

	assert(surf);
	wdfs_log("Destroying surface %p", surf);

	pthread_mutex_lock(&ent->debug->mutex);
	wl_list_for_each(e, &ent->debug->surfaces->childs, link) {
		assert(e->data);
		if (surf == e->data) {
			found = 1;
			break;
		}
	}

	if (found) {
		dump_tree(hierarchy);
		free_entry_unlocked(e);
		dump_tree(hierarchy);
	}

	pthread_mutex_unlock(&ent->debug->mutex);

	if (!found)
		fprintf(stderr, "Got destroy signal for unkown surface!\n");
}

static void
surface_created(struct wl_listener *listener, void *data)
{
	char name[30];
	struct entry *ent;
	struct debug *debug = wl_container_of(listener,
					      debug, surface_created);
	struct weston_surface *surf = data;

	snprintf(name, sizeof name, "%p", surf);

	pthread_mutex_lock(&debug->mutex);

	ent = add_folder(debug->surfaces, name);
	if (!ent) {
		pthread_mutex_unlock(&debug->mutex);
		fprintf(stderr, "Out of memory\n");
		return;
	}

	ent->data = surf;
	ent->destroy_listener.notify = surface_destroyed;
	wl_signal_add(&surf->destroy_signal, &ent->destroy_listener);

	add_file(ent, "geometry", O_RDONLY, weston_surface_read_geometry, 100);
	add_file(ent, "state", O_RDONLY, weston_surface_read_state, 4096);
	add_folder(ent, "views");
	//add_link(ent, ent, "parent");

	pthread_mutex_unlock(&debug->mutex);
}

static inline void
init_surfaces(struct debug *debug)
{
	/* listen to the compositor's signals and keep list
	 * of surfaces as filesystem folders. Signal are added in
	 * create_hierarchy (when we have the tree prepared) */
	debug->surface_created.notify = surface_created;
	wl_signal_add(&compositor->create_surface_signal,
		      &debug->surface_created);
}

static const char *
seat_modifier_state_str(enum weston_keyboard_modifier state)
{
	static char buf[] = "ctrl alt super shift: 0000";

	buf[22] = (state & MODIFIER_CTRL)  ? '1' : '0';
	buf[23] = (state & MODIFIER_ALT)   ? '1' : '0';
	buf[24] = (state & MODIFIER_SUPER) ? '1' : '0';
	buf[25] = (state & MODIFIER_SHIFT) ? '1' : '0';

	return buf;
}

static int
weston_read_seat(struct entry *ent, char *buf, size_t size,
		 off_t offset, struct fuse_file_info *fi)
{
	char *str;
	ssize_t len;
	struct weston_seat *seat = ent->parent->data;

	(void) fi;

	if (!seat) {
		errno = -ENOENT;
		return -1;
	}

	assert(ent->stbuf.st_size);
	str = malloc(ent->stbuf.st_size);
	if (!str) {
		errno = -ENOMEM;
		return -1;
	}

	len = snprintf(str, ent->stbuf.st_size,
		       "seat name: %s\n"
		       "pointer devs count:  %d\n"
		       "keyboard devs count: %d\n"
		       "touch devs count:    %d\n"
		       "keyboard modifier state: %s\n"
		       "selection serial: %u\n"
		       "led update func: %p\n"
		       "slot map: %u\n",
		       seat->seat_name, seat->pointer_device_count,
		       seat->keyboard_device_count, seat->touch_device_count,
		       seat_modifier_state_str(seat->modifier_state),
		       seat->selection_serial, seat->led_update,
		       seat->slot_map);
	if (len < 0 || len >= ent->stbuf.st_size) {
		free(str);
		errno = -EIO;
		return -1;
	}

	if (offset < len) {
		if (offset + size > (size_t) len)
			size = len - offset;
		memcpy(buf, str + offset, size);
	} else
		size = 0;

	free(str);

	return size;

}

static int
weston_pointer_read_status(struct entry *ent, char *buf, size_t size,
			   off_t offset, struct fuse_file_info *fi)
{
	char *str;
	ssize_t len;
	struct weston_pointer *ptr = ent->data;

	(void) fi;

	if (!ptr) {
		errno = -ENOENT;
		return -1;
	}

	assert(ent->stbuf.st_size);
	str = malloc(ent->stbuf.st_size);
	if (!str) {
		errno = -ENOMEM;
		return -1;
	}

	len = snprintf(str, ent->stbuf.st_size,
		       "focus serial: %u\n"
		       "hotspot_x: %d\n"
		       "hotspot_y: %d\n"
		       "grab_x: %f\n"
		       "grab_y: %f\n"
		       "grab_button: %u\n"
		       "grab_serial: %u\n"
		       "grab_time: %u\n"
		       "x,y: %f %f\n"
		       "sx, sy: %f %f\n"
		       "button_count: %u\n",
		       ptr->focus_serial, ptr->hotspot_x, ptr->hotspot_y,
		       wl_fixed_to_double(ptr->grab_x),
		       wl_fixed_to_double(ptr->grab_y),
		       ptr->grab_button, ptr->grab_serial, ptr->grab_time,
		       wl_fixed_to_double(ptr->x), wl_fixed_to_double(ptr->y),
		       wl_fixed_to_double(ptr->sx), wl_fixed_to_double(ptr->sy),
		       ptr->button_count);
	if (len < 0 || len >= ent->stbuf.st_size) {
		free(str);
		errno = -EIO;
		return -1;
	}

	if (offset < len) {
		if (offset + size > (size_t) len)
			size = len - offset;
		memcpy(buf, str + offset, size);
	} else
		size = 0;

	free(str);

	return size;
}

static int
add_pointer(struct entry *parent, struct weston_pointer *pointer)
{
	struct entry *ent = add_file(parent, "status", O_RDONLY,
				     weston_pointer_read_status, 2000);
	if (!ent)
		return 0;

	// add_link(parent, focus);
	return 1;
}

static void
seat_created(struct wl_listener *listener, void *data)
{
	struct entry *ent, *tmp;
	struct debug *debug = wl_container_of(listener,
					      debug, seat_created);
	struct weston_seat *seat = data;

	assert(debug->seats);

	ent = add_folder(debug->seats, seat->seat_name);
	if (!ent) {
		/* XXX abort() */
		fprintf(stderr, "Failed adding seat due to lack of memory\n");
		return;
	}

	ent->data = seat;
	tmp = add_file(ent, "seat", O_RDONLY, weston_read_seat, 1024);
	if (!tmp) {
		fprintf(stderr, "Failed adding seat due to lack of memory\n");
		return;
	}

	if (seat->pointer_device_count > 0) {
		tmp = add_folder(ent, "pointer");
		if (!tmp) {
			fprintf(stderr, "Failed adding pointer, no memory\n");
			return;
		}

		tmp->data = seat->pointer;
		if (!add_pointer(tmp, seat->pointer)) {
			fprintf(stderr, "Failed adding pointer\n");
			return;
		}
	}

	if (seat->keyboard_device_count > 0) {
		tmp = add_folder(ent, "keyboard");
		if (!tmp) {
			fprintf(stderr, "Failed adding keyboard, no memory\n");
			return;
		}
		tmp->data = seat->keyboard;
	}

	if (seat->touch_device_count > 0) {
		tmp = add_folder(ent, "touch");
		if (!tmp) {
			fprintf(stderr, "Failed adding touch, no memory\n");
			return;
		}
		tmp->data = seat->touch;
	}
}

static inline void
init_seats(struct debug *debug)
{
	struct weston_seat *seat;

	debug->seat_created.notify = seat_created;
	wl_signal_add(&compositor->seat_created_signal,
		      &debug->seat_created);

	wl_list_for_each(seat, &compositor->seat_list, link)
		seat_created(&debug->seat_created, seat);
}

static int
create_hierarchy(struct debug *debug)
{
	struct entry *ent;

	hierarchy = add_folder(NULL, "/");
	if (!hierarchy)
		return -1;
	/* we must set this manually, since we have no parent
	 * to get it from */
	hierarchy->debug = debug;

	ent = add_folder(hierarchy, "surfaces");
	if (!ent)
		goto err;
	debug->surfaces = ent;
	init_surfaces(debug);

	ent = add_folder(hierarchy, "xdg_surfaces");
	if (!ent)
		goto err;

	ent = add_folder(hierarchy, "outputs");
	if (!ent)
		goto err;

	ent = add_folder(hierarchy, "seats");
	if (!ent)
		goto err;

	debug->seats = ent;
	init_seats(debug);

	ent = add_folder(hierarchy, "globals");
	if (!ent)
		goto err;

	ent = add_file(hierarchy, "backend", O_RDONLY, NULL, 0);
	if (!ent)
		goto err;

	return 0;

err:
	/* we do not need to use locks, fuse is not running yet */
	free_entry_unlocked(hierarchy);
	return -1;
}

static struct fuse_operations operations = {
	.getattr = debug_getattr,
	.readdir = debug_readdir,
	.open = debug_open,
	.read = debug_read,
};

static void
debug_destroy(struct debug *debug)
{
	if (debug->sigsegv_source)
		wl_event_source_remove(debug->sigsegv_source);

	if (debug->fuse)
		fuse_exit(debug->fuse);

	if (debug->fuse_chan) {
		assert(debug->debug_dir);
		fuse_unmount(debug->debug_dir, debug->fuse_chan);
	}

	if (debug->fuse)
		fuse_destroy(debug->fuse);

	if (debug->debug_dir) {
		if (rmdir(debug->debug_dir) < 0)
			/* what we can do but write the error out? */
			fprintf(stderr, "Removing directory %s: %s",
				debug->debug_dir, strerror(errno));

		free(debug->debug_dir);
	}

	pthread_mutex_destroy(&debug->mutex);
	free(debug);
}

static int
handle_sigsegv(int signum, void *data)
{
	struct debug *debug = data;

	debug_destroy(debug);
	return 0;
}

static void
cleanup(struct wl_listener *listener, void *data)
{
	struct debug *debug = wl_container_of(listener,
					      debug, compositor_destroyed);
	debug_destroy(debug);
}

/*
 * this way we have better control over the fuse than simply using
 * fuse_main
 */
static int
fuse_init(struct debug *debug)
{
	struct fuse_args fuse_args = FUSE_ARGS_INIT(0, NULL);

	/* dummy arg - name of program */
	fuse_opt_add_arg(&fuse_args, "weston-debug");

	debug->fuse_chan = fuse_mount(debug->debug_dir, &fuse_args);
	if (!debug->fuse_chan) {
		perror("Fuse mount");
		goto err;
	}

	debug->fuse = fuse_new(debug->fuse_chan, &fuse_args, &operations,
			       sizeof operations, NULL);
	if (!debug->fuse) {
		perror("Creating fuse");
		goto err;
	}

	fuse_opt_free_args(&fuse_args);

	return 0;
err:
	if(debug->fuse_chan)
		fuse_unmount(debug->debug_dir, debug->fuse_chan);
	fuse_opt_free_args(&fuse_args);
	return -1;
}


/* fuse_loop is blocking and I haven't found any non-blocking
 * way of how to embed the fuse channel's fd into wayland loop.
 * Because of that, use thread */
static void *
fuse_thread(void *data)
{
	struct debug *debug = data;

	if (fuse_init(debug) < 0)
		pthread_exit(NULL);

	fuse_loop(debug->fuse);
	pthread_exit(NULL);
}

WL_EXPORT int
module_init(struct weston_compositor *ec,
	    int *argc, char *argv[])
{
	struct debug *debug;
	pthread_attr_t thr_attr;

	printf("Creating weston-debugfs\n");

	debug = malloc(sizeof *debug);
	if (!debug)
		return -1;

	memset(debug, 0, sizeof *debug);
	compositor = ec;

	if (!(debug->debug_dir = create_debug_dir()))
		goto err;

	/* handle sigsegv, otherwise the fuse will not be unmounted
	 * and it is very.. well, uncomfortable */
	debug->sigsegv_source =
		wl_event_loop_add_signal(wl_display_get_event_loop(ec->wl_display),
					 SIGSEGV, handle_sigsegv, debug);
	if (!debug->sigsegv_source) {
		perror("Handling SIGSEGV");
		goto err;
	}

	debug->compositor_destroyed.notify = cleanup;
	wl_signal_add(&ec->destroy_signal, &debug->compositor_destroyed);

	if (pthread_mutex_init(&debug->mutex, NULL) != 0) {
		perror("mutex init");
		goto err;
	}

	if (create_hierarchy(debug) < 0) {
		fprintf(stderr, "Failed creating hierarchy\n");
		goto err;
	}

	pthread_attr_init(&thr_attr);
	pthread_attr_setdetachstate(&thr_attr, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&debug->fuse_tid, NULL, fuse_thread, debug) != 0) {
		perror("Creating fuse thread");
		free_entry_unlocked(hierarchy);
		goto err;
	}

	return 0;

err:
	debug_destroy(debug);

	return -1;
}
