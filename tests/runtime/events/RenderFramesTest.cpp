#include <doctest/doctest.h>

#include <cstdint>

#include "components/events/RenderFrames.h"

using d2bs::runtime::events::RenderFrames;

TEST_CASE("RenderFrames counts rendered frames") {
    const auto before = RenderFrames::Latest();
    RenderFrames::Count();
    CHECK(RenderFrames::Latest() == before + 1);
    RenderFrames::Count();
    CHECK(RenderFrames::Latest() == before + 2);
}

TEST_CASE("RenderFrames fires once per new frame and coalesces the frames a script missed") {
    uint32_t seen = RenderFrames::Latest();
    CHECK_FALSE(RenderFrames::TakeNew(seen));

    RenderFrames::Count();
    REQUIRE(RenderFrames::TakeNew(seen));
    CHECK(seen == RenderFrames::Latest());
    CHECK_FALSE(RenderFrames::TakeNew(seen));

    const auto before = seen;
    RenderFrames::Count();
    RenderFrames::Count();
    RenderFrames::Count();
    REQUIRE(RenderFrames::TakeNew(seen));
    CHECK(seen == before + 3);
    CHECK_FALSE(RenderFrames::TakeNew(seen));
}

TEST_CASE("RenderFrames tracks each script's last frame independently") {
    uint32_t fast = RenderFrames::Latest();
    uint32_t slow = fast;

    RenderFrames::Count();
    CHECK(RenderFrames::TakeNew(fast));
    RenderFrames::Count();
    CHECK(RenderFrames::TakeNew(fast));

    CHECK(RenderFrames::TakeNew(slow));
    CHECK(slow == fast);
    CHECK_FALSE(RenderFrames::TakeNew(fast));
    CHECK_FALSE(RenderFrames::TakeNew(slow));
}
