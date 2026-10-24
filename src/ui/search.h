// helpers for search boxes: ranking names by a typed text, and a list of
// recently used items. no imgui needed.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace logic::ui {

// where a search text is found in a name, ignoring case
struct text_match {
    // 0: the name starts with it, 1: a word in the name starts with it,
    // 2: somewhere else. lower ranks are listed first.
    int rank = 0;
    // the found text is name[pos, pos + text size)
    size_t pos = 0;
};

// null if the name doesn't contain the text. an empty text matches at 0.
// words start after a space or punctuation, between a letter and a digit
// and at a lower to upper case change ("bitExtender", "Bit extender").
std::optional<text_match> match_text(const std::string& name, const std::string& text);

// the most recently used ids first, without duplicates, at most max of them
class recent_list {
public:
    explicit recent_list(size_t max = 8)
        : max(max)
    {
    }

    // moves the id to the front
    void use(uint64_t id);
    const std::vector<uint64_t>& items() const { return ids; }

    // comma separated, for settings files. parsing skips what isn't a number.
    std::string to_string() const;
    void from_string(const std::string& s);

private:
    size_t max;
    std::vector<uint64_t> ids;
};

}
