// Copyright (C) 2026 - Altvane
// This file is part of Altvane Dialogue
//
// Altvane Dialogue is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Dialogue is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Dialogue. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <functional>
#include <limits>

#include "PluginProcessor.hpp"
#include "alloc_counter.hpp"

using namespace altvane::dialogue;

namespace {
    void setParam(PluginProcessor& p, const juce::String& id, const float plain) {
        auto* para = p.parameters_.getParameter(id);
        para->setValueNotifyingHost(para->convertTo0to1(plain));
    }

    void setLevels(PluginProcessor& p, const float voice, const float ambience, const float reverb) {
        setParam(p, params::kVoice, voice);
        setParam(p, params::kAmbience, ambience);
        setParam(p, params::kReverb, reverb);
    }

    /**
     * speech-like test signal: harmonic bursts with pauses, over noise
     */
    juce::AudioBuffer<float> makeSignal(const int channels, const int length, const double rate, const int seed = 1) {
        juce::AudioBuffer<float> buffer(channels, length);
        juce::Random random(seed);
        for (int c = 0; c < channels; ++c) {
            auto* x = buffer.getWritePointer(c);
            double phase = 0.0;
            for (int i = 0; i < length; ++i) {
                const auto t = static_cast<double>(i) / rate;
                const auto syllable = std::fmod(t, .25) < .17 ? 1.0 : 0.0;
                const auto f0 = 140.0 + 30.0 * std::sin(2.0 * juce::MathConstants<double>::pi * 1.3 * t);
                phase += f0 / rate;
                double voice = 0.0;
                for (int h = 1; h < 12; ++h) {
                    voice += std::sin(2.0 * juce::MathConstants<double>::pi * phase * h) / h;
                }
                x[i] = static_cast<float>(.2 * syllable * voice + .03 * (random.nextFloat() * 2.f - 1.f));
            }
        }
        return buffer;
    }

    struct Run {
        juce::AudioBuffer<float> output; // the main output's channels, then each stem output's
        int latency{0};
    };

    juce::AudioProcessor::BusesLayout makeLayout(const juce::AudioChannelSet& main, const bool stems) {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add(main);
        layout.outputBuses.add(main);
        for (int s = 0; s < dsp::kNumStems; ++s) {
            layout.outputBuses.add(stems ? main : juce::AudioChannelSet::disabled());
        }
        return layout;
    }

    /**
     * @param stems whether the host enables the stem outputs
     * @return the number of channels in the process buffer
     */
    int prepare(PluginProcessor& p, const int channels, const double rate, const int max_block, const bool realtime,
                const bool stems = false) {
        REQUIRE(p.setBusesLayout(makeLayout(juce::AudioChannelSet::canonicalChannelSet(channels), stems)));
        p.setRateAndBufferSizeDetails(rate, max_block);
        p.setNonRealtime(!realtime);
        p.prepareToPlay(rate, max_block);
        return p.getTotalNumOutputChannels();
    }

    /**
     * run a buffer through the processor in blocks (sizes from `blocks`, cycled), flushing the latency
     * @param before_block called with the position of each block before it is processed
     */
    Run run(PluginProcessor& p, const juce::AudioBuffer<float>& input, const double rate,
            const std::vector<int>& blocks, const bool realtime, const bool stems = false,
            const std::function<void(int)>& before_block = nullptr) {
        const auto channels = input.getNumChannels();
        const auto max_block = *std::max_element(blocks.begin(), blocks.end());
        const auto buffer_channels = prepare(p, channels, rate, max_block, realtime, stems);
        Run result;
        result.latency = p.getLatencySamples();
        const auto total = input.getNumSamples() + result.latency;
        result.output.setSize(buffer_channels, total);
        juce::AudioBuffer<float> io(buffer_channels, max_block);
        juce::MidiBuffer midi;
        size_t b = 0;
        for (int pos = 0; pos < total;) {
            const auto n = std::min(blocks[b++ % blocks.size()], total - pos);
            io.setSize(buffer_channels, n, false, false, true);
            // the stem outputs' channels start out with junk, as hosts may leave them
            for (int c = channels; c < buffer_channels; ++c) {
                juce::FloatVectorOperations::fill(io.getWritePointer(c), 1.f, n);
            }
            for (int c = 0; c < channels; ++c) {
                io.clear(c, 0, n);
                const auto available = std::clamp(input.getNumSamples() - pos, 0, n);
                if (available > 0) {
                    io.copyFrom(c, 0, input, c, pos, available);
                }
            }
            if (before_block) {
                before_block(pos);
            }
            p.processBlock(io, midi);
            for (int c = 0; c < buffer_channels; ++c) {
                result.output.copyFrom(c, pos, io, c, 0, n);
            }
            pos += n;
        }
        p.releaseResources();
        return result;
    }

    /**
     * the largest difference between two channels of runs, in dB relative to full scale
     */
    float differenceDb(const juce::AudioBuffer<float>& a, const int channel_a, const juce::AudioBuffer<float>& b,
                       const int channel_b, const float b_gain = 1.f) {
        float worst = 0.f;
        for (int i = 0; i < a.getNumSamples(); ++i) {
            worst = std::max(worst, std::abs(a.getSample(channel_a, i) - b_gain * b.getSample(channel_b, i)));
        }
        return juce::Decibels::gainToDecibels(worst, -200.f);
    }

    /**
     * the largest difference between the main output and the sum of the stem outputs, in dB relative to full scale
     */
    float stemSumResidualDb(const Run& r, const int channels) {
        juce::AudioBuffer<float> sum(channels, r.output.getNumSamples());
        sum.clear();
        for (int s = 0; s < dsp::kNumStems; ++s) {
            for (int c = 0; c < channels; ++c) {
                sum.addFrom(c, 0, r.output, channels * (1 + s) + c, 0, r.output.getNumSamples());
            }
        }
        float worst = -200.f;
        for (int c = 0; c < channels; ++c) {
            worst = std::max(worst, differenceDb(r.output, c, sum, c));
        }
        return worst;
    }

