// The Shots lane's gestures, as the model performs them (seq/sequence.hpp).
//
// The Song Director audit recorded this as a gap: add, move, trim, split, duplicate and delete were
// each implemented and none was covered by a test, because each lived inline in `SequencePanel` and
// could not be reached without ImGui. That is the same shape of hole that let `songPlanFromCues` sit
// correct, unit-tested and called by nothing.
//
// They are `seq::` functions now and the panel calls them, so what a drag does and what this file
// checks are the same code rather than two spellings of it that can drift apart.

#include "seq/sequence.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

std::vector<seq::Shot> threeShots() {
    std::vector<seq::Shot> shots;
    for (int i = 0; i < 3; ++i) {
        seq::Shot shot;
        shot.name = fmt::format("Shot {:02d}", i + 1);
        shot.startSeconds = i * 10.0;
        shot.durationSeconds = 10.0;
        shots.push_back(std::move(shot));
    }
    return shots;
}

} // namespace

TEST_CASE("moving a shot keeps its length", "[seq][shot][editing]") {
    seq::Shot shot;
    shot.startSeconds = 10.0;
    shot.durationSeconds = 6.0;

    seq::moveShot(shot, 25.0);
    CHECK(shot.startSeconds == Approx(25.0));
    CHECK(shot.durationSeconds == Approx(6.0));   // a move is not a resize
    CHECK(shot.endSeconds() == Approx(31.0));

    // Dragged off the front of the piece, it stops at zero rather than going negative.
    seq::moveShot(shot, -5.0);
    CHECK(shot.startSeconds == Approx(0.0));
    CHECK(shot.durationSeconds == Approx(6.0));
}

TEST_CASE("trimming moves one edge and holds the other", "[seq][shot][editing]") {
    SECTION("the end moves, the start does not") {
        seq::Shot shot;
        shot.startSeconds = 10.0;
        shot.durationSeconds = 6.0;
        seq::trimShotEnd(shot, 20.0);
        CHECK(shot.startSeconds == Approx(10.0));
        CHECK(shot.endSeconds() == Approx(20.0));
    }

    SECTION("dragging the end back past the start leaves a shot, not a negative one") {
        seq::Shot shot;
        shot.startSeconds = 10.0;
        shot.durationSeconds = 6.0;
        seq::trimShotEnd(shot, 2.0);
        CHECK(shot.durationSeconds >= seq::kMinShotSeconds);
        CHECK(shot.endSeconds() > shot.startSeconds);
    }

    SECTION("the start moves and the END STAYS PUT") {
        // This is the assertion the gesture exists for. A duration recomputed from a live end would
        // move both edges at once, turning a trim into a move -- which is why `trimShotStart` takes
        // the end as an argument rather than reading it back from the shot it is changing.
        seq::Shot shot;
        shot.startSeconds = 10.0;
        shot.durationSeconds = 6.0;
        const double end = shot.endSeconds();   // what the panel remembers when the drag begins
        seq::trimShotStart(shot, 13.0, end);
        CHECK(shot.startSeconds == Approx(13.0));
        CHECK(shot.endSeconds() == Approx(end));
        CHECK(shot.durationSeconds == Approx(3.0));
    }

    SECTION("trimming the start through the end is refused, not allowed to invert") {
        seq::Shot shot;
        shot.startSeconds = 10.0;
        shot.durationSeconds = 6.0;
        const double end = shot.endSeconds();
        seq::trimShotStart(shot, 99.0, end);
        CHECK(shot.durationSeconds == Approx(seq::kMinShotSeconds));
        CHECK(shot.endSeconds() == Approx(end));
    }
}

