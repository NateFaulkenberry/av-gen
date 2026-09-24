#include "directing/time_ref.hpp"

#include "analysis/structure.hpp"
#include "directing/text.hpp"
#include "seq/sequence.hpp"
#include "song/section_timeline.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>

namespace avgen::directing {
namespace {

Issue malformed(std::string_view text, std::string_view location, std::string cause) {
    Issue issue;
    issue.severity = Severity::Error;
    issue.code = IssueCode::MalformedTime;
    issue.subject = std::string(text);
    issue.location = std::string(location);
    issue.message = fmt::format("'{}' is not a time the Director understands", text);
    issue.cause = std::move(cause);
    issue.suggestions = {"a clock time: 1:30 or 90s", "a bar: bar 64 beat 3",
                         "a section: chorus 2, the second chorus, end of the bridge"};
    return issue;
}

int ordinal(const std::string& word) {
    static const std::vector<std::string> kWords = {"first", "second", "third", "fourth", "fifth",
                                                    "sixth", "seventh", "eighth", "ninth", "tenth"};
    for (std::size_t i = 0; i < kWords.size(); ++i) {
        if (word == kWords[i]) {
            return static_cast<int>(i) + 1;
        }
    }
    if (word == "last" || word == "final") {
        return -1;
    }
    static const std::regex kNth(R"(^(\d+)(st|nd|rd|th)$)");
    std::smatch m;
    if (std::regex_match(word, m, kNth)) {
        return std::stoi(m[1].str());
    }
    return 0;
}

double parseNumber(const std::string& s) { return std::stod(s); }

} // namespace

std::string normaliseSectionType(std::string_view text) {
    std::string out;
    bool pendingBreak = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (std::isalnum(c) != 0) {
            // camelCase is a word boundary: the analyser's "preChorus" and a person's "pre-chorus"
            // must name the same thing.
            const bool camel = std::isupper(c) != 0 && i > 0 && std::islower(static_cast<unsigned char>(text[i - 1])) != 0;
            if ((pendingBreak || camel) && !out.empty()) {
                out.push_back('_');
            }
            pendingBreak = false;
            out.push_back(static_cast<char>(std::tolower(c)));
        } else {
            pendingBreak = true;
        }
    }
    return out;
}

// ---- JSON -------------------------------------------------------------------------------------

nlohmann::json TimeRef::toJson() const {
    nlohmann::json j;
    switch (kind) {
    case Kind::Seconds: j["seconds"] = seconds; break;
    case Kind::Bar:
        j["bar"] = bar;
        j["beat"] = beat;
        break;
    case Kind::Section:
        j["section"] = section;
        if (occurrence != 0) {
            j["occurrence"] = occurrence;
        }
        j["anchor"] = anchor == Anchor::End ? "end" : "start";
        break;
    }
    if (offsetSeconds != 0.0) {
        j["offsetSeconds"] = offsetSeconds;
    }
    if (!text.empty()) {
        j["text"] = text;
    }
    return j;
}

