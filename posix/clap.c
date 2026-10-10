#include "host/clap.h"
#include <poll.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	int fd;
	clap_posix_fd_flags_t flags;
} ClapFd;
struct ClapPlatform {
	ClapFd fds[64];
	unsigned nfds;
};

static bool register_fd(const clap_host_t *h, int fd, clap_posix_fd_flags_t flags) {
	ClapPlatform *c = ((Clap *)h->host_data)->platform;
	if (fd < 0 || !flags || c->nfds == 64)
		return false;
	for (unsigned i = 0; i < c->nfds; ++i)
		if (c->fds[i].fd == fd)
			return false;
	c->fds[c->nfds++] = (ClapFd){fd, flags};
	return true;
}
static bool modify_fd(const clap_host_t *h, int fd, clap_posix_fd_flags_t flags) {
	ClapPlatform *c = ((Clap *)h->host_data)->platform;
	for (unsigned i = 0; i < c->nfds; ++i)
		if (c->fds[i].fd == fd) {
			c->fds[i].flags = flags;
			return true;
		}
	return false;
}
static bool unregister_fd(const clap_host_t *h, int fd) {
	ClapPlatform *c = ((Clap *)h->host_data)->platform;
	for (unsigned i = 0; i < c->nfds; ++i)
		if (c->fds[i].fd == fd) {
			c->fds[i] = c->fds[--c->nfds];
			return true;
		}
	return false;
}
const void *clap_platform_extension(const char *id) {
	static const clap_host_posix_fd_support_t fd = {register_fd, modify_fd, unregister_fd};
	return !strcmp(id, CLAP_EXT_POSIX_FD_SUPPORT) ? &fd : NULL;
}
int clap_platform_open(Clap *c) {
	c->platform = calloc(1, sizeof(*c->platform));
	return c->platform ? 0 : -1;
}
void clap_platform_close(Clap *c) {
	free(c->platform);
}
void clap_platform_poll(Clap *c) {
	ClapPlatform *p = c->platform;
	const clap_plugin_posix_fd_support_t *api = c->plugin->get_extension(c->plugin, CLAP_EXT_POSIX_FD_SUPPORT);
	struct pollfd fds[64];
	unsigned nfds = p->nfds;
	for (unsigned i = 0; i < nfds; ++i)
		fds[i] = (struct pollfd){.fd = p->fds[i].fd,
		    .events = (p->fds[i].flags & CLAP_POSIX_FD_READ ? POLLIN : 0) |
		        (p->fds[i].flags & CLAP_POSIX_FD_WRITE ? POLLOUT : 0) |
		        (p->fds[i].flags & CLAP_POSIX_FD_ERROR ? POLLERR : 0)};
	if (api && poll(fds, nfds, 0) > 0)
		for (unsigned i = 0; i < nfds; ++i)
			if (fds[i].revents)
				for (unsigned j = 0; j < p->nfds; ++j)
					if (p->fds[j].fd == fds[i].fd) {
						api->on_fd(c->plugin, fds[i].fd,
						    (fds[i].revents & (POLLIN | POLLHUP) ? CLAP_POSIX_FD_READ : 0) |
						        (fds[i].revents & POLLOUT ? CLAP_POSIX_FD_WRITE : 0) |
						        (fds[i].revents & (POLLERR | POLLNVAL) ? CLAP_POSIX_FD_ERROR : 0));
						break;
					}
}