    /**
     * the largest difference between the output and the input delayed by the latency, in dB relative to full scale
     */
    float nullResidualDb(const Run& r, const juce::AudioBuffer<float>& input) {
        float worst = 0.f;
        for (int c = 0; c < input.getNumChannels(); ++c) {
            for (int i = 0; i < input.getNumSamples(); ++i) {
                worst = std::max(worst, std::abs(r.output.getSample(c, i + r.latency) - input.getSample(c, i)));
            }
        }
        return juce::Decibels::gainToDecibels(worst, -200.f);
    }

    /**
     * a test signal with nothing above about 1.5 kHz: smooth syllables of a harmonic voice over a steady rumble.
     * its second differences are tiny, so a step in the output (a hard switch between two versions of it) stands out
     */
    juce::AudioBuffer<float> makeLowSignal(const int channels, const int length, const double rate, const int seed = 1) {
        juce::AudioBuffer<float> buffer(channels, length);
        juce::Random random(seed);
        const auto pole = std::exp(-2.0 * juce::MathConstants<double>::pi * 300.0 / rate);
        for (int c = 0; c < channels; ++c) {
            auto* x = buffer.getWritePointer(c);
            std::vector<double> rumble(static_cast<size_t>(length));
            std::array<double, 4> lp{};
            double energy = 0.0;
            for (auto& r : rumble) {
                double v = random.nextFloat() * 2.0 - 1.0;
                for (auto& s : lp) {
                    s = pole * s + (1.0 - pole) * v;
                    v = s;
                }
                r = v;
                energy += v * v;
            }
            const auto rumble_gain = .08 / std::sqrt(energy / length + 1e-30);
            double phase = 0.0;
            for (int i = 0; i < length; ++i) {
                const auto t = static_cast<double>(i) / rate;
                const auto syllable = std::pow(std::max(0.0, std::sin(2.0 * juce::MathConstants<double>::pi * 3.0 * t)), 2.0);
                phase += (130.0 + 20.0 * std::sin(2.0 * juce::MathConstants<double>::pi * 1.3 * t)) / rate;
                double voice = 0.0;
                for (int h = 1; h <= 10; ++h) {
                    voice += std::sin(2.0 * juce::MathConstants<double>::pi * phase * h) / h;
                }
                x[i] = static_cast<float>(.2 * syllable * voice + rumble_gain * rumble[static_cast<size_t>(i)]);
            }
        }
        return buffer;
    }

    /**
     * the largest second difference of a channel from `from` on: a step of size d shows as about d
     */
    float maxCurvature(const juce::AudioBuffer<float>& b, const int channel, const int from = 2) {
        float worst = 0.f;
        const auto* y = b.getReadPointer(channel);
        for (int i = std::max(from, 2); i < b.getNumSamples(); ++i) {
            worst = std::max(worst, std::abs(y[i] - 2.f * y[i - 1] + y[i - 2]));
        }
        return worst;
    }

    /**
     * run in real time with the worker run by hand: before each block, unless `stalled` says it is stalled, the
     * worker processes what the previous block delivered (as a worker that keeps up does before the next block)
     */
    Run runWithWorker(PluginProcessor& p, const juce::AudioBuffer<float>& input, const double rate, const int block,
                      const bool stems, const std::function<bool(int)>& stalled = nullptr,
                      const std::function<void(int)>& before_block = nullptr) {
        p.getEngine().setWorkerThreadForTesting(false);
        return run(p, input, rate, {block}, true, stems, [&](const int pos) {
            if (!stalled || !stalled(pos)) {
                p.getEngine().runWorkerForTesting();
            }
            if (before_block) {
                before_block(pos);
            }
        });
    }
}

TEST_CASE("with every level at unity the output is the input, delayed by the reported latency", "[engine]") {
    const auto rate = GENERATE(44100.0, 48000.0, 88200.0, 96000.0, 192000.0);
    const auto channels = GENERATE(1, 2);
    CAPTURE(rate, channels);
    PluginProcessor p;
    setLevels(p, 0.f, 0.f, 0.f);
    const auto input = makeSignal(channels, static_cast<int>(rate * 1.5), rate);
    // irregular block sizes, as hosts deliver them
    const auto r = run(p, input, rate, {512, 7, 300, 1, 512, 64, 333}, false);
    CHECK(nullResidualDb(r, input) < -90.f);
}

TEST_CASE("the real-time path is transparent even when the model falls behind", "[engine]") {
    PluginProcessor p;
    setLevels(p, 0.f, 0.f, 0.f);
    const auto input = makeSignal(2, 48000, 48000.0);
    // calls back to back, far faster than real time: many hops arrive late and fall back to the delayed input
    const auto r = run(p, input, 48000.0, {256}, true);
    CHECK(nullResidualDb(r, input) < -90.f);
}

TEST_CASE("a worker that keeps up gives exactly the offline output, and offline never skips", "[engine][stems]") {
    const auto input = makeSignal(2, 48000 * 4, 48000.0);
    const std::vector<int> blocks{4096, 64, 1000};
    // offline, with the worker thread there and blocks far faster than real time: the caller waits for every frame
    PluginProcessor offline;
    setLevels(offline, -3.f, params::kLevelOff, -20.f);
    const auto a = run(offline, input, 48000.0, blocks, false, true);
    CHECK(offline.getEngine().getSkippedFrames() == 0);
    CHECK(offline.getEngine().getUnderruns() == 0);

    PluginProcessor realtime;
    setLevels(realtime, -3.f, params::kLevelOff, -20.f);
    realtime.getEngine().setWorkerThreadForTesting(false);
    const auto b = run(realtime, input, 48000.0, blocks, true, true, [&realtime](int) {
        realtime.getEngine().runWorkerForTesting();
    });
    CHECK(realtime.getEngine().getSkippedFrames() == 0);
    CHECK(realtime.getEngine().getUnderruns() == 0);
    for (int c = 0; c < a.output.getNumChannels(); ++c) {
        CAPTURE(c);
        CHECK(differenceDb(a.output, c, b.output, c) < -200.f + 1.f);
    }
}

