// The slice tool's decision (ADR-355), and the four cuts it performs.
//
// Neither the author of this file nor the person who asked for the feature can see Dear ImGui, so
// the only way the tool is verifiable at all is that the decision is arithmetic: `ui::planSlice`
// answers which block, where the cut lands and whether it is legal, and the panel does nothing but
// hand it a pointer position and carry out the answer.
//
// **Every arm here has a control** (ADR-182). A snapping test that only ever moves the cut passes on
// an implementation that snaps everything to the same number; one that only refuses passes on an
// implementation that refuses everything. So the snap arms come in pairs -- one where the grid must
// move the cut and one where it must not -- and the legality arms come in pairs too.

#include "audio/arrangement.hpp"
#include "seq/sequence.hpp"
#include "ui/ui_logic.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

// Four shots, ten seconds each, butted edge to edge: the shape a cut-up film actually has, and the
// one where a half-open block test matters.
std::vector<ui::SliceBlock> fourBlocks() {
    return {{0.0, 10.0}, {10.0, 20.0}, {20.0, 30.0}, {30.0, 40.0}};
}

// A beat every half second to 40 s, which is 120 bpm.
std::vector<double> beatsAt120() {
    std::vector<double> beats;
    for (int i = 0; i <= 80; ++i) {
        beats.push_back(static_cast<double>(i) * 0.5);
    }
    return beats;
}

} // namespace

TEST_CASE("the slice tool finds the block under the pointer", "[ui][slice]") {
    const std::vector<ui::SliceBlock> blocks = fourBlocks();

    CHECK(ui::sliceBlockAt(blocks, 15.0) == 1);
    CHECK(ui::sliceBlockAt(blocks, 0.0) == 0);
    CHECK(ui::sliceBlockAt(blocks, 39.999) == 3);

    SECTION("a seam belongs to the block that starts there, not the one that ends there") {
        // Half-open. Butted edge to edge is the normal state of a shots lane, and a seam that
        // answered twice would make which block gets cut depend on loop order.
        CHECK(ui::sliceBlockAt(blocks, 10.0) == 1);
        CHECK(ui::sliceBlockAt(blocks, 20.0) == 2);
    }

    SECTION("the control: off the end of the lane there is nothing") {
        CHECK(ui::sliceBlockAt(blocks, 40.0) == -1);
        CHECK(ui::sliceBlockAt(blocks, -1.0) == -1);
        CHECK(ui::sliceBlockAt({}, 5.0) == -1);
    }
}

