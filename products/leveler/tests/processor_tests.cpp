// Copyright (C) 2026 - Altvane
// This file is part of Altvane Leveler
//
// Altvane Leveler is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Altvane Leveler is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Altvane Leveler. If not, see <https://www.gnu.org/licenses/>.
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "presets.hpp"
#include "signals.hpp"

using namespace test_signals;

namespace {
    class TestPlayHead final : public juce::AudioPlayHead {
    public:
        bool playing{false};

        juce::Optional<PositionInfo> getPosition() const override {
            PositionInfo info;
            info.setIsPlaying(playing);
            return info;
        }
    };

    class GestureCounter final : public juce::AudioProcessorParameter::Listener {
    public:
        int begins{0}, ends{0}, changes{0};

        void parameterValueChanged(int, float) override { ++changes; }

        void parameterGestureChanged(int, const bool starting) override { ++(starting ? begins : ends); }
    };

    void process(PluginProcessor& p, const juce::AudioBuffer<float>& input, const double rate, const int block,
                 const std::function<void(int)>& after_block = {}) {
        juce::AudioBuffer<float> io(input.getNumChannels(), block);
        juce::MidiBuffer midi;
        for (int pos = 0; pos + block <= input.getNumSamples(); pos += block) {
            for (int c = 0; c < input.getNumChannels(); ++c) {
                io.copyFrom(c, 0, input, c, pos, block);
            }
            p.processBlock(io, midi);
            if (after_block) {
                after_block(pos);
            }
        }
        juce::ignoreUnused(rate);
    }

    juce::MemoryBlock stateOf(PluginProcessor& p) {
        juce::MemoryBlock state;
        p.getStateInformation(state);
        return state;
    }
}

TEST_CASE("parameter IDs are frozen, and the rider fader is the one parameter outside the tree", "[processor]") {
    const juce::StringArray frozen{
        "target", "sensitivity", "speed", "guard", "music", "range_min", "range_max", "idle", "lookahead", "output",
        "bypass"
    };
    CHECK(params::allIDs() == frozen);
    PluginProcessor p;
    for (const auto& id : frozen) {
        CHECK(p.parameters_.getParameter(id) != nullptr);
    }
    CHECK(p.parameters_.getParameter(params::kRide) == nullptr);
    REQUIRE(p.getRide() != nullptr);
    CHECK(p.getRide()->getParameterID() == "ride");
    CHECK(p.getRide()->isAutomatable());
    CHECK(p.getParameters().size() == frozen.size() + 1);
}

TEST_CASE("the session state keeps the parameters, the automation mode and the rider fader", "[processor]") {
    PluginProcessor p;
    setParam(p, params::kTarget, -14.5f);
    setParam(p, params::kGuard, .25f);
    p.setMode(dsp::Mode::kRead);
    *p.getRide() = 3.25f;
    const auto state = stateOf(p);

    PluginProcessor other;
    other.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(std::abs(other.parameters_.getRawParameterValue(params::kTarget)->load() + 14.5f) < .01f);
    CHECK(std::abs(other.parameters_.getRawParameterValue(params::kGuard)->load() - .25f) < .01f);
    CHECK(other.getMode() == dsp::Mode::kRead);
    CHECK(std::abs(other.getRide()->get() - 3.25f) < .01f);
    // the session part never ends up in the parameter tree
    CHECK(!other.parameters_.state.getChildWithName("Session").isValid());
}

TEST_CASE("restoring a session moves the rider fader without writing automation", "[processor]") {
    PluginProcessor source;
    source.setMode(dsp::Mode::kRead);
    *source.getRide() = -6.5f;
    const auto state = stateOf(source);

    PluginProcessor p;
    struct HostListener final : juce::AudioProcessorListener {
        int changes{0}, gestures{0};
        void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override { ++changes; }
        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { ++gestures; }
    } host;
    p.addListener(&host);
    p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    p.removeListener(&host);
    // the host was not told of a change it could record as automation (a session load is no edit)
    CHECK(host.changes == 0);
    CHECK(host.gestures == 0);
    CHECK(std::abs(p.getRide()->get() + 6.5f) < .01f);

    // and the engine follows it in Read: the restored value is applied to the audio
    constexpr double rate = 48000.0;
    p.setRateAndBufferSizeDetails(rate, 480);
    p.prepareToPlay(rate, 480);
    juce::AudioBuffer<float> input(2, samples(rate, .2));
    input.clear();
    addVoice(input, rate, 0, input.getNumSamples(), -20.f, false);
    process(p, input, rate, 480);
    CHECK(std::abs(p.getEngine().getGainDb() + 6.5f) < .01f);
    p.releaseResources();
}

