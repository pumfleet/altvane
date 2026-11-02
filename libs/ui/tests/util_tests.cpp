// Copyright (C) 2026 - Altvane
// This file is part of Altvane
//
// Altvane is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <thread>

#include <altvane_ui/util/freq_scale.hpp>
#include <altvane_ui/util/spsc_queue.hpp>
#include <altvane_ui/util/tri_buffer.hpp>

using namespace altvane::ui;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("the queue delivers every item in order and drops what doesn't fit", "[util][queue]") {
    SpscQueue<int, 4> q;
    int v{0};
    CHECK_FALSE(q.pop(v));
    for (int i = 0; i < 4; ++i) {
        CHECK(q.push(i));
    }
    CHECK_FALSE(q.push(99));
    CHECK(q.size() == 4);
    for (int i = 0; i < 4; ++i) {
        REQUIRE(q.pop(v));
        CHECK(v == i);
    }
    CHECK_FALSE(q.pop(v));

    // wrapping around the ring many times
    for (int i = 0; i < 100; ++i) {
        REQUIRE(q.push(i));
        REQUIRE(q.push(i + 1000));
        REQUIRE(q.pop(v));
        CHECK(v == i);
        REQUIRE(q.pop(v));
        CHECK(v == i + 1000);
    }

    q.push(1);
    q.push(2);
    q.clear();
    CHECK(q.size() == 0);
    CHECK_FALSE(q.pop(v));
}

TEST_CASE("the queue hands items across threads in order", "[util][queue]") {
    SpscQueue<int, 64> q;
    constexpr int kCount = 100000;
    std::thread producer([&] {
        for (int i = 0; i < kCount;) {
            if (q.push(i)) {
                ++i;
            } else {
                std::this_thread::yield();
            }
        }
    });
    int expected{0};
    bool in_order{true};
    while (expected < kCount) {
        if (int v; q.pop(v)) {
            in_order = in_order && v == expected;
            ++expected;
        } else {
            std::this_thread::yield();
        }
    }
    producer.join();
    CHECK(in_order);
}

TEST_CASE("the triple buffer gives the reader the latest publication", "[util][tri_buffer]") {
    TriBuffer<int> b;
    CHECK_FALSE(b.pull());

    b.getWriter() = 1;
    b.publish();
    b.getWriter() = 2;
    b.publish();
    REQUIRE(b.pull());
    CHECK(b.getReader() == 2);
    CHECK_FALSE(b.pull());
    CHECK(b.getReader() == 2);

    b.getWriter() = 3;
    b.publish();
    REQUIRE(b.pull());
    CHECK(b.getReader() == 3);
}

TEST_CASE("the frequency axis is logarithmic from 10 Hz to 30 kHz", "[util][scale]") {
    FreqScale s;
    s.setBounds(100.f, 600.f);
    CHECK_THAT(s.freqToX(FreqScale::kMinFreq), WithinAbs(100.0, 1e-3));
    CHECK_THAT(s.freqToX(FreqScale::kMaxFreq), WithinAbs(700.0, 1e-3));
    // equal ratios, equal distances
    CHECK_THAT(s.freqToX(200.0) - s.freqToX(100.0), WithinAbs(s.freqToX(2000.0) - s.freqToX(1000.0), 1e-3));
    for (const auto hz : {20.0, 440.0, 1000.0, 15000.0}) {
        CHECK_THAT(s.xToFreq(s.freqToX(hz)), WithinRel(hz, 1e-5));
    }
    // zero and negative frequencies don't produce NaN
    CHECK(std::isfinite(s.freqToX(0.0)));
    CHECK(std::isfinite(s.freqToX(-5.0)));

    CHECK_THAT(FreqScale::pointToFreq(0, 11), WithinRel(FreqScale::kMinFreq, 1e-9));
    CHECK_THAT(FreqScale::pointToFreq(10, 11), WithinRel(FreqScale::kMaxFreq, 1e-9));
    CHECK_THAT(FreqScale::pointToFreq(0, 1), WithinRel(FreqScale::kMinFreq, 1e-9));

    // a zero-width axis is treated as one pixel wide rather than dividing by zero
    s.setBounds(0.f, 0.f);
    CHECK(s.getWidth() == 1.f);
    CHECK(std::isfinite(s.xToFreq(0.5f)));
}

TEST_CASE("the gain axis is linear and centred on 0 dB", "[util][scale]") {
    GainScale s;
    s.setBounds(0.f, 220.f, 10.f);
    s.setMaxDB(12.f);
    CHECK(s.dbToY(0.f) == 110.f);
    CHECK_THAT(s.dbToY(12.f), WithinAbs(10.0, 1e-4));
    CHECK_THAT(s.dbToY(-12.f), WithinAbs(210.0, 1e-4));
    CHECK_THAT(s.yToDB(s.dbToY(5.5f)), WithinAbs(5.5, 1e-4));
    s.setMaxDB(0.f);
    CHECK(s.getMaxDB() > 0.f);
}