std::optional<TimeRef> TimeRef::fromJson(const nlohmann::json& j, std::string_view location,
                                         std::vector<Issue>& issues) {
    const auto invalid = [&](std::string why) {
        Issue issue;
        issue.code = IssueCode::SchemaInvalid;
        issue.location = std::string(location);
        issue.message = "not a time reference: " + why;
        issue.suggestions = {R"({"seconds": 90})", R"({"bar": 64, "beat": 3})",
                             R"({"section": "chorus", "occurrence": 2, "anchor": "start"})", R"({"text": "1:30"})"};
        issues.push_back(std::move(issue));
        return std::nullopt;
    };
    if (j.is_string()) {
        // Shorthand: a bare string is text to parse. What a model writes most naturally.
        auto parsed = parseTime(j.get<std::string>(), issues, location);
        return parsed;
    }
    if (!j.is_object()) {
        return invalid("expected an object or a string");
    }
    const int forms = (j.contains("seconds") ? 1 : 0) + (j.contains("bar") ? 1 : 0) + (j.contains("section") ? 1 : 0);
    if (forms > 1) {
        return invalid("give exactly one of seconds, bar or section");
    }
    TimeRef t;
    try {
        if (forms == 0) {
            if (!j.contains("text") || !j["text"].is_string()) {
                return invalid("give one of seconds, bar, section or text");
            }
            auto parsed = parseTime(j["text"].get<std::string>(), issues, location);
            if (parsed && j.contains("offsetSeconds")) {
                parsed->offsetSeconds += j["offsetSeconds"].get<double>();
            }
            return parsed;
        }
        if (j.contains("seconds")) {
            t.kind = Kind::Seconds;
            t.seconds = j.at("seconds").get<double>();
        } else if (j.contains("bar")) {
            t.kind = Kind::Bar;
            t.bar = j.at("bar").get<int>();
            t.beat = j.value("beat", 1);
        } else {
            t.kind = Kind::Section;
            t.section = normaliseSectionType(j.at("section").get<std::string>());
            t.occurrence = j.value("occurrence", 0);
            const std::string anchor = j.value("anchor", std::string("start"));
            if (anchor != "start" && anchor != "end") {
                return invalid("anchor must be \"start\" or \"end\"");
            }
            t.anchor = anchor == "end" ? Anchor::End : Anchor::Start;
        }
        t.offsetSeconds = j.value("offsetSeconds", 0.0);
        t.text = j.value("text", std::string{});
    } catch (const nlohmann::json::exception& e) {
        return invalid(e.what());
    }
    if (t.kind == Kind::Bar && (t.bar < 1 || t.beat < 1)) {
        return invalid("bars and beats count from 1");
    }
    if (t.kind == Kind::Section && t.section.empty()) {
        return invalid("a section needs a type");
    }
    return t;
}

std::string TimeRef::describe() const {
    std::string base;
    switch (kind) {
    case Kind::Seconds: {
        const double s = std::max(seconds, 0.0);
        const auto minutes = static_cast<int>(s / 60.0);
        base = fmt::format("{:02d}:{:06.3f}", minutes, s - (minutes * 60.0));
        break;
    }
    case Kind::Bar: base = fmt::format("bar {} beat {}", bar, beat); break;
    case Kind::Section: {
        const std::string which = occurrence == 0    ? section
                                  : occurrence == -1 ? "last " + section
                                                     : fmt::format("{} {}", section, occurrence);
        base = fmt::format("{} of {}", anchor == Anchor::End ? "end" : "start", which);
        break;
    }
    }
    if (offsetSeconds != 0.0) {
        base += fmt::format(" {} {:.3f}s", offsetSeconds > 0.0 ? "+" : "-", std::abs(offsetSeconds));
    }
    return base;
}

// ---- parsing ----------------------------------------------------------------------------------