TEST_CASE("splitting makes two shots whose lengths add up", "[seq][shot][editing]") {
    std::vector<seq::Shot> shots = threeShots();
    const double originalEnd = shots[1].endSeconds();

    const auto made = seq::splitShot(shots, 1, 14.0);
    REQUIRE(made.has_value());
    CHECK(*made == 2);
    REQUIRE(shots.size() == 4);

    // No time is created or lost: the pair covers exactly what the one shot did.
    CHECK(shots[1].startSeconds == Approx(10.0));
    CHECK(shots[1].endSeconds() == Approx(14.0));
    CHECK(shots[2].startSeconds == Approx(14.0));
    CHECK(shots[2].endSeconds() == Approx(originalEnd));
    CHECK(shots[1].durationSeconds + shots[2].durationSeconds == Approx(10.0));

    // The new seam is a hard cut on both sides. A split inheriting the original's outgoing
    // transition would dissolve in the middle of what used to be one continuous shot.
    CHECK(shots[1].out.kind == seq::TransitionKind::Cut);
    CHECK(shots[2].in.kind == seq::TransitionKind::Cut);

    // The shot after the pair is untouched -- a split is local.
    CHECK(shots[3].startSeconds == Approx(20.0));

    SECTION("a split too near an edge is refused rather than making a shot nobody can see") {
        std::vector<seq::Shot> fresh = threeShots();
        CHECK_FALSE(seq::splitShot(fresh, 1, 10.01).has_value());
        CHECK_FALSE(seq::splitShot(fresh, 1, 19.99).has_value());
        CHECK(fresh.size() == 3);   // and nothing happened
    }

    SECTION("a split outside the shot is refused") {
        std::vector<seq::Shot> fresh = threeShots();
        CHECK_FALSE(seq::splitShot(fresh, 1, 50.0).has_value());
        CHECK_FALSE(seq::splitShot(fresh, 99, 14.0).has_value());
    }
}

TEST_CASE("a duplicate lands after the original, in time as well as in the list",
          "[seq][shot][editing]") {
    std::vector<seq::Shot> shots = threeShots();
    const auto made = seq::duplicateShot(shots, 0);
    REQUIRE(made.has_value());
    CHECK(*made == 1);
    REQUIRE(shots.size() == 4);

    // **Both halves matter.** A copy on the same span would never play: `shotAt` takes the first
    // match, so the duplicate would sit behind the original for ever and the gesture would look like
    // it had done nothing at all.
    CHECK(shots[1].startSeconds == Approx(shots[0].endSeconds()));
    CHECK(shots[1].durationSeconds == Approx(shots[0].durationSeconds));
    CHECK(shots[1].name != shots[0].name);
}

TEST_CASE("removing a shot leaves the rest alone", "[seq][shot][editing]") {
    std::vector<seq::Shot> shots = threeShots();
    REQUIRE(seq::removeShot(shots, 1));
    REQUIRE(shots.size() == 2);
    CHECK(shots[0].name == "Shot 01");
    CHECK(shots[1].name == "Shot 03");
    CHECK(shots[1].startSeconds == Approx(20.0));   // not shuffled to close the gap

    CHECK_FALSE(seq::removeShot(shots, 99));
    CHECK(shots.size() == 2);

    // A piece with no shots is legal -- it is what an untouched project already looks like.
    REQUIRE(seq::removeShot(shots, 0));
    REQUIRE(seq::removeShot(shots, 0));
    CHECK(shots.empty());
}

TEST_CASE("the edited result is what playback would read", "[seq][shot][editing]") {
    // The gestures above change a vector; this checks the vector still means what the transport
    // thinks it means, which is the only reason any of it matters.
    seq::Sequence piece;
    piece.shots = threeShots();
    REQUIRE(seq::splitShot(piece.shots, 0, 4.0).has_value());

    const seq::Shot* before = piece.shotAt(2.0);
    const seq::Shot* after = piece.shotAt(6.0);
    REQUIRE(before != nullptr);
    REQUIRE(after != nullptr);
    CHECK(before->name == "Shot 01");
    CHECK(after->name == "Shot 01 b");

    // And the cut is exactly where it was asked for: at 4.0 the second half is already live.
    const seq::Shot* onTheSeam = piece.shotAt(4.0);
    REQUIRE(onTheSeam != nullptr);
    CHECK(onTheSeam->name == "Shot 01 b");
}
