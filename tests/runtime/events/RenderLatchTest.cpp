#include <doctest/doctest.h>

#include <chrono>

#include "components/events/RenderLatch.h"

using d2bs::runtime::events::RenderLatch;
using namespace std::chrono_literals;

TEST_CASE("RenderLatch queues at most one render event until it is delivered") {
    RenderLatch latch;
    CHECK(latch.TryArm());
    CHECK_FALSE(latch.TryArm());
    CHECK_FALSE(latch.TryArm());

    latch.Deliver(std::chrono::steady_clock::time_point{});
    CHECK(latch.TryArm());
    CHECK_FALSE(latch.TryArm());
}

TEST_CASE("RenderLatch re-arms after a dispatch that never reached the script") {
    RenderLatch latch;
    REQUIRE(latch.TryArm());
    latch.Disarm();
    CHECK(latch.TryArm());
}

TEST_CASE("RenderLatch reports milliseconds since the previous delivery") {
    RenderLatch latch;
    const std::chrono::steady_clock::time_point start{1000ms};

    CHECK(latch.Deliver(start) == 0);
    CHECK(latch.Deliver(start + 40ms) == 40);
    CHECK(latch.Deliver(start + 40ms + 1500us) == 1);
    // A reading earlier than the previous one reports 0 rather than wrapping.
    CHECK(latch.Deliver(start) == 0);
    CHECK(latch.Deliver(start + 16ms) == 16);
}

TEST_CASE("RenderLatch counts rendered frames") {
    const auto before = RenderLatch::LastFrame();
    CHECK(RenderLatch::CountFrame() == before + 1);
    CHECK(RenderLatch::CountFrame() == before + 2);
    CHECK(RenderLatch::LastFrame() == before + 2);
}