std::optional<TimeRef> parseTime(std::string_view raw, std::vector<Issue>& issues, std::string_view location) {
    std::string s = text::lower(text::trim(raw));
    TimeRef t;
    t.text = text::trim(raw);
    if (s.empty()) {
        issues.push_back(malformed(raw, location, "it is empty"));
        return std::nullopt;
    }

    // A trailing offset: "chorus 2 + 1.5s", "1:30 - 0.25 s".
    static const std::regex kOffset(R"(^(.*\S)\s*([+-])\s*(\d+(?:\.\d+)?)\s*(?:s|sec|secs|seconds?)?$)");
    std::smatch m;
    if (std::regex_match(s, m, kOffset)) {
        const double amount = parseNumber(m[3].str());
        t.offsetSeconds = m[2].str() == "-" ? -amount : amount;
        s = m[1].str();
    }

    static const std::regex kClock(R"(^(\d+):(\d{1,2})(?::(\d{1,2}))?(\.\d+)?$)");
    static const std::regex kSeconds(R"(^(\d+(?:\.\d+)?)\s*(?:s|sec|secs|seconds?)?$)");
    static const std::regex kBar(R"(^bar\s*(\d+)(?:\s*,?\s*beat\s*(\d+))?$)");
    static const std::regex kBeatOnly(R"(^beat\s*\d+$)");
    if (std::regex_match(s, m, kClock)) {
        const double a = parseNumber(m[1].str());
        const double b = parseNumber(m[2].str());
        const double frac = m[4].matched ? parseNumber("0" + m[4].str()) : 0.0;
        double total = 0.0;
        if (m[3].matched) {
            const double c = parseNumber(m[3].str());
            if (b >= 60.0 || c >= 60.0) {
                issues.push_back(malformed(raw, location, "minutes and seconds run to 59"));
                return std::nullopt;
            }
            total = (a * 3600.0) + (b * 60.0) + c;
        } else {
            if (b >= 60.0) {
                issues.push_back(malformed(raw, location, "seconds run to 59; 1:75 is not a time"));
                return std::nullopt;
            }
            total = (a * 60.0) + b;
        }
        t.kind = TimeRef::Kind::Seconds;
        t.seconds = total + frac;
        return t;
    }
    if (std::regex_match(s, m, kSeconds)) {
        t.kind = TimeRef::Kind::Seconds;
        t.seconds = parseNumber(m[1].str());
        return t;
    }
    if (std::regex_match(s, m, kBar)) {
        t.kind = TimeRef::Kind::Bar;
        t.bar = std::stoi(m[1].str());
        t.beat = m[2].matched ? std::stoi(m[2].str()) : 1;
        if (t.bar < 1 || t.beat < 1) {
            issues.push_back(malformed(raw, location, "bars and beats count from 1"));
            return std::nullopt;
        }
        return t;
    }
    if (std::regex_match(s, kBeatOnly)) {
        Issue issue = malformed(raw, location, "a beat needs its bar");
        issue.suggestions = {"bar N beat 3"};
        issues.push_back(std::move(issue));
        return std::nullopt;
    }

    // ---- a section --------------------------------------------------------------------------------
    std::vector<std::string> w = text::words(s);
    if (w.empty()) {
        issues.push_back(malformed(raw, location, "it has no words or numbers"));
        return std::nullopt;
    }
    t.kind = TimeRef::Kind::Section;
    // "start of", "beginning of", "end of" -- and the trailing forms "chorus 2 start".
    if (w.size() >= 2 && (w[0] == "start" || w[0] == "beginning" || w[0] == "end") && w[1] == "of") {
        t.anchor = w[0] == "end" ? TimeRef::Anchor::End : TimeRef::Anchor::Start;
        w.erase(w.begin(), w.begin() + 2);
    } else if (!w.empty() && (w.back() == "start" || w.back() == "end")) {
        t.anchor = w.back() == "end" ? TimeRef::Anchor::End : TimeRef::Anchor::Start;
        w.pop_back();
    }
    if (!w.empty() && w.front() == "the") {
        w.erase(w.begin());
    }
    if (!w.empty()) {
        if (const int o = ordinal(w.front()); o != 0) {
            t.occurrence = o;
            w.erase(w.begin());
        }
    }
    if (!w.empty() && t.occurrence == 0 && std::all_of(w.back().begin(), w.back().end(), ::isdigit)) {
        t.occurrence = std::stoi(w.back());
        w.pop_back();
    }
    for (const std::string& word : w) {
        if (std::any_of(word.begin(), word.end(), ::isdigit)) {
            issues.push_back(malformed(raw, location, fmt::format("'{}' is neither a section name nor a count", word)));
            return std::nullopt;
        }
    }
    if (w.empty()) {
        issues.push_back(malformed(raw, location, "it names no section"));
        return std::nullopt;
    }
    std::string joined;
    for (const std::string& word : w) {
        joined += (joined.empty() ? "" : "_") + word;
    }
    t.section = normaliseSectionType(joined);
    return t;
}

// ---- context ----------------------------------------------------------------------------------

