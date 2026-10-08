#include <doctest/doctest.h>

#include "components/events/FrameCounter.h"

using d2bs::runtime::events::FrameCounter;

TEST_CASE("FrameCounter counts rendered frames") {
    const auto before = FrameCounter::Current();
    FrameCounter::Increment();
    CHECK(FrameCounter::Current() == before + 1);
    FrameCounter::Increment();
    CHECK(FrameCounter::Current() == before + 2);
}
