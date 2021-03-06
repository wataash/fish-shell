// High level library for handling the terminal screen
//
// The screen library allows the interactive reader to write its output to screen efficiently by
// keeping an internal representation of the current screen contents and trying to find a reasonably
// efficient way for transforming that to the desired screen content.
//
// The current implementation is less smart than ncurses allows and can not for example move blocks
// of text around to handle text insertion.
#ifndef FISH_SCREEN_H
#define FISH_SCREEN_H
#include "config.h"  // IWYU pragma: keep

#include <stddef.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstddef>
#include <cwchar>
#include <list>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common.h"
#include "highlight.h"

class page_rendering_t;

/// A class representing a single line of a screen.
struct line_t {
    std::vector<wchar_t> text;
    std::vector<highlight_spec_t> colors;
    bool is_soft_wrapped;
    size_t indentation;

    line_t() : text(), colors(), is_soft_wrapped(false), indentation(0) {}

    void clear(void) {
        text.clear();
        colors.clear();
    }

    void append(wchar_t txt, highlight_spec_t color) {
        text.push_back(txt);
        colors.push_back(color);
    }

    void append(const wchar_t *txt, highlight_spec_t color) {
        for (size_t i = 0; txt[i]; i++) {
            text.push_back(txt[i]);
            colors.push_back(color);
        }
    }

    size_t size(void) const { return text.size(); }

    wchar_t char_at(size_t idx) const { return text.at(idx); }

    highlight_spec_t color_at(size_t idx) const { return colors.at(idx); }

    void append_line(const line_t &line) {
        text.insert(text.end(), line.text.begin(), line.text.end());
        colors.insert(colors.end(), line.colors.begin(), line.colors.end());
    }

    wcstring to_string() const { return wcstring(this->text.begin(), this->text.end()); }
};

/// A class representing screen contents.
class screen_data_t {
    std::vector<line_t> line_datas;

   public:
    struct cursor_t {
        int x;
        int y;
        cursor_t() : x(0), y(0) {}
        cursor_t(int a, int b) : x(a), y(b) {}
    } cursor;

    line_t &add_line(void) {
        line_datas.resize(line_datas.size() + 1);
        return line_datas.back();
    }

    void resize(size_t size) { line_datas.resize(size); }

    line_t &create_line(size_t idx) {
        if (idx >= line_datas.size()) {
            line_datas.resize(idx + 1);
        }
        return line_datas.at(idx);
    }

    line_t &insert_line_at_index(size_t idx) {
        assert(idx <= line_datas.size());
        return *line_datas.insert(line_datas.begin() + idx, line_t());
    }

    line_t &line(size_t idx) { return line_datas.at(idx); }

    const line_t &line(size_t idx) const { return line_datas.at(idx); }

    size_t line_count() const { return line_datas.size(); }

    void append_lines(const screen_data_t &d) {
        this->line_datas.insert(this->line_datas.end(), d.line_datas.begin(), d.line_datas.end());
    }

    bool empty() const { return line_datas.empty(); }
};

class outputter_t;

/// The class representing the current and desired screen contents.
class screen_t {
    outputter_t &outp_;

   public:
    /// Constructor.
    screen_t();

    /// The internal representation of the desired screen contents.
    screen_data_t desired;
    /// The internal representation of the actual screen contents.
    screen_data_t actual;
    /// A string containing the prompt which was last printed to the screen.
    wcstring actual_left_prompt;
    /// Last right prompt width.
    size_t last_right_prompt_width;
    /// The actual width of the screen at the time of the last screen write.
    int actual_width;
    /// If we support soft wrapping, we can output to this location without any cursor motion.
    screen_data_t::cursor_t soft_wrap_location;
    /// Whether the last-drawn autosuggestion (if any) is truncated, or hidden entirely.
    bool autosuggestion_is_truncated;
    /// This flag is set to true when there is reason to suspect that the parts of the screen lines
    /// where the actual content is not filled in may be non-empty. This means that a clr_eol
    /// command has to be sent to the terminal at the end of each line, including
    /// actual_lines_before_reset.
    bool need_clear_lines;
    /// Whether there may be yet more content after the lines, and we issue a clr_eos if possible.
    bool need_clear_screen;
    /// If we need to clear, this is how many lines the actual screen had, before we reset it. This
    /// is used when resizing the window larger: if the cursor jumps to the line above, we need to
    /// remember to clear the subsequent lines.
    size_t actual_lines_before_reset;
    /// These status buffers are used to check if any output has occurred other than from fish's
    /// main loop, in which case we need to redraw.
    struct stat prev_buff_1, prev_buff_2, post_buff_1, post_buff_2;

    /// \return the outputter for this screen.
    outputter_t &outp() { return outp_; }
};