MusicalContext musicalContextFrom(const seq::Sequence& sequence, std::span<const double> beatTimes, double tempoBpm,
                                  double durationSeconds) {
    MusicalContext ctx;
    ctx.beatTimes.assign(beatTimes.begin(), beatTimes.end());
    ctx.tempoBpm = tempoBpm > 0.0 ? tempoBpm : static_cast<double>(sequence.structure.tempoBpm);
    ctx.firstBeatSeconds = ctx.beatTimes.empty() ? 0.0 : ctx.beatTimes.front();
    ctx.durationSeconds = durationSeconds;

    std::vector<SectionRun> raw;
    if (!sequence.sectionTimeline.sections.empty()) {
        ctx.sectionSource = "sectionTimeline";
        for (const song::Section& s : sequence.sectionTimeline.sections) {
            raw.push_back(SectionRun{normaliseSectionType(s.type), s.label, s.startSeconds, s.endSeconds, 1});
        }
    } else if (!sequence.structure.sections.empty()) {
        ctx.sectionSource = "structure";
        for (const analysis::SongSection& s : sequence.structure.sections) {
            raw.push_back(SectionRun{normaliseSectionType(analysis::sectionFunctionName(s.function)), s.label,
                                     s.startSeconds, s.endSeconds, 1});
        }
    }
    std::sort(raw.begin(), raw.end(), [](const SectionRun& a, const SectionRun& b) { return a.startSeconds < b.startSeconds; });
    for (SectionRun& run : raw) {
        if (!ctx.sections.empty() && ctx.sections.back().type == run.type &&
            std::abs(ctx.sections.back().endSeconds - run.startSeconds) < 1e-3) {
            ctx.sections.back().endSeconds = run.endSeconds; // one passage, several entries
            continue;
        }
        ctx.sections.push_back(std::move(run));
    }
    std::vector<std::pair<std::string, int>> counts;
    for (SectionRun& run : ctx.sections) {
        auto it = std::find_if(counts.begin(), counts.end(), [&](const auto& c) { return c.first == run.type; });
        if (it == counts.end()) {
            counts.emplace_back(run.type, 1);
            run.occurrence = 1;
        } else {
            run.occurrence = ++it->second;
        }
    }
    if (ctx.durationSeconds <= 0.0 && !ctx.sections.empty()) {
        ctx.durationSeconds = ctx.sections.back().endSeconds;
    }
    return ctx;
}

// ---- resolution -------------------------------------------------------------------------------