TEST_CASE("late frames cross-fade into and out of the fallback, the input as voice at its level", "[engine][stems]") {
    constexpr int kBlock = 256;
    constexpr int kShortStall = 24000, kLongStall = 48000, kLongStallEnd = 72000;
    constexpr float kVoiceDb = -3.f;
    const auto input = makeLowSignal(2, 48000 * 3, 48000.0);
    const auto configure = [](PluginProcessor& p) {
        setLevels(p, kVoiceDb, params::kLevelOff, -20.f);
    };
    PluginProcessor on_time;
    configure(on_time);
    const auto ideal = runWithWorker(on_time, input, 48000.0, kBlock, true);
    REQUIRE(on_time.getEngine().getUnderruns() == 0);

    // a short stall, which the worker catches up on, and a long one, after which it skips ahead
    PluginProcessor late;
    configure(late);
    const auto r = runWithWorker(late, input, 48000.0, kBlock, true, [](const int pos) {
        return (pos >= kShortStall && pos < kShortStall + 3 * kBlock) || (pos >= kLongStall && pos < kLongStallEnd);
    });
    REQUIRE(late.getEngine().getUnderruns() > 0);
    REQUIRE(late.getEngine().getSkippedFrames() > 0);
    const auto latency = r.latency;

    // a fallback that is all voice keeps the stems adding up to the mix
    CHECK(stemSumResidualDb(r, 2) < -90.f);
    // well into the long stall, the output is the input at the voice's level: never louder ambience than the mix
    const auto voice = params::levelToGain(kVoiceDb);
    float worst = 0.f, difference = 0.f;
    int compared = 0;
    for (int c = 0; c < 2; ++c) {
        for (int i = kLongStall + 4 * dsp::kHop; i < kLongStallEnd; ++i) {
            const auto fallback = voice * input.getSample(c, i - latency);
            worst = std::max(worst, std::abs(r.output.getSample(c, i) - fallback));
            difference += std::abs(ideal.output.getSample(c, i) - fallback);
            ++compared;
        }
    }
    CHECK(worst < 1e-6f);

    // into the fallback and out of it, no step: the output is never less smooth than the processed output or the
    // fallback themselves, whereas a hard switch between the two would step by their typical difference
    for (int c = 0; c < 2; ++c) {
        CAPTURE(c);
        const auto allowed = std::max(maxCurvature(ideal.output, c, latency), voice * maxCurvature(input, c));
        const auto typical_step = difference / static_cast<float>(compared);
        CAPTURE(allowed, typical_step, maxCurvature(r.output, c, latency));
        CHECK(typical_step > 4.f * allowed);
        CHECK(maxCurvature(r.output, c, latency) < 1.5f * allowed);
    }
}

TEST_CASE("after a stall the worker skips ahead and is back to processed output in a bounded time", "[engine]") {
    const auto block = GENERATE(128, 1024);
    CAPTURE(block);
    constexpr int kStall = 48000, kStallEnd = 96000;
    const auto input = makeSignal(2, 48000 * 4, 48000.0);
    const auto configure = [](PluginProcessor& p) {
        setLevels(p, 0.f, params::kLevelOff, params::kLevelOff);
    };
    PluginProcessor on_time;
    configure(on_time);
    const auto ideal = runWithWorker(on_time, input, 48000.0, block, false);

    PluginProcessor late;
    configure(late);
    uint64_t underruns = 0;
    int last_late_block = -1;
    int64_t skipped_by_end = -1;
    const auto r = runWithWorker(late, input, 48000.0, block, false, [](const int pos) {
        return pos >= kStall && pos < kStallEnd;
    }, [&](const int pos) {
        if (const auto u = late.getEngine().getUnderruns(); u != underruns) {
            underruns = u;
            last_late_block = pos - block;
        }
        if (skipped_by_end < 0 && pos >= kStallEnd) {
            skipped_by_end = static_cast<int64_t>(late.getEngine().getSkippedFrames());
        }
    });
    // the worker skipped about the stall's worth of frames as soon as it ran again, and none after
    CHECK(skipped_by_end > (kStallEnd - kStall) / dsp::kHop - 10);
    CHECK(static_cast<int64_t>(late.getEngine().getSkippedFrames()) == skipped_by_end);
    // the audio thread stops playing the fallback within a block of the worker running again...
    CHECK(last_late_block <= kStallEnd + block);
    // ...which plays its own fallback while the model re-warms (kModelLag hops) and then fades in (8 hops): after
    // that, the output is processed again, as close to the uninterrupted output as the model's state allows
    const auto back = kStallEnd + 14 * dsp::kHop;
    for (int from = back; from + 4800 <= r.output.getNumSamples() - r.latency; from += 4800) {
        CAPTURE(from);
        double error = 0.0, removed = 0.0;
        for (int c = 0; c < 2; ++c) {
            for (int i = from; i < from + 4800; ++i) {
                const auto y = ideal.output.getSample(c, i);
                const auto x = input.getSample(c, i - r.latency);
                error += std::pow(r.output.getSample(c, i) - y, 2.0);
                removed += std::pow(x - y, 2.0);
            }
        }
        // what the processing removes is there, and the stalled run removes nearly the same
        CHECK(removed > 0.0);
        CHECK(error < removed * .1);
    }
}

