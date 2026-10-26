// Copyright (C) 2026 - Contour contributors, based on ZLEqualizer by zsliu98
// This file is part of Contour
//
// Contour is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License Version 3 as published by the Free Software Foundation.
//
// Contour is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License along with Contour. If not, see <https://www.gnu.org/licenses/>.
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

/**
 * the processor and editor need JUCE (message manager, desktop) for the whole run;
 * scoping it to the run gives a clean shutdown before JUCE's leak detector checks
 */
class JuceRunListener final : public Catch::EventListenerBase {
public:
    using EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo&) override {
        juce_ = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
    }

    void testRunEnded(const Catch::TestRunStats&) override {
        juce_.reset();
    }

private:
    std::unique_ptr<juce::ScopedJuceInitialiser_GUI> juce_;
};

CATCH_REGISTER_LISTENER(JuceRunListener)
