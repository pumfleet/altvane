// Renders the Altvane Leveler editor headlessly with the real processor running, and writes PNG snapshots for
// visual review. Give it a vocal recording (and optionally the music) for a realistic picture; without one it uses a
// synthetic voice: phrases at uneven levels with breaths between them.
//
// usage: AltvaneLevelerSnapshot <output-dir> [scale] [vocal.wav [music.wav]]

#include <juce_audio_formats/juce_audio_formats.h>

#include "PluginProcessor.hpp"
#include "ui/editor.hpp"
#if JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif

using altvane::leveler::PluginProcessor;
namespace params = altvane::leveler::params;
namespace dsp = altvane::leveler::dsp;

namespace {
    constexpr double kPi = juce::MathConstants<double>::pi;

    class PlayHead final : public juce::AudioPlayHead {
    public:
        bool playing{false};

        juce::Optional<PositionInfo> getPosition() const override {
            PositionInfo info;
            info.setIsPlaying(playing);
            return info;
        }
    };

    void set(PluginProcessor& p, const juce::String& id, const float plain) {
        auto* para = p.parameters_.getParameter(id);
        para->setValueNotifyingHost(para->convertTo0to1(plain));
    }

    void writePNG(juce::Component& c, const juce::File& file, const float scale) {
        const auto image = c.createComponentSnapshot(c.getLocalBounds(), true, scale);
        file.deleteFile();
        juce::FileOutputStream out(file);
        juce::PNGImageFormat png;
        png.writeImageToStream(image, out);
        std::cout << "wrote " << file.getFullPathName() << std::endl;
    }

    std::optional<juce::AudioBuffer<float>> read(const char* path) {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        const std::unique_ptr<juce::AudioFormatReader> reader(
            formats.createReaderFor(juce::File::getCurrentWorkingDirectory().getChildFile(path)));
        if (reader == nullptr) {
            return std::nullopt;
        }
        juce::AudioBuffer<float> buffer(2, static_cast<int>(reader->lengthInSamples));
        reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
        return buffer;
    }

    /**
     * sung phrases at uneven levels (syllables with short gaps), a breath before each, and a little room noise
     */
    juce::AudioBuffer<float> syntheticVoice(const int length, const double rate) {
        juce::AudioBuffer<float> buffer(2, length);
        juce::Random random(3);
        struct Phrase {
            double start, end, gain, f0;
        };
        const std::array<Phrase, 4> phrases{{{.3, 2.4, .05, 196.0}, {2.9, 4.6, .32, 247.0}, {5.3, 7.0, .1, 220.0}, {7.4, 9.6, .2, 175.0}}};
        double phase = 0.0;
        for (int i = 0; i < length; ++i) {
            const auto t = static_cast<double>(i) / rate;
            double x = .0015 * (random.nextDouble() * 2.0 - 1.0);
            for (const auto& ph : phrases) {
                if (t >= ph.start && t < ph.end) {
                    const auto u = std::fmod(t - ph.start, .32);
                    const auto env = u < .24 ? std::min(1.0, std::min(u, .24 - u) / .015) : 0.0;
                    phase += ph.f0 * (1.0 + .012 * std::sin(2.0 * kPi * 5.5 * t)) / rate;
                    double v = 0.0;
                    for (int h = 1; h * ph.f0 < 5000.0; ++h) {
                        const auto f = h * ph.f0;
                        const auto formant = std::exp(-std::pow((f - 700.0) / 400.0, 2.0)) + .6 * std::exp(-std::pow((f - 2300.0) / 600.0, 2.0));
                        v += std::sin(2.0 * kPi * phase * h) * (.15 + formant) / h;
                    }
                    // syllables swell and fade a little within the phrase
                    x += v * env * ph.gain * (.75 + .25 * std::sin(2.0 * kPi * .7 * (t - ph.start)));
                } else if (t >= ph.start - .35 && t < ph.start - .05) {
                    x += .02 * (random.nextDouble() * 2.0 - 1.0) * std::sin(kPi * (t - ph.start + .35) / .3);
                }
            }
            buffer.setSample(0, i, static_cast<float>(x));
            buffer.setSample(1, i, static_cast<float>(x * .97));
        }
        return buffer;
    }

    juce::AudioBuffer<float> syntheticMusic(const int length, const double rate) {
        juce::AudioBuffer<float> buffer(2, length);
        juce::Random random(5);
        for (int i = 0; i < length; ++i) {
            const auto t = static_cast<double>(i) / rate;
            // the chorus comes in louder halfway through
            const auto level = t < 5.0 ? .03 : .08;
            double x = .3 * (random.nextDouble() * 2.0 - 1.0);
            for (const auto f : {110.0, 164.8, 220.0, 277.2}) {
                x += std::sin(2.0 * kPi * f * t);
            }
            buffer.setSample(0, i, static_cast<float>(x * level));
            buffer.setSample(1, i, static_cast<float>(x * level * .9));
        }
        return buffer;
    }
}