TEST_CASE("the worker never reads input the audio thread may be overwriting", "[engine]") {
    // a stall longer than the input ring (16384 samples), then stalls of random lengths
    const auto block = GENERATE(64, 512, 2048);
    CAPTURE(block);
    const auto input = makeSignal(2, 48000 * 8, 48000.0);
    PluginProcessor p;
    setLevels(p, -3.f, -30.f, -12.f);
    juce::Random random(7);
    int stall_left = 0;
    const auto r = runWithWorker(p, input, 48000.0, block, true, [&](const int pos) {
        if (pos >= 48000 && pos < 48000 * 5 / 2) {
            return true;
        }
        if (stall_left > 0) {
            --stall_left;
            return true;
        }
        if (pos > 48000 * 3 && random.nextInt(20) == 0) {
            stall_left = random.nextInt(48000 / block);
        }
        return false;
    });
    CHECK(p.getEngine().getSkippedFrames() > 0);
    CHECK(p.getEngine().getLeastInputMarginForTesting() >= 0);
    CHECK(stemSumResidualDb(r, 2) < -90.f);
    for (int c = 0; c < r.output.getNumChannels(); ++c) {
        for (int i = 0; i < r.output.getNumSamples(); ++i) {
            REQUIRE(std::isfinite(r.output.getSample(c, i)));
        }
    }
}

TEST_CASE("releasing does not wait for a backlog of hops", "[engine]") {
    // a host calling back far faster than real time (pluginval's "Open editor whilst processing") leaves the worker
    // seconds of model work; waiting for it would time out and kill the thread by force
    PluginProcessor p;
    p.setPlayConfigDetails(2, 2, 48000.0, 64);
    p.setNonRealtime(false);
    p.prepareToPlay(48000.0, 64);
    juce::AudioBuffer<float> io(2, 64);
    juce::MidiBuffer midi;
    const auto input = makeSignal(2, 64, 48000.0);
    for (int block = 0; block < 48000 * 30 / 64; ++block) {
        io.makeCopyOf(input, true);
        p.processBlock(io, midi);
    }
    const auto start = juce::Time::getMillisecondCounterHiRes();
    p.releaseResources();
    CHECK(juce::Time::getMillisecondCounterHiRes() - start < 1000.0);
}

TEST_CASE("the reported latency is the delay of an impulse", "[engine]") {
    const auto rate = GENERATE(44100.0, 48000.0, 96000.0);
    CAPTURE(rate);
    PluginProcessor p;
    setLevels(p, 0.f, 0.f, 0.f);
    juce::AudioBuffer<float> input(1, static_cast<int>(rate));
    input.clear();
    input.setSample(0, 1000, 1.f);
    const auto r = run(p, input, rate, {480}, false);
    int peak = 0;
    for (int i = 0; i < r.output.getNumSamples(); ++i) {
        if (std::abs(r.output.getSample(0, i)) > std::abs(r.output.getSample(0, peak))) {
            peak = i;
        }
    }
    CHECK(peak == 1000 + r.latency);
}

TEST_CASE("the 48 kHz round trip lines up with the delayed input", "[engine]") {
    // with no residual, the output is up(down(x)): in band it must match the delayed input sample for sample,
    // or the residual would comb-filter whenever the ambience level is not unity
    const auto rate = GENERATE(44100.0, 88200.0, 96000.0);
    CAPTURE(rate);
    dsp::RateBridge bridge;
    constexpr int kBlock = 300;
    bridge.prepare(rate, 1, kBlock);
    juce::AudioBuffer<float> io(1, kBlock);
    std::vector<float> in, out;
    for (int b = 0; b < static_cast<int>(rate) / kBlock; ++b) {
        for (int i = 0; i < kBlock; ++i) {
            const auto t = static_cast<double>(b * kBlock + i) / rate;
            const auto x = static_cast<float>(.5 * std::sin(2.0 * juce::MathConstants<double>::pi * 1000.0 * t)
                                              + .3 * std::sin(2.0 * juce::MathConstants<double>::pi * 7300.0 * t));
            io.setSample(0, i, x);
            in.push_back(x);
        }
        bridge.process(io, false, 0.f);
        out.insert(out.end(), io.getReadPointer(0), io.getReadPointer(0) + kBlock);
    }
    bridge.release();
    float worst = 0.f;
    for (size_t i = static_cast<size_t>(bridge.getLatency()) + 4800; i < out.size(); ++i) {
        worst = std::max(worst, std::abs(out[i] - in[i - static_cast<size_t>(bridge.getLatency())]));
    }
    CHECK(juce::Decibels::gainToDecibels(worst) < -60.f);
}

TEST_CASE("the solo stems add up to the input", "[engine]") {
    PluginProcessor p;
    setLevels(p, -6.f, -20.f, -10.f);
    const auto input = makeSignal(1, 48000 * 2, 48000.0);
    juce::AudioBuffer<float> sum(1, input.getNumSamples() + 8192);
    sum.clear();
    int latency = 0;
    for (const auto listen : {dsp::Listen::kVoice, dsp::Listen::kAmbience, dsp::Listen::kReverb}) {
        p.setListen(listen);
        const auto r = run(p, input, 48000.0, {512}, false);
        latency = r.latency;
        sum.addFrom(0, 0, r.output, 0, 0, r.output.getNumSamples());
    }
    Run combined{sum, latency};
    CHECK(nullResidualDb(combined, input) < -90.f);
}

TEST_CASE("Mid/Side is transparent at unity, and its solo stems add up to the input", "[engine]") {
    const auto rate = GENERATE(44100.0, 48000.0);
    CAPTURE(rate);
    PluginProcessor p;
    setParam(p, params::kStereo, 2.f);
    // a centred voice over noise that differs between the channels, so the side is not silent
    const auto input = makeSignal(2, static_cast<int>(rate * 1.5), rate);
    setLevels(p, 0.f, 0.f, 0.f);
    CHECK(nullResidualDb(run(p, input, rate, {512, 7, 300}, false), input) < -90.f);

    setLevels(p, -6.f, -20.f, -10.f);
    Run sum;
    for (const auto listen : {dsp::Listen::kVoice, dsp::Listen::kAmbience, dsp::Listen::kReverb}) {
        p.setListen(listen);
        const auto r = run(p, input, rate, {512}, false);
        if (sum.output.getNumSamples() == 0) {
            sum.output.setSize(2, r.output.getNumSamples());
            sum.output.clear();
            sum.latency = r.latency;
        }
        for (int c = 0; c < 2; ++c) {
            sum.output.addFrom(c, 0, r.output, c, 0, r.output.getNumSamples());
        }
    }
    CHECK(nullResidualDb(sum, input) < -90.f);
}