TEST_CASE("a slice is planned, or refused with a reason", "[ui][slice]") {
    const std::vector<ui::SliceBlock> blocks = fourBlocks();

    SECTION("the middle of a block is legal and the cut lands where the grid put it") {
        const ui::SlicePlan plan = ui::planSlice(ui::StripLane::Shots, blocks, 14.8, 15.0, 0.25);
        REQUIRE(plan.legal());
        CHECK(plan.kind == ui::SliceKind::Shot);
        CHECK(plan.index == 1);
        // The SNAPPED time, not the pointer's. This is the whole of the owner's "obey the active
        // snapping rules": the cut goes where the grid said, not where the hand was.
        CHECK(plan.atSeconds == Approx(15.0));
        CHECK_FALSE(plan.touchesAudio());
    }

    SECTION("nothing under the pointer") {
        const ui::SlicePlan plan = ui::planSlice(ui::StripLane::Shots, blocks, 55.0, 55.0, 0.25);
        CHECK_FALSE(plan.legal());
        CHECK(plan.refusal == ui::SliceRefusal::NothingUnderPointer);
    }

    SECTION("a lane with nothing sliceable in it") {
        // The ruler is a scrub and holds no spans. It refuses for a *different* reason than an empty
        // shots lane does, and the status line says so -- "nothing in this lane" and "nothing under
        // the pointer" are different mistakes.
        const ui::SlicePlan ruler = ui::planSlice(ui::StripLane::Ruler, blocks, 15.0, 15.0, 0.25);
        CHECK(ruler.refusal == ui::SliceRefusal::LaneNotSliceable);
        CHECK(ruler.kind == ui::SliceKind::None);
    }

    SECTION("too close to an edge, both ends") {
        CHECK(ui::planSlice(ui::StripLane::Shots, blocks, 10.1, 10.1, 0.25).refusal ==
              ui::SliceRefusal::WouldBeTooShort);
        CHECK(ui::planSlice(ui::StripLane::Shots, blocks, 19.9, 19.9, 0.25).refusal ==
              ui::SliceRefusal::WouldBeTooShort);

        // The control, and it is the one that matters: a hair further in and the identical gesture
        // must be allowed. Without it this arm passes on a planner that refuses everything.
        CHECK(ui::planSlice(ui::StripLane::Shots, blocks, 10.3, 10.3, 0.25).legal());
        CHECK(ui::planSlice(ui::StripLane::Shots, blocks, 19.7, 19.7, 0.25).legal());
    }

    SECTION("an actor lane with no row under the pointer cannot be sliced") {
        CHECK(ui::planSlice(ui::StripLane::Actors, blocks, 15.0, 15.0, 0.25, -1).refusal ==
              ui::SliceRefusal::NothingUnderPointer);
        // The control: the same click on a real row is fine.
        const ui::SlicePlan onRow = ui::planSlice(ui::StripLane::Actors, blocks, 15.0, 15.0, 0.25, 2);
        CHECK(onRow.legal());
        CHECK(onRow.actorRow == 2);
        CHECK(onRow.kind == ui::SliceKind::ActorClip);
    }
}

TEST_CASE("the block is found at the pointer and the cut is tested against that block", "[ui][slice]") {
    // The failure this exists to prevent: with a coarse grid on, the snapped time can land past the
    // end of the block that was clicked and inside the next one. Slicing the block the person did not
    // point at is worse than refusing, so the planner refuses.
    const std::vector<ui::SliceBlock> blocks = fourBlocks();

    const ui::SlicePlan overshot = ui::planSlice(ui::StripLane::Shots, blocks,
                                                 /*rawSeconds=*/19.6, /*snappedSeconds=*/20.5, 0.25);
    CHECK(overshot.index == 1); // the block the pointer was in
    CHECK_FALSE(overshot.legal());
    CHECK(overshot.refusal == ui::SliceRefusal::WouldBeTooShort);

    // The control: the same pointer, snapped to a time that stays inside the block it pointed at.
    const ui::SlicePlan inside = ui::planSlice(ui::StripLane::Shots, blocks, 19.6, 19.5, 0.25);
    CHECK(inside.index == 1);
    CHECK(inside.legal());
    CHECK(inside.atSeconds == Approx(19.5));
}

TEST_CASE("only the audio lane says it touched the audio", "[ui][slice]") {
    // `TimelineChange::clipsTouched` makes an undo record re-open every source and re-mix the whole
    // piece. A shot slice that claimed to touch the audio would pay for a pass over every sample to
    // discover nothing had changed, and a clip slice that did not claim it would leave the mix stale.
    const std::vector<ui::SliceBlock> blocks = fourBlocks();
    const auto plan = [&](ui::StripLane lane) {
        return ui::planSlice(lane, blocks, 15.0, 15.0, 0.25, 0);
    };
    CHECK(plan(ui::StripLane::Audio).touchesAudio());
    // Every other lane, as a control, rather than one of them.
    CHECK_FALSE(plan(ui::StripLane::Shots).touchesAudio());
    CHECK_FALSE(plan(ui::StripLane::Sections).touchesAudio());
    CHECK_FALSE(plan(ui::StripLane::Overlays).touchesAudio());
    CHECK_FALSE(plan(ui::StripLane::Actors).touchesAudio());
    CHECK_FALSE(plan(ui::StripLane::Ruler).touchesAudio());
}

