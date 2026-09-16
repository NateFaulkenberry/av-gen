#include "song/reanalysis.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace avgen::song {
namespace {

constexpr double kEpsilon = 1e-9;

[[nodiscard]] std::string clockOf(double seconds) {
    const auto minutes = static_cast<int>(std::floor(std::max(seconds, 0.0) / 60.0));
    return fmt::format("{}:{:05.2f}", minutes, std::max(seconds, 0.0) - minutes * 60.0);
}

[[nodiscard]] std::string spanOf(const Section& s) {
    return fmt::format("{}-{}", clockOf(s.startSeconds), clockOf(s.endSeconds));
}

[[nodiscard]] std::string nameOf(const Section& s) {
    return s.label.empty() ? s.type : s.label;
}

[[nodiscard]] double overlapOf(const Section& a, const Section& b) {
    return std::max(0.0, std::min(a.endSeconds, b.endSeconds) -
                             std::max(a.startSeconds, b.startSeconds));
}

// A fresh section with every frozen span subtracted from it. Returns the pieces that survive, which
// may be none (a frozen section covered it), one (untouched, or trimmed at one end) or two (a frozen
// section landed in the middle of it).
[[nodiscard]] std::vector<Section> carve(const Section& freshSection,
                                         const std::vector<Section>& frozen) {
    std::vector<Section> parts{freshSection};
    for (const Section& block : frozen) {
        std::vector<Section> next;
        next.reserve(parts.size() + 1);
        for (const Section& p : parts) {
            const bool disjoint = p.endSeconds <= block.startSeconds + kEpsilon ||
                                  p.startSeconds >= block.endSeconds - kEpsilon;
            if (disjoint) {
                next.push_back(p);
                continue;
            }
            const bool swallowed = p.startSeconds >= block.startSeconds - kEpsilon &&
                                   p.endSeconds <= block.endSeconds + kEpsilon;
            if (swallowed) {
                continue;
            }
            if (p.startSeconds < block.startSeconds && p.endSeconds > block.endSeconds) {
                Section left = p;
                left.endSeconds = block.startSeconds;
                Section right = p;
                right.startSeconds = block.endSeconds;
                next.push_back(std::move(left));
                next.push_back(std::move(right));
                continue;
            }
            Section trimmed = p;
            if (trimmed.startSeconds < block.startSeconds) {
                trimmed.endSeconds = block.startSeconds;
            } else {
                trimmed.startSeconds = block.endSeconds;
            }
            next.push_back(std::move(trimmed));
        }
        parts = std::move(next);
    }
    std::erase_if(parts,
                  [](const Section& s) { return s.endSeconds - s.startSeconds <= kEpsilon; });
    return parts;
}

// One section on its way into the result, with the one fact the result itself will not record:
// whether the analyzer just produced it, or a person did.
struct Piece {
    Section section;
    bool fromFresh = false;
};

} // namespace

const char* reanalysisPolicyName(ReanalysisPolicy p) {
    return p == ReanalysisPolicy::Replace ? "replace" : "merge";
}

bool ReanalysisReport::changedAnything() const {
    return sectionsReplaced != 0 || spansFrozen != 0 || fieldsCarried != 0 || freshTrimmed != 0 ||
           freshDropped != 0 || discarded != 0;
}

std::string ReanalysisReport::summary() const {
    if (!changedAnything()) {
        return "Re-analysis found nothing to do.";
    }
    std::string out = fmt::format("{} section{} re-detected", sectionsReplaced,
                                  sectionsReplaced == 1 ? "" : "s");
    if (spansFrozen > 0) {
        out += fmt::format(", {} span{} kept", spansFrozen, spansFrozen == 1 ? "" : "s");
    }
    if (fieldsCarried > 0) {
        out += fmt::format(", {} edit{} kept on {} section{}", fieldsCarried,
                           fieldsCarried == 1 ? "" : "s", sectionsCarried,
                           sectionsCarried == 1 ? "" : "s");
    }
    if (discarded > 0) {
        out += fmt::format(", {} edited section{} discarded", discarded, discarded == 1 ? "" : "s");
    }
    return out + ".";
}

