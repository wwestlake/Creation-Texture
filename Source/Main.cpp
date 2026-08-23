#include <JuceHeader.h>
#include "MainComponent.h"
#include <creation/ui/CreationSuiteLogos.h>
#include <creation/ui/SuiteJUCEApplication.h>

class CreationTextureApplication final : public creation::ui::SuiteJUCEApplication
{
public:
    CreationTextureApplication() : SuiteJUCEApplication(creation::ui::SuiteLogoId::texture) {}

    const juce::String getApplicationName() override { return "Creation Texture"; }
    const juce::String getApplicationVersion() override { return "0.0.1"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void systemRequestedQuit() override
    {
        quit();
    }

protected:
    std::unique_ptr<juce::DocumentWindow> createMainWindow() override
    {
        return std::make_unique<MainWindow>(getApplicationName());
    }

private:
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
};

START_JUCE_APPLICATION(CreationTextureApplication)