int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI gui;
    const auto out_dir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1])
                                  : juce::File::getCurrentWorkingDirectory();
    const auto scale_factor = argc > 2 ? static_cast<float>(std::atof(argv[2])) : 2.f;
    out_dir.createDirectory();

    constexpr double kSampleRate = 48000.0;
    constexpr int kBlock = 480;
    auto voice = argc > 3 ? read(argv[3]) : std::optional<juce::AudioBuffer<float>>(syntheticVoice(static_cast<int>(kSampleRate * 10.0), kSampleRate));
    auto music = argc > 4 ? read(argv[4]) : std::optional<juce::AudioBuffer<float>>(syntheticMusic(voice ? voice->getNumSamples() : 1, kSampleRate));
    if (!voice || !music) {
        std::cerr << "cannot read the audio files" << std::endl;
        return 1;
    }

    PluginProcessor p;
    auto layout = p.getBusesLayout();
    layout.inputBuses.getReference(1) = juce::AudioChannelSet::stereo();
    p.setBusesLayout(layout);
    p.setRateAndBufferSizeDetails(kSampleRate, kBlock);
    p.prepareToPlay(kSampleRate, kBlock);
    PlayHead head;
    p.setPlayHead(&head);
    set(p, params::kTarget, -20.f);

    std::unique_ptr<juce::AudioProcessorEditor> editor_base(p.createEditor());
    auto* editor = dynamic_cast<altvane::leveler::Editor*>(editor_base.get());
    if (editor == nullptr) {
        std::cerr << "editor is not an Altvane Leveler editor" << std::endl;
        return 1;
    }
    editor->setSize(960, 620);
    // a headless editor is never shown, so start the history by hand
    p.getEngine().setPublishing(true);

    juce::AudioBuffer<float> buffer(4, kBlock);
    juce::MidiBuffer midi;
    double t = 0.0, audio_time = 0.0;
    int pos = 0;
    auto run = [&](const double seconds) {
        const auto frames = static_cast<int>(seconds * 60.0);
        for (int f = 0; f < frames; ++f) {
            t += 1.0 / 60.0;
            while (audio_time < t) {
                for (int i = 0; i < kBlock; ++i) {
                    const auto at = (pos + i) % voice->getNumSamples();
                    for (int c = 0; c < 2; ++c) {
                        buffer.setSample(c, i, voice->getSample(c, at));
                        buffer.setSample(2 + c, i, music->getSample(c, at % music->getNumSamples()));
                    }
                }
                pos += kBlock;
                p.processBlock(buffer, midi);
                audio_time += kBlock / kSampleRate;
            }
            p.runTimerForTesting();
            editor->runFrameForTesting(t);
#if JUCE_MAC
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, .002, false);
#endif
        }
    };

    run(8.6);
    writePNG(*editor, out_dir.getChildFile("01_overview.png"), scale_factor);

    p.setMode(dsp::Mode::kWrite);
    head.playing = true;
    run(1.5);
    writePNG(*editor, out_dir.getChildFile("02_write.png"), scale_factor);
    head.playing = false;

    p.setMode(dsp::Mode::kRead);
    *p.getRide() = 3.f;
    run(1.5);
    writePNG(*editor, out_dir.getChildFile("03_read.png"), scale_factor);
    p.setMode(dsp::Mode::kOff);

    set(p, params::kMusic, .6f);
    set(p, params::kRangeMin, -12.f);
    set(p, params::kRangeMax, 12.f);
    run(8.0);
    writePNG(*editor, out_dir.getChildFile("04_music_key.png"), scale_factor);

    editor->setSize(760, 520);
    run(.5);
    writePNG(*editor, out_dir.getChildFile("05_small_window.png"), scale_factor);
    editor->setSize(960, 620);

    editor->openPresetsForTesting();
    run(.5);
    writePNG(*editor, out_dir.getChildFile("06_preset_browser.png"), scale_factor);
    editor->closeSheetsForTesting();
    editor->openSettingsForTesting();
    run(.5);
    writePNG(*editor, out_dir.getChildFile("07_settings.png"), scale_factor);
    editor->closeSheetsForTesting();
    run(.3);

    editor_base.reset();
    p.setPlayHead(nullptr);
    p.releaseResources();
    return 0;
}