TEST_CASE("a cut lands on the grid the thing it creates is dragged on", "[ui][slice]") {
    // The strip has two snaps on purpose. This is the decision about which one a slice takes, and it
    // is here rather than only in a comment because it is the one judgement call in the feature.
    CHECK(ui::sliceGridFor(ui::StripLane::Sections) == ui::SliceGrid::Section);
    // The control: every other lane takes the strip's grid, including the lanes added later.
    CHECK(ui::sliceGridFor(ui::StripLane::Shots) == ui::SliceGrid::Strip);
    CHECK(ui::sliceGridFor(ui::StripLane::Audio) == ui::SliceGrid::Strip);
    CHECK(ui::sliceGridFor(ui::StripLane::Overlays) == ui::SliceGrid::Strip);
    CHECK(ui::sliceGridFor(ui::StripLane::Actors) == ui::SliceGrid::Strip);
}

TEST_CASE("snapping moves the cut, and with snapping off it does not", "[ui][slice]") {
    // The two arms ADR-182 asks for, composed of the same two pieces the panel composes: the lane's
    // grid choice, and `seq::snapTime`. The panel's own `snap`/`snapSection` need an `app::Engine`
    // and so cannot be reached from here; what they do to a time is exactly this.
    const std::vector<double> beats = beatsAt120();
    const std::vector<ui::SliceBlock> blocks = fourBlocks();
    const double pointer = 14.83; // between the beats at 14.5 and 15.0, nearer 15.0

    SECTION("beats on: the cut moves to the beat") {
        const double snapped = seq::snapTime(pointer, seq::SnapMode::Beats, beats);
        REQUIRE(snapped == Approx(15.0));
        const ui::SlicePlan plan = ui::planSlice(ui::StripLane::Shots, blocks, pointer, snapped, 0.25);
        REQUIRE(plan.legal());
        CHECK(plan.atSeconds == Approx(15.0));
        // And it really moved: the arm is worthless if the pointer was already on the grid.
        CHECK(plan.atSeconds != Approx(pointer));
    }

    SECTION("the control -- snapping off: the cut lands exactly where the pointer was") {
        const double snapped = seq::snapTime(pointer, seq::SnapMode::Off, beats);
        REQUIRE(snapped == Approx(pointer));
        const ui::SlicePlan plan = ui::planSlice(ui::StripLane::Shots, blocks, pointer, snapped, 0.25);
        REQUIRE(plan.legal());
        CHECK(plan.atSeconds == Approx(pointer));
    }

    SECTION("the section lane's bar grid moves the cut further than the beat grid does") {
        // Bars are every fourth beat, which `snapSection` derives and `BakeOptions::beatsPerBar`
        // states. At 120 bpm that is a bar every two seconds.
        std::vector<double> bars;
        for (std::size_t i = 0; i < beats.size(); i += 4) {
            bars.push_back(beats[i]);
        }
        const double beat = seq::snapTime(14.83, seq::SnapMode::Beats, beats);
        const double bar = seq::snapTime(14.83, seq::SnapMode::Beats, bars);
        CHECK(beat == Approx(15.0));
        CHECK(bar == Approx(14.0));
        // The two grids genuinely disagree here, which is what makes `sliceGridFor` a decision
        // rather than a formality.
        CHECK(beat != Approx(bar));
    }
}