TEST_CASE("lookahead, which sets the latency, is not automatable", "[processor]") {
    PluginProcessor p;
    CHECK_FALSE(p.parameters_.getParameter(params::kLookahead)->isAutomatable());
    CHECK(p.parameters_.getParameter(params::kTarget)->isAutomatable());
}

TEST_CASE("a preset never switches the automation mode or moves the rider fader", "[processor]") {
    PluginProcessor source;
    setParam(source, params::kTarget, -30.f);
    source.setMode(dsp::Mode::kWrite);
    *source.getRide() = 6.f;
    const auto codec = altvane::leveler::presetCodec();
    const auto file = juce::File::createTempFile("json");
    REQUIRE(codec.write(file, stateOf(source)).wasOk());
    CHECK(!file.loadFileAsString().contains("Session"));
    juce::MemoryBlock preset;
    REQUIRE(codec.read(file, preset).wasOk());
    file.deleteFile();

    PluginProcessor p;
    p.setMode(dsp::Mode::kRead);
    *p.getRide() = -4.f;
    p.setStateInformation(preset.getData(), static_cast<int>(preset.getSize()));
    CHECK(std::abs(p.parameters_.getRawParameterValue(params::kTarget)->load() + 30.f) < .01f);
    CHECK(p.getMode() == dsp::Mode::kRead);
    CHECK(std::abs(p.getRide()->get() + 4.f) < .01f);
    // and the fader is not part of what a preset is
    CHECK(!codec.includes(*p.getRide()));
    CHECK(codec.includes(*p.parameters_.getParameter(params::kTarget)));
}

TEST_CASE("A/B leaves the rider fader alone", "[processor]") {
    PluginProcessor p;
    *p.getRide() = 2.f;
    p.session_.switchAB(p.parameters_);
    *p.getRide() = 5.f;
    p.session_.switchAB(p.parameters_);
    CHECK(std::abs(p.getRide()->get() - 5.f) < .01f);
}

TEST_CASE("a Write pass sends the rider fader to the host while the transport plays, outside the undo history", "[processor]") {
    PluginProcessor p;
    setParam(p, params::kRangeMin, -12.f);
    setParam(p, params::kRangeMax, 12.f);
    p.session_.undo_manager.clearUndoHistory();
    constexpr double rate = 48000.0;
    constexpr int kBlock = 480;
    p.setRateAndBufferSizeDetails(rate, kBlock);
    p.prepareToPlay(rate, kBlock);
    TestPlayHead head;
    p.setPlayHead(&head);
    GestureCounter counter;
    p.getRide()->addListener(&counter);
    juce::AudioBuffer<float> input(2, samples(rate, 3.0));
    input.clear();
    addVoice(input, rate, 0, input.getNumSamples(), -30.f, true);

    // Off: riding, but nothing reaches the host
    process(p, input, rate, kBlock, [&](int) { p.runTimerForTesting(); });
    CHECK(counter.changes == 0);
    CHECK(p.getEngine().getGainDb() > 5.f);

    // Write, stopped: still nothing
    p.setMode(dsp::Mode::kWrite);
    process(p, input, rate, kBlock, [&](int) { p.runTimerForTesting(); });
    CHECK(counter.begins == 0);
    CHECK(counter.changes == 0);

    // Write, playing: one gesture, with the moves inside it
    head.playing = true;
    p.setPlayHead(&head);
    process(p, input, rate, kBlock, [&](int) { p.runTimerForTesting(); });
    CHECK(counter.begins == 1);
    CHECK(counter.ends == 0);
    CHECK(counter.changes > 0);
    CHECK(p.isWritingAutomation());
    CHECK(std::abs(p.getRide()->get() - p.getEngine().getGainDb()) < .06f);

    // stopping ends the gesture
    head.playing = false;
    process(p, input, rate, kBlock, [&](int) { p.runTimerForTesting(); });
    CHECK(counter.ends == 1);
    CHECK(!p.isWritingAutomation());

    // none of it is undoable
    CHECK(!p.session_.undo_manager.canUndo());
    p.getRide()->removeListener(&counter);
    p.setPlayHead(nullptr);
    p.releaseResources();
}