/// This is the main function for the screen putput library. It is used to define the desired
/// contents of the screen. The screen command will use its knowledge of the current contents of the
/// screen in order to render the desired output using as few terminal commands as possible.
///
/// \param s the screen on which to write
/// \param left_prompt the prompt to prepend to the command line
/// \param right_prompt the right prompt, or NULL if none
/// \param commandline the command line
/// \param explicit_len the number of characters of the "explicit" (non-autosuggestion) portion of
/// the command line
/// \param colors the colors to use for the comand line
/// \param indent the indent to use for the command line
/// \param cursor_pos where the cursor is
/// \param pager_data any pager data, to append to the screen
/// \param cursor_is_within_pager whether the position is within the pager line (first line)
void s_write(screen_t *s, const wcstring &left_prompt, const wcstring &right_prompt,
             const wcstring &commandline, size_t explicit_len,
             const std::vector<highlight_spec_t> &colors, const std::vector<int> &indent,
             size_t cursor_pos, const page_rendering_t &pager_data, bool cursor_is_within_pager);

/// This function resets the screen buffers internal knowledge about the contents of the screen. Use
/// this function when some other function than s_write has written to the screen.
///
/// \param s the screen to reset
/// \param reset_cursor whether the line on which the cursor has changed should be assumed to have
/// changed. If \c reset_cursor is false, the library will attempt to make sure that the screen area
/// does not seem to move up or down on repaint.
/// \param reset_prompt whether to reset the prompt as well.
///
/// If reset_cursor is incorrectly set to false, this may result in screen contents being erased. If
/// it is incorrectly set to true, it may result in one or more lines of garbage on screen on the
/// next repaint. If this happens during a loop, such as an interactive resizing, there will be one
/// line of garbage for every repaint, which will quickly fill the screen.
void s_reset(screen_t *s, bool reset_cursor, bool reset_prompt = true);

/// Stat stdout and stderr and save result as the current timestamp.
void s_save_status(screen_t *s);

enum screen_reset_mode_t {
    /// Do not make a new line, do not repaint the prompt.
    screen_reset_current_line_contents,
    /// Do not make a new line, do repaint the prompt.
    screen_reset_current_line_and_prompt,
    /// Abandon the current line, go to the next one, repaint the prompt.
    screen_reset_abandon_line,
    /// Abandon the current line, go to the next one, clear the rest of the screen.
    screen_reset_abandon_line_and_clear_to_end_of_screen
};

void s_reset(screen_t *s, screen_reset_mode_t mode);

/// Issues an immediate clr_eos.
void screen_force_clear_to_end();

/// Returns the length of an escape code. Exposed for testing purposes only.
size_t escape_code_length(const wchar_t *code);

// Information about the layout of a prompt.
struct prompt_layout_t {
    size_t line_count;       // how many lines the prompt consumes
    size_t max_line_width;   // width of the longest line
    size_t last_line_width;  // width of the last line
};

// Maintain a mapping of escape sequences to their length for fast lookup.
class layout_cache_t {
   private:
    // Cached escape sequences we've already detected in the prompt and similar strings, ordered
    // lexicographically.
    wcstring_list_t esc_cache_;

    // LRU-list of prompts and their layouts.
    // Use a list so we can promote to the front on a cache hit.
    using prompt_layout_pair_t = std::pair<wcstring, prompt_layout_t>;
    std::list<prompt_layout_pair_t> prompt_cache_;

   public:
    static constexpr size_t prompt_cache_max_size = 8;

    /// \return the size of the escape code cache.
    size_t esc_cache_size() const { return esc_cache_.size(); }

    /// Insert the entry \p str in its sorted position, if it is not already present in the cache.
    void add_escape_code(wcstring str) {
        auto where = std::upper_bound(esc_cache_.begin(), esc_cache_.end(), str);
        if (where == esc_cache_.begin() || where[-1] != str) {
            esc_cache_.emplace(where, std::move(str));
        }
    }

    /// \return the length of a string that matches a prefix of \p entry.
    size_t find_escape_code(const wchar_t *entry) const {
        // Do a binary search and see if the escape code right before our entry is a prefix of our
        // entry. Note this assumes that escape codes are prefix-free: no escape code is a prefix of
        // another one. This seems like a safe assumption.
        auto where = std::upper_bound(esc_cache_.begin(), esc_cache_.end(), entry);
        // 'where' is now the first element that is greater than entry. Thus where-1 is the last
        // element that is less than or equal to entry.
        if (where != esc_cache_.begin()) {
            const wcstring &candidate = where[-1];
            if (string_prefixes_string(candidate.c_str(), entry)) return candidate.size();
        }
        return 0;
    }

    // Finds the layout for a prompt, promoting it to the front. Returns none() if not found.
    maybe_t<prompt_layout_t> find_prompt_layout(const wcstring &input);

    // Adds a prompt layout for a given string.
    void add_prompt_layout(wcstring input, prompt_layout_t layout);

    void clear() {
        esc_cache_.clear();
        prompt_cache_.clear();
    }
};

// Singleton that is exposed so that the cache can be invalidated when terminal related variables
// change by calling `cached_esc_sequences.clear()`.
extern layout_cache_t cached_layouts;

#endif