TEST_CASE("slicing a lyric", "[seq][slice]") {
    std::vector<seq::OverlayCue> overlays;
    seq::OverlayCue cue;
    cue.id = "line1";
    cue.content = "the whole line";
    cue.startSeconds = 4.0;
    cue.endSeconds = 8.0;
    overlays.push_back(cue);

    const auto made = seq::splitOverlay(overlays, 0, 6.0);
    REQUIRE(made.has_value());
    CHECK(*made == 1);
    REQUIRE(overlays.size() == 2);
    CHECK(overlays[0].startSeconds == Approx(4.0));
    CHECK(overlays[0].endSeconds == Approx(6.0));
    CHECK(overlays[1].startSeconds == Approx(6.0));
    CHECK(overlays[1].endSeconds == Approx(8.0));
    // The two halves cover the original exactly, and neither id is the other's.
    CHECK(overlays[0].id != overlays[1].id);
    CHECK(overlays[1].content == "the whole line");

    SECTION("the control: a cut that would leave nothing is refused and changes nothing") {
        std::vector<seq::OverlayCue> fresh{cue};
        CHECK_FALSE(seq::splitOverlay(fresh, 0, 4.1).has_value());
        CHECK_FALSE(seq::splitOverlay(fresh, 0, 7.9).has_value());
        CHECK_FALSE(seq::splitOverlay(fresh, 9, 6.0).has_value());
        CHECK(fresh.size() == 1);
        CHECK(fresh[0].endSeconds == Approx(8.0));
    }
}

TEST_CASE("slicing an actor's clip cue", "[seq][slice]") {
    seq::Actor actor;
    actor.id = "dancer";
    actor.clips.push_back({.timeSeconds = 0.0, .clip = "idle"});
    actor.clips.push_back({.timeSeconds = 10.0, .clip = "run"});

    SECTION("a middle span splits at the next cue, not at the end of the piece") {
        const auto made = seq::splitActorClip(actor, 0, 4.0, /*endOfPiece=*/30.0);
        REQUIRE(made.has_value());
        CHECK(*made == 1);
        REQUIRE(actor.clips.size() == 3);
        CHECK(actor.clips[1].timeSeconds == Approx(4.0));
        CHECK(actor.clips[1].clip == "idle"); // the same animation, now in two spans
        CHECK(actor.clips[2].timeSeconds == Approx(10.0));
    }

    SECTION("the LAST span runs to the end of the piece, which is why it is passed in") {
        const auto made = seq::splitActorClip(actor, 1, 20.0, /*endOfPiece=*/30.0);
        REQUIRE(made.has_value());
        REQUIRE(actor.clips.size() == 3);
        CHECK(actor.clips[2].timeSeconds == Approx(20.0));
        CHECK(actor.clips[2].clip == "run");
    }

    SECTION("the control: the same cut, on a piece that ends before it") {
        // Identical arguments but for `endOfPiece`. A planner that ignored the span's end would
        // accept this, and the cue it inserted would be past the end of everything.
        seq::Actor fresh = actor;
        CHECK_FALSE(seq::splitActorClip(fresh, 1, 20.0, /*endOfPiece=*/12.0).has_value());
        CHECK(fresh.clips.size() == 2);
    }

    SECTION("the control: too close to either edge of the span") {
        seq::Actor fresh = actor;
        CHECK_FALSE(seq::splitActorClip(fresh, 0, 0.1, 30.0).has_value());
        CHECK_FALSE(seq::splitActorClip(fresh, 0, 9.9, 30.0).has_value());
        CHECK(fresh.clips.size() == 2);
    }
}

