#include "config.h"  // IWYU pragma: keep

#include "redirection.h"

#include <fcntl.h>

#include "io.h"
#include "wutil.h"

dup2_list_t::~dup2_list_t() = default;

maybe_t<int> redirection_spec_t::get_target_as_fd() const {
    errno = 0;
    int result = fish_wcstoi(target.c_str());
    if (errno || result < 0) return none();
    return result;
}

int redirection_spec_t::oflags() const {
    switch (mode) {
        case redirection_mode_t::append:
            return O_CREAT | O_APPEND | O_WRONLY;
        case redirection_mode_t::overwrite:
            return O_CREAT | O_WRONLY | O_TRUNC;
        case redirection_mode_t::noclob:
            return O_CREAT | O_EXCL | O_WRONLY;
        case redirection_mode_t::input:
            return O_RDONLY;
        case redirection_mode_t::fd:
        default:
            DIE("Not a file redirection");
    }
}

maybe_t<dup2_list_t> dup2_list_t::resolve_chain(const io_chain_t &io_chain) {
    ASSERT_IS_NOT_FORKED_CHILD();
    dup2_list_t result;
    for (const auto &io_ref : io_chain) {
        switch (io_ref->io_mode) {
            case io_mode_t::file: {
                const io_file_t *io = static_cast<const io_file_t *>(io_ref.get());
                result.add_dup2(io->file_fd(), io->fd);
                break;
            }

            case io_mode_t::close: {
                const io_close_t *io = static_cast<const io_close_t *>(io_ref.get());
                result.add_close(io->fd);
                break;
            }

            case io_mode_t::fd: {
                const io_fd_t *io = static_cast<const io_fd_t *>(io_ref.get());
                result.add_dup2(io->old_fd, io->fd);
                break;
            }

            case io_mode_t::pipe: {
                const io_pipe_t *io = static_cast<const io_pipe_t *>(io_ref.get());
                result.add_dup2(io->pipe_fd(), io->fd);
                result.add_close(io->pipe_fd());
                break;
            }

            case io_mode_t::bufferfill: {
                const io_bufferfill_t *io = static_cast<const io_bufferfill_t *>(io_ref.get());
                result.add_dup2(io->write_fd(), io->fd);
                result.add_close(io->write_fd());
                break;
            }
        }
    }
    return {std::move(result)};
}

int dup2_list_t::fd_for_target_fd(int target) const {
    // Paranoia.
    if (target < 0) {
        return target;
    }
    // Note we can simply walk our action list backwards, looking for src -> target dups.
    int cursor = target;
    for (auto iter = actions_.rbegin(); iter != actions_.rend(); ++iter) {
        if (iter->target == cursor) {
            // cursor is replaced by iter->src
            cursor = iter->src;
        } else if (iter->src == cursor && iter->target < 0) {
            // cursor is closed.
            cursor = -1;
            break;
        }
    }
    return cursor;
}
