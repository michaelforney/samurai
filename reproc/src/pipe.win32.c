#define _WIN32_WINNT _WIN32_WINNT_VISTA

#include "pipe.h"

#include "error.h"
#include "handle.h"
#include "macro.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <windows.h>
#include <winsock2.h>

const SOCKET PIPE_INVALID = INVALID_SOCKET;

// Inspired by https://gist.github.com/geertj/4325783.
static int socketpair(int domain, int type, int protocol, SOCKET *out)
{
  assert(out);

  SOCKET server = PIPE_INVALID;
  SOCKET pair[] = { PIPE_INVALID, PIPE_INVALID };
  int r = -1;

  server = WSASocketW(AF_INET, SOCK_STREAM, 0, NULL, 0, 0);
  if (server == INVALID_SOCKET) {
    goto finish;
  }

  SOCKADDR_IN localhost = { 0 };
  localhost.sin_family = AF_INET;
  localhost.sin_addr.S_un.S_addr = htonl(INADDR_LOOPBACK);
  localhost.sin_port = 0;

  r = bind(server, (SOCKADDR *) &localhost, sizeof(localhost));
  if (r < 0) {
    goto finish;
  }

  r = listen(server, 1);
  if (r < 0) {
    goto finish;
  }

  SOCKADDR_STORAGE name = { 0 };
  int size = sizeof(name);
  r = getsockname(server, (SOCKADDR *) &name, &size);
  if (r < 0) {
    goto finish;
  }

  pair[0] = WSASocketW(domain, type, protocol, NULL, 0, 0);
  if (pair[0] == INVALID_SOCKET) {
    goto finish;
  }

  r = pipe_nonblocking(pair[0], true);
  if (r < 0) {
    goto finish;
  }

  r = connect(pair[0], (SOCKADDR *) &name, size);
  if (r < 0 && WSAGetLastError() != WSAEWOULDBLOCK) {
    goto finish;
  }

  r = pipe_nonblocking(pair[0], false);
  if (r < 0) {
    goto finish;
  }

  pair[1] = accept(server, NULL, NULL);
  if (pair[1] == INVALID_SOCKET) {
    r = -1;
    goto finish;
  }

  out[0] = pair[0];
  out[1] = pair[1];

  pair[0] = PIPE_INVALID;
  pair[1] = PIPE_INVALID;

  r = 0;

finish:
  pipe_destroy(server);
  pipe_destroy(pair[0]);
  pipe_destroy(pair[1]);

  return error_unify(r);
}

int pipe_init(SOCKET *read, SOCKET *write)
{
  assert(read);
  assert(write);

  SOCKET pair[] = { PIPE_INVALID, PIPE_INVALID };
  int r = 0;

  // Use sockets instead of pipes so we can use `WSAPoll` which only works with
  // sockets.
  r = socketpair(AF_INET, SOCK_STREAM, 0, pair);
  if (r < 0) {
    goto finish;
  }

  r = SetHandleInformation((HANDLE) pair[0], HANDLE_FLAG_INHERIT, 0);
  if (r == 0) {
    goto finish;
  }

  r = SetHandleInformation((HANDLE) pair[1], HANDLE_FLAG_INHERIT, 0);
  if (r == 0) {
    goto finish;
  }

  // Make the connection unidirectional to better emulate a pipe.

  r = shutdown(pair[0], SD_SEND);
  if (r < 0) {
    goto finish;
  }

  r = shutdown(pair[1], SD_RECEIVE);
  if (r < 0) {
    goto finish;
  }

  *read = pair[0];
  *write = pair[1];

  pair[0] = PIPE_INVALID;
  pair[1] = PIPE_INVALID;

finish:
  pipe_destroy(pair[0]);
  pipe_destroy(pair[1]);

  return error_unify(r);
}

int pipe_nonblocking(SOCKET pipe, bool enable)
{
  u_long mode = enable;
  int r = ioctlsocket(pipe, (long) FIONBIO, &mode);
  return error_unify(r);
}

int pipe_read(SOCKET pipe, uint8_t *buffer, size_t size)
{
  assert(pipe != PIPE_INVALID);
  assert(buffer);
  assert(size <= INT_MAX);

  int r = recv(pipe, (char *) buffer, (int) size, 0);

  if (r == 0 || (r < 0 && WSAGetLastError() == WSAECONNRESET)) {
    r = -ERROR_BROKEN_PIPE;
  }

  return error_unify_or_else(r, r);
}

int pipe_write(SOCKET pipe, const uint8_t *buffer, size_t size)
{
  assert(pipe != PIPE_INVALID);
  assert(buffer);
  assert(size <= INT_MAX);

  int r = send(pipe, (const char *) buffer, (int) size, 0);

  if (r < 0 && WSAGetLastError() == WSAECONNRESET) {
    r = -ERROR_BROKEN_PIPE;
  }

  return error_unify_or_else(r, r);
}

int pipe_wait(pipe_set *sets, size_t num_sets, int timeout)
{
  assert(num_sets * PIPES_PER_SET <= INT_MAX);

  WSAPOLLFD *pollfds = NULL;
  size_t num_pipes = num_sets * PIPES_PER_SET;
  int r = -ERROR_NOT_ENOUGH_MEMORY;

  pollfds = calloc(sizeof(WSAPOLLFD), num_pipes);
  if (pollfds == NULL) {
    goto finish;
  }

  for (size_t i = 0; i < num_sets; i++) {
    size_t j = i * PIPES_PER_SET;
    pollfds[j + 0] = (WSAPOLLFD){ .fd = sets[i].in, .events = POLLOUT };
    pollfds[j + 1] = (WSAPOLLFD){ .fd = sets[i].out, .events = POLLIN };
    pollfds[j + 2] = (WSAPOLLFD){ .fd = sets[i].err, .events = POLLIN };
    pollfds[j + 3] = (WSAPOLLFD){ .fd = sets[i].exit };
  }

  r = WSAPoll(pollfds, (ULONG) num_pipes, timeout);
  if (r < 0) {
    goto finish;
  }

  for (size_t i = 0; i < num_sets; i++) {
    sets[i].events = 0;
  }

  for (size_t i = 0; i < num_pipes; i++) {
    WSAPOLLFD pollfd = pollfds[i];

    if (pollfd.revents > 0 && pollfd.revents != POLLNVAL) {
      int event = 1 << (i % PIPES_PER_SET);
      sets[i / PIPES_PER_SET].events |= event;
    }
  }

  if (r == 0) {
    r = -WAIT_TIMEOUT;
  } else if(r > 0) {
    // `WSAPoll` returns the amount of ready sockets on success so we explicitly
    // reset `r` to 0.
    r = 0;
  }

finish:
  free(pollfds);

  return r;
}

SOCKET pipe_destroy(SOCKET pipe)
{
  if (pipe == PIPE_INVALID) {
    return PIPE_INVALID;
  }

  int saved = WSAGetLastError();

  int r = closesocket(pipe);
  ASSERT_UNUSED(r == 0);

  WSASetLastError(saved);

  return PIPE_INVALID;
}