TEST_CASE("slicing an audio clip moves the window into the file, not just the timeline",
          "[audio][slice]") {
    // The arithmetic this primitive exists for. A clip is a window onto a file: cutting it at a
    // timeline second has to advance `inSeconds` by the same amount it advances `startSeconds`, or
    // the later half plays the wrong part of the take -- which sounds like a skip, not like a cut.
    std::vector<audio::AudioClip> clips;
    audio::AudioClip clip;
    clip.file = "take.wav";
    clip.startSeconds = 10.0;
    clip.inSeconds = 2.0;   // the clip already starts two seconds into the file
    clip.durationSeconds = 8.0;
    clip.fadeInSeconds = 0.5;
    clip.fadeOutSeconds = 0.75;
    clip.name = "take";
    clips.push_back(clip);

    const auto made = audio::splitClip(clips, 0, /*seconds=*/13.0, /*endSeconds=*/18.0);
    REQUIRE(made.has_value());
    CHECK(*made == 1);
    REQUIRE(clips.size() == 2);

    CHECK(clips[0].startSeconds == Approx(10.0));
    CHECK(clips[0].inSeconds == Approx(2.0));
    CHECK(clips[0].durationSeconds == Approx(3.0));
    CHECK(clips[1].startSeconds == Approx(13.0));
    // 2.0 + the three seconds of the head. This is the line the whole primitive is for.
    CHECK(clips[1].inSeconds == Approx(5.0));
    CHECK(clips[1].durationSeconds == Approx(5.0));

    SECTION("the two halves still cover exactly what the one clip did") {
        CHECK(clips[0].durationSeconds + clips[1].durationSeconds == Approx(8.0));
        CHECK(clips[1].startSeconds + clips[1].durationSeconds == Approx(18.0));
    }

    SECTION("the fades stay at the outer edges and no new one appears at the seam") {
        CHECK(clips[0].fadeInSeconds == Approx(0.5));
        CHECK(clips[0].fadeOutSeconds == Approx(0.0));
        CHECK(clips[1].fadeInSeconds == Approx(0.0));
        CHECK(clips[1].fadeOutSeconds == Approx(0.75));
    }
}

TEST_CASE("an open-ended audio clip stays open when it is sliced", "[audio][slice]") {
    // `durationSeconds == 0` means "the rest of the file". The later half has to inherit that rather
    // than being frozen at whatever length the file happened to be when the cut was made.
    std::vector<audio::AudioClip> clips;
    audio::AudioClip clip;
    clip.file = "song.wav";
    clip.startSeconds = 0.0;
    clip.durationSeconds = 0.0;
    clips.push_back(clip);

    const auto made = audio::splitClip(clips, 0, /*seconds=*/30.0, /*endSeconds=*/180.0);
    REQUIRE(made.has_value());
    REQUIRE(clips.size() == 2);
    CHECK(clips[0].durationSeconds == Approx(30.0)); // the head is now bounded
    CHECK(clips[1].durationSeconds == Approx(0.0));  // and the tail still follows the file
    CHECK(clips[1].inSeconds == Approx(30.0));

    SECTION("the control: refused near either edge, and the list is untouched") {
        std::vector<audio::AudioClip> fresh{clip};
        CHECK_FALSE(audio::splitClip(fresh, 0, 0.1, 180.0).has_value());
        CHECK_FALSE(audio::splitClip(fresh, 0, 179.9, 180.0).has_value());
        CHECK_FALSE(audio::splitClip(fresh, 5, 30.0, 180.0).has_value());
        CHECK(fresh.size() == 1);
        CHECK(fresh[0].durationSeconds == Approx(0.0));
    }
}

TEST_CASE("every refusal has something to say", "[ui][slice]") {
    // ADR-355's no-silent-no-op rule, as a test. A cut that cannot happen must put a line in the
    // status strip; an empty string here is a gesture that looks like a broken tool.
    for (const ui::SliceRefusal refusal : {ui::SliceRefusal::LaneNotSliceable,
                                           ui::SliceRefusal::NothingUnderPointer,
                                           ui::SliceRefusal::WouldBeTooShort}) {
        INFO("refusal " << static_cast<int>(refusal));
        CHECK(std::string_view(ui::sliceRefusalMessage(refusal)).size() > 0);
    }
    // The control: the one value that is NOT a refusal says nothing, because nothing went wrong.
    CHECK(std::string_view(ui::sliceRefusalMessage(ui::SliceRefusal::None)).empty());

    // And every kind the tool can cut names itself, for the line it writes when it succeeds.
    for (const ui::SliceKind kind : {ui::SliceKind::Shot, ui::SliceKind::Section,
                                     ui::SliceKind::AudioClip, ui::SliceKind::Overlay,
                                     ui::SliceKind::ActorClip}) {
        INFO("kind " << static_cast<int>(kind));
        CHECK(std::string_view(ui::sliceKindName(kind)).size() > 0);
    }
    CHECK(std::string_view(ui::sliceKindName(ui::SliceKind::None)).empty());
}