TimeResolution resolveTime(const TimeRef& ref, const MusicalContext& ctx, std::string_view location) {
    TimeResolution out;
    const auto issue = [&](Severity severity, IssueCode code, std::string message) -> Issue& {
        Issue i;
        i.severity = severity;
        i.code = code;
        i.subject = ref.text.empty() ? ref.describe() : ref.text;
        i.location = std::string(location);
        i.message = std::move(message);
        out.issues.push_back(std::move(i));
        return out.issues.back();
    };

    double seconds = 0.0;
    switch (ref.kind) {
    case TimeRef::Kind::Seconds: seconds = ref.seconds; break;
    case TimeRef::Kind::Bar: {
        if (ref.beat > ctx.beatsPerBar) {
            Issue& i = issue(Severity::Error, IssueCode::MalformedTime,
                             fmt::format("a bar has {} beats here, so there is no beat {}", ctx.beatsPerBar, ref.beat));
            i.suggestions = {fmt::format("bar {} beat {}", ref.bar, ctx.beatsPerBar)};
            return out;
        }
        const auto index = static_cast<std::size_t>((ref.bar - 1) * ctx.beatsPerBar + (ref.beat - 1));
        if (index < ctx.beatTimes.size()) {
            seconds = ctx.beatTimes[index];
        } else if (ctx.tempoBpm > 0.0) {
            seconds = ctx.firstBeatSeconds + (static_cast<double>(index) * 60.0 / ctx.tempoBpm);
            Issue& i = issue(Severity::Warning, IssueCode::UnresolvableTime,
                             fmt::format("placed on a constant {:.2f} BPM grid: the analysed beat grid {}", ctx.tempoBpm,
                                         ctx.beatTimes.empty() ? "is missing" : "ends before this bar"));
            i.details = {{"tempoBpm", ctx.tempoBpm}, {"analysedBeats", ctx.beatTimes.size()}};
        } else {
            Issue& i = issue(Severity::Error, IssueCode::UnresolvableTime,
                             "this piece has no beat grid and no tempo, so bars cannot be placed");
            i.suggestions = {"analyse the audio", "give the time in seconds"};
            return out;
        }
        break;
    }
    case TimeRef::Kind::Section: {
        std::vector<const SectionRun*> runs;
        std::vector<std::string> types;
        for (const SectionRun& run : ctx.sections) {
            if (std::find(types.begin(), types.end(), run.type) == types.end()) {
                types.push_back(run.type);
            }
            if (run.type == ref.section) {
                runs.push_back(&run);
            }
        }
        if (ctx.sections.empty()) {
            Issue& i = issue(Severity::Error, IssueCode::UnresolvableTime, "this piece has no sections to place a time by");
            i.suggestions = {"analyse the song structure", "give the time in seconds"};
            return out;
        }
        if (runs.empty()) {
            Issue& i = issue(Severity::Error, IssueCode::UnresolvableTime,
                             fmt::format("this piece has no '{}' section", ref.section));
            i.details = {{"available", types}, {"source", ctx.sectionSource}};
            i.suggestions = text::nearest(ref.section, types);
            if (i.suggestions.empty()) {
                i.suggestions = types;
            }
            return out;
        }
        const SectionRun* chosen = nullptr;
        if (ref.occurrence == -1) {
            chosen = runs.back();
        } else if (ref.occurrence == 0) {
            if (runs.size() > 1) {
                Issue& i = issue(Severity::Error, IssueCode::AmbiguousTime,
                                 fmt::format("this piece has {} '{}' sections; say which", runs.size(), ref.section));
                nlohmann::json candidates = nlohmann::json::array();
                for (const SectionRun* r : runs) {
                    candidates.push_back({{"occurrence", r->occurrence}, {"start", r->startSeconds}, {"end", r->endSeconds}});
                    i.suggestions.push_back(fmt::format("{} {}", ref.section, r->occurrence));
                }
                i.details = {{"candidates", std::move(candidates)}, {"source", ctx.sectionSource}};
                return out;
            }
            chosen = runs.front();
        } else if (ref.occurrence > static_cast<int>(runs.size())) {
            Issue& i = issue(Severity::Error, IssueCode::UnresolvableTime,
                             fmt::format("this piece has only {} '{}' section{}", runs.size(), ref.section,
                                         runs.size() == 1 ? "" : "s"));
            i.suggestions = {fmt::format("last {}", ref.section)};
            return out;
        } else {
            chosen = runs[static_cast<std::size_t>(ref.occurrence - 1)];
        }
        seconds = ref.anchor == TimeRef::Anchor::End ? chosen->endSeconds : chosen->startSeconds;
        out.explanation = fmt::format("{} {} runs {:.2f}-{:.2f}s (from the {})", chosen->type, chosen->occurrence,
                                      chosen->startSeconds, chosen->endSeconds, ctx.sectionSource);
        break;
    }
    }
    seconds += ref.offsetSeconds;
    if (seconds < 0.0 || (ctx.durationSeconds > 0.0 && seconds > ctx.durationSeconds + 1e-6)) {
        Issue& i = issue(Severity::Error, IssueCode::TimeOutOfRange,
                         fmt::format("{:.3f}s is outside the piece (0-{:.3f}s)", seconds, ctx.durationSeconds));
        i.details = {{"seconds", seconds}, {"duration", ctx.durationSeconds}};
        return out;
    }
    out.seconds = seconds;
    return out;
}

} // namespace avgen::directing