TEST_CASE("Mid/Side meters the stems of both streams", "[engine]") {
    // an anti-phase input has a silent mid, and its side is the left channel: Mid/Side's side stream hears exactly
    // what Stereo's left stream hears, so the stems the editor draws must agree
    auto input = makeSignal(2, 48000, 48000.0);
    for (int i = 0; i < input.getNumSamples(); ++i) {
        input.setSample(1, i, -input.getSample(0, i));
    }
    auto meter = [&input](const float mode) {
        PluginProcessor p;
        setLevels(p, 0.f, -20.f, -10.f);
        setParam(p, params::kStereo, mode);
        dsp::AnalysisFrame frame;
        bool pulled = false;
        run(p, input, 48000.0, {512}, false, false, [&](const int pos) {
            if (!pulled && pos >= 24000) {
                pulled = p.getEngine().pullAnalysis(frame);
            }
        });
        REQUIRE(pulled);
        return frame;
    };
    const auto stereo = meter(1.f);
    const auto mid_side = meter(2.f);
    double total = 0.0, worst = 0.0;
    for (size_t s = 0; s < dsp::kNumStems; ++s) {
        for (size_t b = 0; b < dsp::AnalysisFrame::kBands; ++b) {
            total += stereo.stems[s][b];
            worst = std::max(worst, static_cast<double>(std::abs(stereo.stems[s][b] - mid_side.stems[s][b])
                                                        / (stereo.input[b] + 1e-12f)));
        }
    }
    CHECK(total > 0.0);
    CHECK(worst < 1e-4);
}

TEST_CASE("changing the stereo mode while playing stays transparent", "[engine][stems]") {
    PluginProcessor p;
    setLevels(p, 0.f, 0.f, 0.f);
    const auto input = makeSignal(2, 48000 * 2, 48000.0);
    // Linked, Mid/Side, Stereo, Mid/Side, Linked: the fades cross between left/right and mid/side
    const auto r = run(p, input, 48000.0, {512}, false, true, [&p](const int pos) {
        constexpr std::array<std::pair<int, float>, 4> kChanges{{{20, 2.f}, {40, 1.f}, {60, 2.f}, {80, 0.f}}};
        for (const auto& [block, mode] : kChanges) {
            if (pos == block * 512) {
                setParam(p, params::kStereo, mode);
            }
        }
    });
    CHECK(nullResidualDb(r, input) < -90.f);
    // the stem outputs fade with the mix
    CHECK(stemSumResidualDb(r, 2) < -90.f);
}

TEST_CASE("changing the quality while playing stays transparent, and cross-fades at any level", "[engine][stems]") {
    const auto stereo = GENERATE(0.f, 2.f);
    CAPTURE(stereo);
    // High, Eco, High, Eco: each change runs the new model beside the old one until it is warm, then fades
    const auto change = [](PluginProcessor& p) {
        return [&p](const int pos) {
            constexpr std::array<std::pair<int, float>, 4> kChanges{{{0, 1.f}, {30, 0.f}, {60, 1.f}, {90, 0.f}}};
            for (const auto& [block, quality] : kChanges) {
                if (pos == block * 512) {
                    setParam(p, params::kQuality, quality);
                }
            }
        };
    };
    {
        PluginProcessor p;
        setParam(p, params::kStereo, stereo);
        setLevels(p, 0.f, 0.f, 0.f);
        const auto input = makeSignal(2, 48000 * 2, 48000.0);
        const auto r = run(p, input, 48000.0, {512}, false, true, change(p));
        CHECK(nullResidualDb(r, input) < -90.f);
        CHECK(stemSumResidualDb(r, 2) < -90.f);
    }

    // with the levels apart, Eco and High give different outputs: the change between them has no step
    const auto input = makeLowSignal(2, 48000 * 2, 48000.0);
    const auto configure = [stereo](PluginProcessor& p, const float quality) {
        setParam(p, params::kStereo, stereo);
        setParam(p, params::kQuality, quality);
        setLevels(p, 0.f, params::kLevelOff, -20.f);
    };
    PluginProcessor eco, high, switching;
    configure(eco, 0.f);
    configure(high, 1.f);
    configure(switching, 1.f);
    const auto a = run(eco, input, 48000.0, {512}, false);
    const auto b = run(high, input, 48000.0, {512}, false);
    const auto r = run(switching, input, 48000.0, {512}, false, true, change(switching));
    CHECK(stemSumResidualDb(r, 2) < -90.f);
    for (int c = 0; c < 2; ++c) {
        CAPTURE(c);
        const auto allowed = std::max(maxCurvature(a.output, c, a.latency), maxCurvature(b.output, c, b.latency));
        CHECK(differenceDb(a.output, c, b.output, c) > -40.f);
        CHECK(maxCurvature(r.output, c, r.latency) < 1.5f * allowed);
    }
}

