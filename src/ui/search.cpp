#include "search.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace logic::ui {

namespace {

bool word_start(const std::string& s, size_t i)
{
    if (i == 0) return true;
    auto prev = (unsigned char)s[i - 1], cur = (unsigned char)s[i];
    if (!std::isalnum(prev)) return true;
    if ((bool)std::isdigit(prev) != (bool)std::isdigit(cur)) return true;
    return std::islower(prev) && std::isupper(cur);
}

}

std::optional<text_match> match_text(const std::string& name, const std::string& text)
{
    if (text.empty()) return text_match{0, 0};
    if (text.size() > name.size()) return std::nullopt;

    auto same = [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); };
    std::optional<text_match> best;
    for (size_t i = 0; i + text.size() <= name.size(); i++) {
        if (!std::equal(text.begin(), text.end(), name.begin() + i, same)) continue;
        int rank = i == 0 ? 0 : word_start(name, i) ? 1 : 2;
        if (!best || rank < best->rank) best = text_match{rank, i};
        if (rank <= 1) break;
    }
    return best;
}

void recent_list::use(uint64_t id)
{
    std::erase(ids, id);
    ids.insert(ids.begin(), id);
    if (ids.size() > max) ids.resize(max);
}

std::string recent_list::to_string() const
{
    std::string s;
    for (auto id : ids) {
        if (!s.empty()) s += ',';
        s += std::to_string(id);
    }
    return s;
}

void recent_list::from_string(const std::string& s)
{
    ids.clear();
    std::stringstream in(s);
    std::string part;
    while (std::getline(in, part, ',')) {
        char* end = nullptr;
        auto id = std::strtoull(part.c_str(), &end, 10);
        if (part.empty() || *end != 0) continue;
        if (std::find(ids.begin(), ids.end(), id) == ids.end() && ids.size() < max) ids.push_back(id);
    }
}

}
