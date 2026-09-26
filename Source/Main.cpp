#include <JuceHeader.h>
#include "MainComponent.h"
#include <creation/ui/CreationSuiteLogos.h>
#include <creation/ui/SuiteJUCEApplication.h>

namespace
{
// The splash stays up this long, with its progress bar filling steadily, so people actually see it.
constexpr int splashVisibleMs = 10000;
constexpr int splashTickMs = 50;
}

// The shared suite application base gives Texture the suite splash at startup, the storage-folder check, and the
// About box behind the header's "?" button (the same splash panel, with a Close button).
class CreationTextureApplication final : public creation::ui::SuiteJUCEApplication
{
public:
    // The splash appears as soon as the app starts, so the bar is timed from here.
    CreationTextureApplication()
        : SuiteJUCEApplication(creation::ui::SuiteLogoId::texture, splashVisibleMs),
          splashStartedMs(juce::Time::getMillisecondCounter())
    {
    }

    const juce::String getApplicationName() override { return "Djehuti Texture"; }
    const juce::String getApplicationVersion() override { return "0.0.1"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void systemRequestedQuit() override
    {
        splashProgress.stopTimer();
        quit();
    }

protected:
    std::unique_ptr<juce::DocumentWindow> createMainWindow() override
    {
        auto window = std::make_unique<MainWindow>(getApplicationName());
        splashProgress.startTimer(splashTickMs);
        return window;
    }

private:
    // Fills the splash's progress bar over the whole visible time, stepping through what the app sets up.
    void advanceSplashProgress()
    {
        static const juce::StringArray steps {
            "Preparing the texture workspace...",
            "Loading material nodes...",
            "Starting the 3D preview...",
            "Loading FRust image tools...",
            "Opening the project...",
            "Ready."
        };

        const auto elapsed = static_cast<float>(juce::Time::getMillisecondCounter() - splashStartedMs);
        const auto progress = juce::jlimit(0.0f, 1.0f, elapsed / static_cast<float>(splashVisibleMs));
        const auto step = juce::jmin(steps.size() - 1, static_cast<int>(progress * static_cast<float>(steps.size())));
        reportSplashProgress(steps[step], progress);

        if (progress >= 1.0f)
            splashProgress.stopTimer();
    }

    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(juce::String name)
            : juce::DocumentWindow(name,
                                   juce::Desktop::getInstance().getDefaultLookAndFeel()
                                       .findColour(juce::ResizableWindow::backgroundColourId),
                                   juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setIcon(creation::ui::getSuiteLogoImage(creation::ui::SuiteLogoId::texture));
            setContentOwned(new MainComponent(), true);
            centreWithSize(getWidth(), getHeight());
            setResizable(true, true);
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    juce::uint32 splashStartedMs = 0;
    juce::TimedCallback splashProgress { [this] { advanceSplashProgress(); } };
};

START_JUCE_APPLICATION(CreationTextureApplication)