TEST_CASE("non-finite input does not poison the model", "[engine]") {
    const auto rate = GENERATE(44100.0, 48000.0);
    const auto bad = GENERATE(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity());
    CAPTURE(rate, bad);
    const auto length = static_cast<int>(rate * 3);
    const auto burst = static_cast<int>(rate / 2);
    auto input = makeSignal(2, length, rate);
    auto clean = input;
    for (int c = 0; c < 2; ++c) {
        for (int i = burst; i < burst + 2000; ++i) {
            input.setSample(c, i, bad);
            clean.setSample(c, i, 0.f);
        }
    }
    PluginProcessor p;
    setLevels(p, -3.f, -30.f, -12.f);
    const auto r = run(p, input, rate, {512}, false, true);
    const auto reference = run(p, clean, rate, {512}, false, true);
    // the engine hears silence instead; only the rate conversion's residual carries the bad samples, briefly
    const auto settled = burst + 2000 + r.latency + static_cast<int>(rate / 50);
    const auto last_second = r.output.getNumSamples() - static_cast<int>(rate);
    for (int c = 0; c < r.output.getNumChannels(); ++c) {
        CAPTURE(c);
        double error = 0.0, energy = 0.0;
        for (int i = settled; i < r.output.getNumSamples(); ++i) {
            REQUIRE(std::isfinite(r.output.getSample(c, i)));
            if (i >= last_second) {
                error += std::pow(r.output.getSample(c, i) - reference.output.getSample(c, i), 2.0);
                energy += std::pow(reference.output.getSample(c, i), 2.0);
            }
        }
        if (static_cast<int>(rate) == 48000) {
            // with no rate conversion, the engine hears exactly the clean input with silence for the burst
            CHECK(differenceDb(r.output, c, reference.output, c) < -200.f + 1.f);
        } else {
            // around the burst, the downsampler gives the engine slightly different samples than the clean run;
            // the model carries that on for a while, but it fades rather than staying
            CAPTURE(error, energy);
            CHECK(error < energy * 1e-3);
        }
    }
}

TEST_CASE("input too loud for the model resets it rather than leaving it broken", "[engine]") {
    auto input = makeSignal(2, 48000 * 3, 48000.0);
    for (int c = 0; c < 2; ++c) {
        for (int i = 24000; i < 26000; ++i) {
            input.setSample(c, i, input.getSample(c, i) * 1e30f);
        }
    }
    PluginProcessor p;
    setLevels(p, 0.f, params::kLevelOff, params::kLevelOff);
    const auto r = run(p, input, 48000.0, {512}, false);
    // a second later the model separates again: the output is finite and has had the ambience removed
    double in_energy = 0.0, out_energy = 0.0;
    for (int c = 0; c < 2; ++c) {
        for (int i = 72000; i < 48000 * 3; ++i) {
            const auto y = r.output.getSample(c, i + r.latency);
            REQUIRE(std::isfinite(y));
            in_energy += std::pow(input.getSample(c, i), 2.0);
            out_energy += std::pow(y, 2.0);
        }
    }
    CHECK(out_energy < in_energy * .9);
    CHECK(out_energy > in_energy * .1);
}

TEST_CASE("offline output does not depend on the block size", "[engine]") {
    PluginProcessor p;
    setLevels(p, 0.f, -30.f, -12.f);
    const auto input = makeSignal(2, 48000 * 2, 48000.0);
    const auto a = run(p, input, 48000.0, {512}, false);
    const auto b = run(p, input, 48000.0, {64, 1000, 17}, false);
    float worst = 0.f;
    for (int c = 0; c < 2; ++c) {
        for (int i = 0; i < input.getNumSamples(); ++i) {
            worst = std::max(worst, std::abs(a.output.getSample(c, i + a.latency) - b.output.getSample(c, i + b.latency)));
        }
    }
    CHECK(worst < 1e-6f);
}

TEST_CASE("lowering ambience and reverb removes energy, and Removed plays what is gone", "[engine]") {
    PluginProcessor p;
    const auto input = makeSignal(1, 48000 * 2, 48000.0);
    setLevels(p, 0.f, -60.f, -60.f);
    const auto mix = run(p, input, 48000.0, {512}, false);
    p.setListen(dsp::Listen::kRemoved);
    const auto removed = run(p, input, 48000.0, {512}, false);
    double in_energy = 0.0, mix_energy = 0.0;
    float worst = 0.f;
    for (int i = 24000; i < input.getNumSamples(); ++i) {
        const auto x = input.getSample(0, i);
        const auto y = mix.output.getSample(0, i + mix.latency);
        in_energy += x * x;
        mix_energy += y * y;
        worst = std::max(worst, std::abs(y + removed.output.getSample(0, i + removed.latency) - x));
    }
    CHECK(mix_energy < in_energy * .95);
    CHECK(worst < 1e-4f);
}

TEST_CASE("the stem outputs add up to the main output, each its solo at its level", "[engine][stems]") {
    const auto rate = GENERATE(44100.0, 48000.0, 96000.0);
    const auto channels = GENERATE(1, 2);
    CAPTURE(rate, channels);
    PluginProcessor p;
    constexpr std::array<float, dsp::kNumStems> kLevels{-6.f, -20.f, 3.f};
    setLevels(p, kLevels[dsp::kVoice], kLevels[dsp::kAmbience], kLevels[dsp::kReverb]);
    setParam(p, params::sensitivityID(dsp::kVoice, 4), .5f);
    setParam(p, params::kOutput, -3.f);
    const auto input = makeSignal(channels, static_cast<int>(rate * 1.5), rate);
    const std::vector<int> blocks{512, 7, 300, 1, 512, 64, 333};
    const auto r = run(p, input, rate, blocks, false, true);
    CHECK(stemSumResidualDb(r, channels) < -90.f);

    // with the stem outputs on, the main output is what it was without them
    const auto plain = run(p, input, rate, blocks, false);
    for (int c = 0; c < channels; ++c) {
        CHECK(differenceDb(r.output, c, plain.output, c) < -110.f);
    }

    // a solo plays its stem at unity (and the output gain); the stem output has it at its level
    for (const auto stem : {dsp::kVoice, dsp::kAmbience, dsp::kReverb}) {
        CAPTURE(stem);
        p.setListen(static_cast<dsp::Listen>(static_cast<int>(dsp::Listen::kVoice) + stem));
        const auto solo = run(p, input, rate, blocks, false);
        const auto gain = params::levelToGain(kLevels[static_cast<size_t>(stem)]);
        for (int c = 0; c < channels; ++c) {
            CHECK(differenceDb(r.output, channels * (1 + stem) + c, solo.output, c, gain) < -90.f);
        }
    }
}