TEST_CASE("a Write pass ends when the host releases the processor or stops calling it", "[processor]") {
    for (const auto release : {true, false}) {
        CAPTURE(release);
        PluginProcessor p;
        constexpr double rate = 48000.0;
        p.setRateAndBufferSizeDetails(rate, 480);
        p.prepareToPlay(rate, 480);
        TestPlayHead head;
        head.playing = true;
        p.setPlayHead(&head);
        GestureCounter counter;
        p.getRide()->addListener(&counter);
        p.setMode(dsp::Mode::kWrite);
        juce::AudioBuffer<float> input(2, samples(rate, .5));
        input.clear();
        addVoice(input, rate, 0, input.getNumSamples(), -30.f, false);
        process(p, input, rate, 480, [&](int) { p.runTimerForTesting(); });
        REQUIRE(p.isWritingAutomation());
        // the transport never reports stopping
        if (release) {
            p.releaseResources();
            p.runTimerForTesting();
        } else {
            for (int i = 0; i < 20; ++i) {
                p.runTimerForTesting();
            }
        }
        CHECK(!p.isWritingAutomation());
        CHECK(counter.begins == 1);
        CHECK(counter.ends == 1);
        p.getRide()->removeListener(&counter);
        p.setPlayHead(nullptr);
        p.releaseResources();
    }
}

TEST_CASE("changing the automation mode tells the host the session changed; restoring it does not", "[processor]") {
    struct Listener final : juce::AudioProcessorListener {
        int changes{0};
        void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override {
            changes += details.nonParameterStateChanged ? 1 : 0;
        }
    } listener;
    PluginProcessor p;
    juce::MemoryBlock state;
    p.setMode(dsp::Mode::kRead);
    p.getStateInformation(state);
    p.setMode(dsp::Mode::kOff);
    p.addListener(&listener);
    p.setMode(dsp::Mode::kWrite);
    CHECK(listener.changes == 1);
    p.setMode(dsp::Mode::kWrite);
    CHECK(listener.changes == 1);
    p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(p.getMode() == dsp::Mode::kRead);
    CHECK(listener.changes == 1);
    p.removeListener(&listener);
}

TEST_CASE("a hand on the rider fader overrides the engine until it lets go", "[processor]") {
    PluginProcessor p;
    constexpr double rate = 48000.0;
    p.setRateAndBufferSizeDetails(rate, 480);
    p.prepareToPlay(rate, 480);
    juce::AudioBuffer<float> input(2, samples(rate, 1.0));
    input.clear();
    addVoice(input, rate, 0, input.getNumSamples(), -20.f, false);
    p.beginManualRide();
    p.setManualRide(-9.f);
    process(p, input, rate, 480);
    CHECK(std::abs(p.getEngine().getGainDb() + 9.f) < .01f);
    p.endManualRide();
    process(p, input, rate, 480);
    // riding again, back toward 0 dB for a voice already at the target
    CHECK(p.getEngine().getGainDb() > -6.f);
    p.releaseResources();
}

TEST_CASE("the lookahead's latency reaches the host from the message thread", "[processor]") {
    PluginProcessor p;
    p.setRateAndBufferSizeDetails(48000.0, 480);
    p.prepareToPlay(48000.0, 480);
    CHECK(p.getLatencySamples() == dsp::kLookaheadHops * 240);
    // a host that has suspended processing (no blocks) still hears about it
    setParam(p, params::kLookahead, 0.f);
    p.runTimerForTesting();
    CHECK(p.getLatencySamples() == 0);
    // and the audio follows once blocks arrive
    juce::AudioBuffer<float> io(2, 480);
    io.clear();
    juce::MidiBuffer midi;
    p.processBlock(io, midi);
    CHECK(p.getEngine().getLatency() == 0);
    p.releaseResources();
}

