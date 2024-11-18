
#define DUMP_BUF_LEN (1024)
const char *dump_event(const int ev)
{
    static char str[DUMP_BUF_LEN];
    int offset = 0;

    if (ev & EPOLLIN) {
        offset = sprintf(str + offset, "%s", "EPOLLIN");
    }

    if (ev & EPOLLOUT) {
        offset = sprintf(str + offset, "%s | ", "EPOLLOUT");
    }

    if (ev & EPOLLRDHUP) {
        offset = sprintf(str + offset, "%s | ", "EPOLLRDHUP");
    }

    if (ev & EPOLLPRI) {
        offset = sprintf(str + offset, "%s | ", "EPOLLPRI");
    }

    if (ev & EPOLLERR) {
        offset = sprintf(str + offset, "%s | ", "EPOLLERR");
    }

    if (ev & EPOLLHUP) {
        offset = sprintf(str + offset, "%s | ", "EPOLLHUP");
    }

    if (ev & EPOLLET) {
        offset = sprintf(str + offset, "%s | ", "EPOLLET");
    }

    if (ev & EPOLLONESHOT) {
        offset = sprintf(str + offset, "%s | ", "EPOLLONESHOT");
    }
    if (ev & EPOLLWAKEUP) {
        offset = sprintf(str + offset, "%s | ", "EPOLLWAKEUP");
    }
    if (ev & EPOLLEXCLUSIVE) {
        sprintf(str + offset, "%s | ", "EPOLLEXCLUSIVE");
    }

    return str;
}

int pepa_process_fdx_analyze_error(const int ev, const char *name)
{
    if (ev & EPOLLRDHUP) {
        slog_error_l("[%s] Remote side disconnected", name);
        return -PEPA_ERR_BAD_SOCKET_REMOTE;
    }

    /* This side is broken. Close and remove this file descriptor from IN, return error */
    if (ev & EPOLLHUP) {
        slog_error_l("[%s] This side disconnected", name);
        return -PEPA_ERR_BAD_SOCKET_LOCAL;
    }

    /* Another error happened. Close and remove this file descriptor from IN, return error*/
    if (ev & EPOLLERR) {
        slog_error_l("[%s] Unknown error", name);
        return -PEPA_ERR_BAD_SOCKET_LOCAL;
    }

    return PEPA_ERR_OK;
}