TEST_CASE("in Mid/Side the stem outputs add up to the main output, each its solo at its level", "[engine][stems]") {
    PluginProcessor p;
    setParam(p, params::kStereo, 2.f);
    constexpr std::array<float, dsp::kNumStems> kLevels{-6.f, -20.f, 3.f};
    setLevels(p, kLevels[dsp::kVoice], kLevels[dsp::kAmbience], kLevels[dsp::kReverb]);
    const auto input = makeSignal(2, 48000 * 3 / 2, 48000.0);
    const auto r = run(p, input, 48000.0, {512}, false, true);
    CHECK(stemSumResidualDb(r, 2) < -90.f);
    for (const auto stem : {dsp::kVoice, dsp::kAmbience, dsp::kReverb}) {
        CAPTURE(stem);
        p.setListen(static_cast<dsp::Listen>(static_cast<int>(dsp::Listen::kVoice) + stem));
        const auto solo = run(p, input, 48000.0, {512}, false);
        const auto gain = params::levelToGain(kLevels[static_cast<size_t>(stem)]);
        for (int c = 0; c < 2; ++c) {
            CHECK(differenceDb(r.output, 2 * (1 + stem) + c, solo.output, c, gain) < -90.f);
        }
    }
}

TEST_CASE("the stem outputs add up to the main output when the model falls behind", "[engine][stems]") {
    PluginProcessor p;
    setLevels(p, -6.f, -20.f, 3.f);
    const auto input = makeSignal(2, 48000, 48000.0);
    // as in the real-time test above: late samples are the delayed input, in the main output and the voice's
    const auto r = run(p, input, 48000.0, {256}, true, true);
    CHECK(stemSumResidualDb(r, 2) < -90.f);
}

TEST_CASE("bypassed, the stem outputs are silent", "[engine][stems]") {
    PluginProcessor p;
    setLevels(p, -6.f, -20.f, 3.f);
    setParam(p, params::kBypass, 1.f);
    const auto input = makeSignal(2, 48000, 48000.0);
    const auto r = run(p, input, 48000.0, {512}, false, true);
    for (int c = 2; c < r.output.getNumChannels(); ++c) {
        CHECK(r.output.getMagnitude(c, 0, r.output.getNumSamples()) == 0.f);
    }
    CHECK(nullResidualDb(r, input) < -90.f);
}

TEST_CASE("the stem outputs are off or match the main output", "[engine][stems]") {
    PluginProcessor p;
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();
    CHECK(p.checkBusesLayoutSupported(makeLayout(stereo, false)));
    CHECK(p.checkBusesLayoutSupported(makeLayout(stereo, true)));
    CHECK(p.checkBusesLayoutSupported(makeLayout(mono, true)));
    auto voice_only = makeLayout(stereo, false);
    voice_only.outputBuses.getReference(PluginProcessor::kFirstStemBus + dsp::kVoice) = stereo;
    CHECK(p.checkBusesLayoutSupported(voice_only));
    auto mismatched = makeLayout(stereo, true);
    mismatched.outputBuses.getReference(PluginProcessor::kFirstStemBus + dsp::kReverb) = mono;
    CHECK_FALSE(p.checkBusesLayoutSupported(mismatched));
    // AU enables every bus at its default (stereo) and still needs a mono instance
    auto mono_with_stereo_stems = makeLayout(stereo, true);
    mono_with_stereo_stems.inputBuses.getReference(0) = mono;
    mono_with_stereo_stems.outputBuses.getReference(0) = mono;
    CHECK(p.checkBusesLayoutSupported(mono_with_stereo_stems));
    // by default, the plug-in is a plain stereo effect
    CHECK(p.getBusesLayout() == makeLayout(stereo, false));
}

TEST_CASE("a mono instance plays each stem on both channels of a stereo stem output", "[engine][stems]") {
    PluginProcessor p;
    setLevels(p, -6.f, -20.f, 3.f);
    const auto input = makeSignal(1, 48000, 44100.0);
    auto layout = makeLayout(juce::AudioChannelSet::stereo(), true);
    layout.inputBuses.getReference(0) = juce::AudioChannelSet::mono();
    layout.outputBuses.getReference(0) = juce::AudioChannelSet::mono();
    REQUIRE(p.setBusesLayout(layout));
    REQUIRE(p.getTotalNumOutputChannels() == 7);
    p.setRateAndBufferSizeDetails(44100.0, 512);
    p.setNonRealtime(true);
    p.prepareToPlay(44100.0, 512);
    const auto latency = p.getLatencySamples();
    juce::AudioBuffer<float> out(7, input.getNumSamples() + latency);
    juce::AudioBuffer<float> io(7, 512);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < out.getNumSamples(); pos += 512) {
        const auto n = std::min(512, out.getNumSamples() - pos);
        io.setSize(7, n, false, false, true);
        io.clear();
        const auto available = std::clamp(input.getNumSamples() - pos, 0, n);
        if (available > 0) {
            io.copyFrom(0, 0, input, 0, pos, available);
        }
        p.processBlock(io, midi);
        for (int c = 0; c < 7; ++c) {
            out.copyFrom(c, pos, io, c, 0, n);
        }
    }
    p.releaseResources();
    juce::AudioBuffer<float> sum(1, out.getNumSamples());
    sum.clear();
    for (int s = 0; s < dsp::kNumStems; ++s) {
        CAPTURE(s);
        // the main output's channel, then two per stem
        CHECK(differenceDb(out, 1 + 2 * s, out, 2 + 2 * s) < -200.f + 1.f);
        CHECK(out.getMagnitude(1 + 2 * s, 0, out.getNumSamples()) > 0.f);
        sum.addFrom(0, 0, out, 1 + 2 * s, 0, out.getNumSamples());
    }
    CHECK(differenceDb(out, 0, sum, 0) < -90.f);
}

