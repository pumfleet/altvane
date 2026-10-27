// Offline renderer: levels a WAV file through the plug-in, latency-compensated, writes the result and the gain curve,
// and prints how even the voice is before and after.
//
//   AltvaneLevelerRender <in.wav> <out-dir> [--key music.wav] [--target dB] [--sensitivity dB] [--speed %]
//                        [--guard %] [--music %] [--min dB] [--max dB] [--idle dB] [--lookahead on|off] [--block N]
//
// Writes <out-dir>/leveled.wav and <out-dir>/gain.csv (one row per 5 ms hop: time, input level, gain, output level,
// state). The spread is the standard deviation of the 100 ms loudness over the voiced hops.

#include <chrono>
#include <juce_audio_formats/juce_audio_formats.h>

#include "PluginProcessor.hpp"

using altvane::leveler::PluginProcessor;
namespace params = altvane::leveler::params;
namespace dsp = altvane::leveler::dsp;

namespace {
    void setParam(PluginProcessor& p, const char* id, const float plain) {
        auto* para = p.parameters_.getParameter(id);
        para->setValueNotifyingHost(para->convertTo0to1(plain));
    }

    std::optional<juce::AudioBuffer<float>> read(const juce::File& file, double& rate) {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr) {
            return std::nullopt;
        }
        const auto channels = std::min(static_cast<int>(reader->numChannels), 2);
        juce::AudioBuffer<float> buffer(channels, static_cast<int>(reader->lengthInSamples));
        reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, channels > 1);
        rate = reader->sampleRate;
        return buffer;
    }

    bool write(const juce::File& file, const juce::AudioBuffer<float>& buffer, const double rate) {
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream(file.createOutputStream().release());
        if (stream == nullptr) {
            return false;
        }
        auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}.withSampleRate(rate)
                                                                               .withNumChannels(buffer.getNumChannels())
                                                                               .withBitsPerSample(24));
        return writer != nullptr && writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    }

    double spread(const std::vector<float>& values) {
        if (values.size() < 2) {
            return 0.0;
        }
        double mean = 0.0;
        for (const auto v : values) {
            mean += v;
        }
        mean /= static_cast<double>(values.size());
        double var = 0.0;
        for (const auto v : values) {
            var += (v - mean) * (v - mean);
        }
        return std::sqrt(var / static_cast<double>(values.size() - 1));
    }
}

