// QA pass 2026-09-28: `--export-bundle` without `--headless` opened the live windowed app, and on
// exit that app wrote the person's recent-files list and editor layout. A batch flag must imply
// `--headless`; parseArgs asks app::flagImpliesHeadless for every argument.

#include "app/cli_batch.hpp"

#include <catch2/catch_test_macros.hpp>

using avgen::app::flagImpliesHeadless;

TEST_CASE("Every batch flag implies --headless, --export-bundle included", "[app][cli]") {
    CHECK(flagImpliesHeadless("--export-bundle"));
    CHECK(flagImpliesHeadless("--render"));
    CHECK(flagImpliesHeadless("--pathtrace"));
    CHECK(flagImpliesHeadless("--queue"));
    CHECK(flagImpliesHeadless("--ab"));
    // Flags that configure a session, rather than run a job and exit, do not.
    CHECK_FALSE(flagImpliesHeadless("--project"));
    CHECK_FALSE(flagImpliesHeadless("--play"));
    CHECK_FALSE(flagImpliesHeadless("--save-project"));
    CHECK_FALSE(flagImpliesHeadless("--export"));
}