ReanalysisReport reanalyze(SectionTimeline& current, const SectionTimeline& fresh,
                           ReanalysisPolicy policy) {
    ReanalysisReport report;

    // Refusing to act on empty information is much better than emptying somebody's timeline because
    // the detector had a bad day. ADR-215's rule, kept.
    if (fresh.sections.empty()) {
        return report;
    }

    if (policy == ReanalysisPolicy::Replace) {
        for (const Section& s : current.sections) {
            if (s.authored || any(s.edited)) {
                ++report.discarded;
            }
        }
        current = fresh;
        current.renumber();
        report.sectionsReplaced = static_cast<int>(current.sections.size());
        return report;
    }

    // ---- 1. what a person decided -------------------------------------------------------------
    //
    // Two kinds, and they are treated differently on purpose. A section whose *span* a person set
    // keeps its span and everything in it. A section whose type, name or treatment a person set --
    // but whose boundaries they left alone -- keeps those decisions and takes the fresh boundaries,
    // which is the improvement over ADR-215 that the brief's section 18 asks for.
    std::vector<Section> frozen;
    std::vector<Section> loose;
    for (const Section& s : current.sections) {
        if (s.spanIsFrozen()) {
            frozen.push_back(s);
        } else if (any(s.edited)) {
            loose.push_back(s);
        }
    }
    report.spansFrozen = static_cast<int>(frozen.size());

    // ---- 2. cut the fresh detection around the frozen spans -------------------------------------
    std::vector<Piece> pieces;
    pieces.reserve(fresh.sections.size() + frozen.size() + 2);
    for (const Section& f : fresh.sections) {
        std::vector<Section> parts = carve(f, frozen);
        if (parts.empty()) {
            ++report.freshDropped;
            continue;
        }
        const bool unchanged = parts.size() == 1 &&
                               parts.front().startSeconds == f.startSeconds &&
                               parts.front().endSeconds == f.endSeconds;
        if (!unchanged) {
            ++report.freshTrimmed;
        }
        for (Section& p : parts) {
            pieces.push_back(Piece{std::move(p), true});
        }
    }
    for (Section& f : frozen) {
        pieces.push_back(Piece{std::move(f), false});
    }

    std::stable_sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) {
        return a.section.startSeconds < b.section.startSeconds;
    });
    std::erase_if(pieces, [](const Piece& p) {
        return p.section.endSeconds - p.section.startSeconds <= kEpsilon;
    });

    // Close any daylight the carving left. A frozen boundary is where a person put it, so the
    // detected neighbour is the one that moves.
    for (std::size_t i = 1; i < pieces.size(); ++i) {
        const double gap = pieces[i].section.startSeconds - pieces[i - 1].section.endSeconds;
        if (std::fabs(gap) <= kEpsilon) {
            pieces[i].section.startSeconds = pieces[i - 1].section.endSeconds;
            continue;
        }
        if (pieces[i].fromFresh) {
            pieces[i].section.startSeconds = pieces[i - 1].section.endSeconds;
        } else {
            pieces[i - 1].section.endSeconds = pieces[i].section.startSeconds;
        }
    }

    // ---- 3. carry the decisions off the loose sections ------------------------------------------
    //
    // Greedily, by overlap, largest first, and **at most one loose section to any one fresh piece**.
    // Without that cap, two edited sections that a coarser fresh detection merges into one would
    // both write to it and the second would silently win -- which is data loss wearing the costume
    // of a successful merge.
    struct Claim {
        double overlap = 0.0;
        std::size_t looseIndex = 0;
        std::size_t pieceIndex = 0;
    };
    std::vector<Claim> claims;
    for (std::size_t l = 0; l < loose.size(); ++l) {
        for (std::size_t p = 0; p < pieces.size(); ++p) {
            if (!pieces[p].fromFresh) {
                continue;
            }
            const double ov = overlapOf(loose[l], pieces[p].section);
            if (ov > kEpsilon) {
                claims.push_back(Claim{ov, l, p});
            }
        }
    }
    // Ties broken by index so the result is a pure function of the inputs on every machine.
    std::sort(claims.begin(), claims.end(), [](const Claim& a, const Claim& b) {
        if (a.overlap != b.overlap) {
            return a.overlap > b.overlap;
        }
        if (a.looseIndex != b.looseIndex) {
            return a.looseIndex < b.looseIndex;
        }
        return a.pieceIndex < b.pieceIndex;
    });

    std::vector<bool> looseTaken(loose.size(), false);
    std::vector<bool> pieceTaken(pieces.size(), false);
    for (const Claim& c : claims) {
        if (looseTaken[c.looseIndex] || pieceTaken[c.pieceIndex]) {
            continue;
        }
        looseTaken[c.looseIndex] = true;
        pieceTaken[c.pieceIndex] = true;

        const Section& from = loose[c.looseIndex];
        Section& to = pieces[c.pieceIndex].section;
        std::vector<std::string> moved;
        if (from.isEdited(SectionField::Type)) {
            to.type = from.type;
            to.edited |= SectionField::Type;
            moved.emplace_back("type");
        }
        if (from.isEdited(SectionField::Label)) {
            to.label = from.label;
            to.edited |= SectionField::Label;
            moved.emplace_back("name");
        }
        if (from.isEdited(SectionField::ShotIntent)) {
            to.shotIntent = from.shotIntent;
            to.edited |= SectionField::ShotIntent;
            moved.emplace_back("shot");
        }
        if (moved.empty()) {
            continue;
        }
        report.fieldsCarried += static_cast<int>(moved.size());
        ++report.sectionsCarried;
        report.kept.push_back(
            fmt::format("{} {} ({})", spanOf(to), nameOf(to), fmt::join(moved, ", ")));
    }
    for (std::size_t l = 0; l < loose.size(); ++l) {
        if (!looseTaken[l]) {
            // Edited, and the fresh detection has nothing under it to hand the edits to. Counted
            // rather than hidden: a re-analysis that quietly loses an edit is the exact failure this
            // whole model exists to prevent, and a silent zero here would be that failure.
            ++report.discarded;
        }
    }

    // ---- 4. the result ---------------------------------------------------------------------------
    SectionTimeline result;
    result.sections.reserve(pieces.size());
    for (Piece& p : pieces) {
        if (p.fromFresh) {
            ++report.sectionsReplaced;
        } else {
            report.kept.push_back(fmt::format("{} {} (span)", spanOf(p.section), nameOf(p.section)));
        }
        result.sections.push_back(std::move(p.section));
    }
    result.durationSeconds = std::max(fresh.durationSeconds,
                                      result.sections.empty()
                                          ? 0.0
                                          : result.sections.back().endSeconds);
    result.renumber();
    current = std::move(result);
    return report;
}

ReanalysisReport previewReanalysis(const SectionTimeline& current, const SectionTimeline& fresh,
                                   ReanalysisPolicy policy) {
    // Literally the call it predicts, run against a copy. Any other implementation is a second
    // algorithm that can come to disagree with the first.
    SectionTimeline scratch = current;
    return reanalyze(scratch, fresh, policy);
}

} // namespace avgen::song