int main(int argc, char* argv[]) {
    const juce::ScopedJuceInitialiser_GUI juce_init;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) {
        args.add(argv[i]);
    }
    if (args.size() < 2) {
        std::cerr << "usage: AltvaneLevelerRender <in.wav> <out-dir> [--key music.wav] [--target dB] [--sensitivity dB]"
                     " [--speed %] [--guard %] [--music %] [--min dB] [--max dB] [--idle dB] [--lookahead on|off]"
                     " [--block N]\n";
        return 1;
    }
    auto option = [&](const juce::String& name, const juce::String& fallback) {
        const auto i = args.indexOf(name);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : fallback;
    };
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto out_dir = cwd.getChildFile(args[1]);
    out_dir.createDirectory();

    double rate = 0.0;
    const auto input = read(cwd.getChildFile(args[0]), rate);
    if (!input) {
        std::cerr << "cannot read " << args[0] << "\n";
        return 1;
    }
    std::optional<juce::AudioBuffer<float>> key;
    if (const auto key_path = option("--key", {}); key_path.isNotEmpty()) {
        double key_rate = 0.0;
        key = read(cwd.getChildFile(key_path), key_rate);
        if (!key || std::abs(key_rate - rate) > .5) {
            std::cerr << "cannot read " << key_path << " (or its sample rate differs)\n";
            return 1;
        }
    }
    const auto channels = input->getNumChannels();
    const auto key_channels = key ? key->getNumChannels() : 0;
    const auto block = option("--block", "512").getIntValue();
    if (block < 1) {
        std::cerr << "--block must be a positive number of samples\n";
        return 1;
    }

    PluginProcessor p;
    setParam(p, params::kTarget, option("--target", "-20").getFloatValue());
    setParam(p, params::kSensitivity, option("--sensitivity", "0").getFloatValue());
    setParam(p, params::kSpeed, option("--speed", "50").getFloatValue() / 100.f);
    setParam(p, params::kGuard, option("--guard", "70").getFloatValue() / 100.f);
    setParam(p, params::kMusic, option("--music", "0").getFloatValue() / 100.f);
    setParam(p, params::kRangeMin, option("--min", "-6").getFloatValue());
    setParam(p, params::kRangeMax, option("--max", "6").getFloatValue());
    setParam(p, params::kIdle, option("--idle", "0").getFloatValue());
    setParam(p, params::kLookahead, option("--lookahead", "on") == "off" ? 0.f : 1.f);

    auto layout = p.getBusesLayout();
    layout.inputBuses.getReference(0) = juce::AudioChannelSet::canonicalChannelSet(channels);
    layout.outputBuses.getReference(0) = juce::AudioChannelSet::canonicalChannelSet(channels);
    layout.inputBuses.getReference(1) = key_channels > 0 ? juce::AudioChannelSet::canonicalChannelSet(key_channels)
                                                         : juce::AudioChannelSet::disabled();
    if (!p.setBusesLayout(layout)) {
        std::cerr << "unsupported channel layout\n";
        return 1;
    }
    p.setRateAndBufferSizeDetails(rate, block);
    p.setNonRealtime(true);
    p.prepareToPlay(rate, block);
    auto& engine = p.getEngine();
    engine.setPublishing(true);
    const auto latency = p.getLatencySamples();
    const auto total = input->getNumSamples() + latency;

    juce::AudioBuffer<float> out(channels, total);
    juce::AudioBuffer<float> io(channels + key_channels, block);
    juce::MidiBuffer midi;
    std::vector<dsp::Column> columns;
    dsp::Column column;
    const auto start = std::chrono::steady_clock::now();
    for (int pos = 0; pos < total; pos += block) {
        const auto n = std::min(block, total - pos);
        io.setSize(channels + key_channels, n, false, false, true);
        io.clear();
        const auto available = std::clamp(input->getNumSamples() - pos, 0, n);
        for (int c = 0; c < channels && available > 0; ++c) {
            io.copyFrom(c, 0, *input, c, pos, available);
        }
        for (int c = 0; c < key_channels && available > 0; ++c) {
            io.copyFrom(channels + c, 0, *key, c, pos, std::min(available, std::max(key->getNumSamples() - pos, 0)));
        }
        p.processBlock(io, midi);
        for (int c = 0; c < channels; ++c) {
            out.copyFrom(c, pos, io, c, 0, n);
        }
        while (engine.popColumn(column)) {
            columns.push_back(column);
        }
    }
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    p.releaseResources();

    juce::AudioBuffer<float> aligned(channels, input->getNumSamples());
    for (int c = 0; c < channels; ++c) {
        aligned.copyFrom(c, 0, out, c, latency, input->getNumSamples());
    }
    if (!write(out_dir.getChildFile("leveled.wav"), aligned, rate)) {
        std::cerr << "cannot write leveled.wav\n";
        return 1;
    }

    // columns are lined up with the output: drop the ones that describe the latency's silence
    const auto hop = engine.getHopSamples();
    const auto skip = static_cast<size_t>(latency / hop);
    juce::String csv = "time,input_db,gain_db,output_db,state\n";
    std::vector<float> before, after;
    for (size_t i = skip; i < columns.size(); ++i) {
        const auto& c = columns[i];
        const auto t = static_cast<double>((i - skip + 1) * static_cast<size_t>(hop)) / rate;
        static constexpr const char* kStates[] = {"silent", "unvoiced", "voiced"};
        csv << juce::String(t, 3) << "," << juce::String(c.in_db, 2) << "," << juce::String(c.gain_db, 2) << ","
                << juce::String(c.out_db, 2) << "," << kStates[static_cast<int>(c.state)] << "\n";
        if (c.state == dsp::HopState::kVoiced) {
            before.push_back(c.in_db);
            after.push_back(c.in_db + c.gain_db);
        }
    }
    out_dir.getChildFile("gain.csv").replaceWithText(csv);

    const auto audio_seconds = static_cast<double>(input->getNumSamples()) / rate;
    std::cout << "voiced level spread: " << juce::String(spread(before), 2) << " dB before, "
              << juce::String(spread(after), 2) << " dB after (" << before.size() << " voiced hops)\n";
    std::cout << juce::String(seconds, 2) << " s for " << juce::String(audio_seconds, 2) << " s of audio ("
              << juce::String(100.0 * seconds / audio_seconds, 2) << "% of real time), latency " << latency
              << " samples (" << juce::String(1000.0 * latency / rate, 1) << " ms)\n";
    return 0;
}