TEST_CASE("with only some stem outputs on, those carry their stems", "[engine][stems]") {
    PluginProcessor p;
    setLevels(p, -6.f, -20.f, 3.f);
    const auto input = makeSignal(2, 48000, 48000.0);
    const auto all = run(p, input, 48000.0, {512}, false, true);

    auto layout = makeLayout(juce::AudioChannelSet::stereo(), false);
    layout.outputBuses.getReference(PluginProcessor::kFirstStemBus + dsp::kAmbience) = juce::AudioChannelSet::stereo();
    REQUIRE(p.setBusesLayout(layout));
    p.setNonRealtime(true);
    p.prepareToPlay(48000.0, 512);
    REQUIRE(p.getTotalNumOutputChannels() == 4);
    const auto latency = p.getLatencySamples();
    juce::AudioBuffer<float> out(4, input.getNumSamples() + latency);
    juce::AudioBuffer<float> io(4, 512);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < out.getNumSamples(); pos += 512) {
        const auto n = std::min(512, out.getNumSamples() - pos);
        io.setSize(4, n, false, false, true);
        io.clear();
        for (int c = 0; c < 2; ++c) {
            const auto available = std::clamp(input.getNumSamples() - pos, 0, n);
            if (available > 0) {
                io.copyFrom(c, 0, input, c, pos, available);
            }
        }
        p.processBlock(io, midi);
        for (int c = 0; c < 4; ++c) {
            out.copyFrom(c, pos, io, c, 0, n);
        }
    }
    p.releaseResources();
    for (int c = 0; c < 2; ++c) {
        CHECK(differenceDb(out, 2 + c, all.output, 2 * (1 + dsp::kAmbience) + c) < -110.f);
    }
}

TEST_CASE("the audio thread neither allocates nor locks", "[engine]") {
    const auto stems = GENERATE(false, true);
    CAPTURE(stems);
    PluginProcessor p;
    setLevels(p, 0.f, -20.f, -10.f);
    const auto buffer_channels = prepare(p, 2, 48000.0, 512, true, stems);
    juce::AudioBuffer<float> io(buffer_channels, 512);
    juce::MidiBuffer midi;
    const auto input = makeSignal(2, 512, 48000.0);
    size_t allocations = 0;
    for (int block = 0; block < 400; ++block) {
        for (int c = 0; c < buffer_channels; ++c) {
            io.copyFrom(c, 0, input, c % 2, 0, 512);
        }
        const alloc_counter::Scope scope;
        p.processBlock(io, midi);
        allocations += scope.count();
    }
    p.releaseResources();
    CHECK(allocations == 0);
}

TEST_CASE("releasing does not wait for the worker's backlog", "[engine]") {
    PluginProcessor p;
    const auto buffer_channels = prepare(p, 2, 48000.0, 512, true, true);
    juce::AudioBuffer<float> io(buffer_channels, 512);
    juce::MidiBuffer midi;
    const auto input = makeSignal(2, 512, 48000.0);
    // far faster than real time: the worker falls seconds of hops behind
    for (int block = 0; block < 400; ++block) {
        for (int c = 0; c < buffer_channels; ++c) {
            io.copyFrom(c, 0, input, c % 2, 0, 512);
        }
        p.processBlock(io, midi);
    }
    const auto start = juce::Time::getMillisecondCounterHiRes();
    p.releaseResources();
    // a hop or two, not the backlog (which, past four seconds, would get the worker killed by force)
    CHECK(juce::Time::getMillisecondCounterHiRes() - start < 500.0);
}

TEST_CASE("parameter IDs never change", "[params]") {
    // saved sessions and presets refer to these; this list may only grow
    const juce::StringArray frozen{
        "voice_level", "ambience_level", "reverb_level", "quality", "stereo_mode", "output_gain", "bypass",
        "voice_sens_1", "voice_sens_2", "voice_sens_3", "voice_sens_4", "voice_sens_5", "voice_sens_6",
        "ambience_sens_1", "ambience_sens_2", "ambience_sens_3", "ambience_sens_4", "ambience_sens_5",
        "ambience_sens_6",
        "reverb_sens_1", "reverb_sens_2", "reverb_sens_3", "reverb_sens_4", "reverb_sens_5", "reverb_sens_6"
    };
    PluginProcessor p;
    for (const auto& id : frozen) {
        CAPTURE(id);
        CHECK(p.parameters_.getParameter(id) != nullptr);
    }
    CHECK(params::allIDs() == frozen);
}

TEST_CASE("state round-trips", "[params]") {
    PluginProcessor a;
    setLevels(a, -3.f, -18.f, -40.f);
    setParam(a, params::sensitivityID(dsp::kAmbience, 2), .5f);
    setParam(a, params::kQuality, 0.f);
    setParam(a, params::kStereo, 2.f);
    juce::MemoryBlock state;
    a.getStateInformation(state);
    PluginProcessor b;
    b.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (const auto& id : params::allIDs()) {
        CAPTURE(id);
        CHECK(std::abs(a.parameters_.getParameter(id)->getValue() - b.parameters_.getParameter(id)->getValue()) < 1e-6f);
    }
}

TEST_CASE("every program has a name", "[params]") {
    // the VST3 validator fails a plug-in whose program list has an unnamed program
    PluginProcessor p;
    REQUIRE(p.getNumPrograms() > 0);
    for (int i = 0; i < p.getNumPrograms(); ++i) {
        CHECK(p.getProgramName(i).isNotEmpty());
    }
}

TEST_CASE("level text round-trips, with Off at the bottom", "[params]") {
    PluginProcessor p;
    auto* voice = p.parameters_.getParameter(params::kVoice);
    CHECK(voice->getText(0.f, 32) == "Off");
    CHECK(params::levelToGain(params::kLevelOff) == 0.f);
    CHECK(std::abs(voice->getValueForText("Off")) < 1e-6f);
    CHECK(std::abs(voice->convertFrom0to1(voice->getValueForText("-6.0 dB")) + 6.f) < .05f);
    // what the interface shows reads back
    const auto shown = voice->getText(voice->convertTo0to1(-6.f), 32);
    CHECK(std::abs(voice->convertFrom0to1(voice->getValueForText(shown)) + 6.f) < .05f);
}