TEST_CASE("bus layouts: mono or stereo in and out, the music key off, mono or stereo", "[processor]") {
    PluginProcessor p;
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();
    const auto off = juce::AudioChannelSet::disabled();
    auto layout = [](const juce::AudioChannelSet& in, const juce::AudioChannelSet& key, const juce::AudioChannelSet& out) {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add(in);
        l.inputBuses.add(key);
        l.outputBuses.add(out);
        return l;
    };
    for (const auto& main : {mono, stereo}) {
        for (const auto& key : {off, mono, stereo}) {
            CHECK(p.checkBusesLayoutSupported(layout(main, key, main)));
        }
    }
    CHECK(!p.checkBusesLayoutSupported(layout(mono, off, stereo)));
    CHECK(!p.checkBusesLayoutSupported(layout(stereo, off, mono)));
    CHECK(!p.checkBusesLayoutSupported(layout(juce::AudioChannelSet::create5point1(), off, juce::AudioChannelSet::create5point1())));
}

TEST_CASE("learn measures the voiced loudness", "[processor]") {
    PluginProcessor p;
    constexpr double rate = 48000.0;
    p.setRateAndBufferSizeDetails(rate, 480);
    p.prepareToPlay(rate, 480);
    juce::AudioBuffer<float> input(2, samples(rate, 3.0));
    input.clear();
    addVoice(input, rate, 0, input.getNumSamples(), -27.f, true);
    p.getEngine().startLearn();
    CHECK(p.getEngine().isLearning());
    process(p, input, rate, 480);
    const auto learned = p.getEngine().stopLearn();
    REQUIRE(learned.has_value());
    CHECK(std::abs(*learned + 27.f) < 1.f);
    // silence teaches nothing
    p.getEngine().startLearn();
    juce::AudioBuffer<float> silence(2, samples(rate, 1.0));
    silence.clear();
    process(p, silence, rate, 480);
    CHECK(!p.getEngine().stopLearn().has_value());
    // nor does a take with no audio at all: the totals of the last take are not reported again
    process(p, input, rate, 480);
    p.getEngine().startLearn();
    CHECK(!p.getEngine().stopLearn().has_value());
    p.releaseResources();
}

TEST_CASE("learn starts and stops on the message thread while the audio thread plays", "[processor]") {
    // startLearn only asks the audio thread to start a new take; it never writes the totals the audio thread adds to,
    // so no take mixes in what came before it
    PluginProcessor p;
    constexpr double rate = 48000.0;
    p.setRateAndBufferSizeDetails(rate, 480);
    p.prepareToPlay(rate, 480);
    juce::AudioBuffer<float> loud(2, samples(rate, 2.0)), quiet(2, samples(rate, 2.0));
    loud.clear();
    quiet.clear();
    addVoice(loud, rate, 0, loud.getNumSamples(), -12.f, false);
    addVoice(quiet, rate, 0, quiet.getNumSamples(), -32.f, false);
    std::atomic<bool> quiet_now{false}, stop{false};
    std::atomic<int> blocks{0};
    std::thread audio([&] {
        juce::AudioBuffer<float> io(2, 480);
        juce::MidiBuffer midi;
        for (int pos = 0; !stop.load(); pos = (pos + 480) % (loud.getNumSamples() - 480)) {
            const auto& from = quiet_now.load() ? quiet : loud;
            for (int c = 0; c < 2; ++c) {
                io.copyFrom(c, 0, from, c, pos, 480);
            }
            p.processBlock(io, midi);
            blocks.fetch_add(1);
        }
    });
    const auto waitBlocks = [&](const int n) {
        const auto until = blocks.load() + n;
        while (blocks.load() < until) {
            std::this_thread::yield();
        }
    };
    for (int take = 0; take < 20; ++take) {
        CAPTURE(take);
        // a loud take, then a quiet one started while the loud voice is still playing: once the quiet voice has
        // played for a while, the take reads the quiet voice
        quiet_now = false;
        waitBlocks(100);
        quiet_now = true;
        waitBlocks(30);
        p.getEngine().startLearn();
        waitBlocks(120);
        const auto learned = p.getEngine().stopLearn();
        REQUIRE(learned.has_value());
        CHECK(std::abs(*learned + 32.f) < 1.f);
    }
    stop = true;
    audio.join();
    p.releaseResources();
}
