#include "MainComponent.h"

#include "Branding.h"
#include "../Language/AppLanguagePolicy.h"
#include <creation/ui/CreationSuiteLogos.h>
#include <gl/GL.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../../CreationEngine/third_party/stb_image.h"

using namespace juce::gl;

namespace
{
constexpr auto textureProjectRoot = "texture/";
constexpr auto textureSessionEntry = "texture/session/texture-session.xml";
constexpr auto texturePreviewImportRoot = "texture/preview/imported-textures/";
constexpr auto texturePreviewDerivedRoot = "texture/preview/derived/";
constexpr auto legacyTextureSessionEntry = "texture-session.xml";
constexpr auto legacyTexturePreviewImportRoot = "preview/imported-textures/";

void configureSummaryBox(juce::TextEditor& editor)
{
    editor.setMultiLine(true);
    editor.setReadOnly(true);
    editor.setScrollbarsShown(true);
    editor.setCaretVisible(false);
    editor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff121a24));
    editor.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff314155));
    editor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
}

void configureModeButton(juce::TextButton& button)
{
    button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a2431));
    button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2b6ca3));
    button.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd7e6f6));
    button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
}

void configureAdjustmentSlider(juce::Slider& slider, double min, double max, double value)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 20);
    slider.setRange(min, max, 0.01);
    slider.setValue(value, juce::dontSendNotification);
}

int workspaceModeIndex(MainComponent::WorkspaceMode mode)
{
    return static_cast<int>(mode);
}

juce::String workspaceModeName(MainComponent::WorkspaceMode mode)
{
    switch (mode)
    {
        case MainComponent::WorkspaceMode::preview: return "Preview";
        case MainComponent::WorkspaceMode::procedural: return "Procedural";
        case MainComponent::WorkspaceMode::maps: return "Maps";
        case MainComponent::WorkspaceMode::adjustments: return "Adjustments";
        case MainComponent::WorkspaceMode::utilities: return "Utilities";
    }

    return "Workspace";
}

class ManagedDocumentWindow final : public juce::DocumentWindow
{
public:
    ManagedDocumentWindow(const juce::String& title,
                          juce::Colour backgroundColour,
                          int requiredButtons,
                          std::function<void()> onCloseCallback)
        : juce::DocumentWindow(title, backgroundColour, requiredButtons),
          onClose(std::move(onCloseCallback))
    {
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        auto closeCallback = onClose;
        juce::MessageManager::callAsync([closeCallback]() mutable
        {
            if (closeCallback)
                closeCallback();
        });
    }

private:
    std::function<void()> onClose;
};
}

MainComponent::ViewModeBar::ViewModeBar()
{
    titleLabel.setText("Modes", juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8da3c0));
    addAndMakeVisible(titleLabel);

    auto setupButton = [this](juce::TextButton& button, WorkspaceMode mode)
    {
        configureModeButton(button);
        button.onClick = [this, mode]
        {
            if (onModeSelected)
                onModeSelected(mode);
        };
        addAndMakeVisible(button);
    };

    setupButton(previewButton, WorkspaceMode::preview);
    setupButton(proceduralButton, WorkspaceMode::procedural);
    setupButton(mapsButton, WorkspaceMode::maps);
    setupButton(adjustmentsButton, WorkspaceMode::adjustments);
    setupButton(utilitiesButton, WorkspaceMode::utilities);

    configureModeButton(popOutButton);
    popOutButton.setTooltip("Pop the current texture workspace out into its own window");
    popOutButton.onClick = [this]
    {
        if (onPopOutRequested)
            onPopOutRequested();
    };
    addAndMakeVisible(popOutButton);

    setActiveMode(WorkspaceMode::preview);
}

void MainComponent::ViewModeBar::setActiveMode(WorkspaceMode newMode)
{
    activeMode = newMode;
    previewButton.setToggleState(activeMode == WorkspaceMode::preview, juce::dontSendNotification);
    proceduralButton.setToggleState(activeMode == WorkspaceMode::procedural, juce::dontSendNotification);
    mapsButton.setToggleState(activeMode == WorkspaceMode::maps, juce::dontSendNotification);
    adjustmentsButton.setToggleState(activeMode == WorkspaceMode::adjustments, juce::dontSendNotification);
    utilitiesButton.setToggleState(activeMode == WorkspaceMode::utilities, juce::dontSendNotification);
    repaint();
}

void MainComponent::ViewModeBar::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xff10141a));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 12.0f);
    g.setColour(juce::Colour(0xff263140));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 12.0f, 1.0f);
    g.setColour(juce::Colour(0xff8ea0b7));
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText(workspaceModeName(activeMode) + " active",
               getLocalBounds().reduced(12, 0),
               juce::Justification::centredRight,
               true);
}

void MainComponent::ViewModeBar::resized()
{
    auto area = getLocalBounds().reduced(14, 8);
    titleLabel.setBounds(area.removeFromLeft(80));
    area.removeFromLeft(8);
    popOutButton.setBounds(area.removeFromRight(92));
    area.removeFromRight(8);

    constexpr int gap = 8;
    const int buttonWidth = (area.getWidth() - (gap * 4)) / 5;
    previewButton.setBounds(area.removeFromLeft(buttonWidth));
    area.removeFromLeft(gap);
    proceduralButton.setBounds(area.removeFromLeft(buttonWidth));
    area.removeFromLeft(gap);
    mapsButton.setBounds(area.removeFromLeft(buttonWidth));
    area.removeFromLeft(gap);
    adjustmentsButton.setBounds(area.removeFromLeft(buttonWidth));
    area.removeFromLeft(gap);
    utilitiesButton.setBounds(area);
}

MainComponent::WorkspacePanel::WorkspacePanel(const juce::String& title)
{
    titleLabel.setText(title, juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(22.0f)).boldened());
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel);

    configureSummaryBox(summaryEditor);
    addAndMakeVisible(summaryEditor);
}

void MainComponent::WorkspacePanel::setSummaryText(const juce::String& text)
{
    summaryEditor.setText(text, juce::dontSendNotification);
}

void MainComponent::WorkspacePanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff131c27));
    g.fillRoundedRectangle(bounds, 18.0f);
    g.setColour(juce::Colour(0xff314155));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 18.0f, 1.0f);
}

void MainComponent::WorkspacePanel::resized()
{
    auto area = getLocalBounds().reduced(18);
    titleLabel.setBounds(area.removeFromTop(30));
    area.removeFromTop(10);
    summaryEditor.setBounds(area);
}

class MainComponent::PreviewWorkspacePanel::Viewport final : public juce::Component,
                                                             private juce::OpenGLRenderer
{
public:
    enum class Primitive
    {
        plane = 0,
        cube,
        sphere,
        cylinder
    };

    Viewport()
    {
        openGLContext.setRenderer(this);
        openGLContext.attachTo(*this);
    }

    ~Viewport() override
    {
        openGLContext.detach();
    }

    void setPrimitive(Primitive newPrimitive)
    {
        const juce::ScopedLock lock(stateLock);
        primitive = newPrimitive;
    }

    Primitive getPrimitive() const noexcept
    {
        const juce::ScopedLock lock(stateLock);
        return primitive;
    }

    void setTextureImage(const juce::Image& newImage)
    {
        const juce::ScopedLock lock(stateLock);
        pendingImage = newImage;
        textureDirty = true;
    }

    void refreshNow()
    {
        openGLContext.triggerRepaint();
    }

    bool hasTexture() const noexcept
    {
        const juce::ScopedLock lock(stateLock);
        return ! pendingImage.isNull() || textureId != 0;
    }

private:
    void newOpenGLContextCreated() override
    {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_TEXTURE_2D);
        glShadeModel(GL_SMOOTH);
        glClearColor(0.06f, 0.09f, 0.13f, 1.0f);
    }

    void openGLContextClosing() override
    {
        releaseTexture();
    }

    void renderOpenGL() override
    {
        jassert(juce::OpenGLHelpers::isContextActive());

        uploadPendingTextureIfNeeded();

        auto scale = (float) openGLContext.getRenderingScale();
        glViewport(0, 0, juce::roundToInt(scale * (float) getWidth()), juce::roundToInt(scale * (float) getHeight()));
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        auto aspect = getHeight() > 0 ? (float) getWidth() / (float) getHeight() : 1.0f;
        setupProjection(aspect);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(0.0f, 0.0f, -3.25f);
        glRotatef(18.0f, 1.0f, 0.0f, 0.0f);
        glRotatef(rotationDegrees, 0.0f, 1.0f, 0.0f);

        drawBackdrop();
        bindTextureOrFallback();

        Primitive primitiveToDraw = Primitive::plane;
        {
            const juce::ScopedLock lock(stateLock);
            primitiveToDraw = primitive;
        }

        switch (primitiveToDraw)
        {
            case Primitive::plane: drawPlane(); break;
            case Primitive::cube: drawCube(); break;
            case Primitive::sphere: drawSphere(); break;
            case Primitive::cylinder: drawCylinder(); break;
        }

        glBindTexture(GL_TEXTURE_2D, 0);
        rotationDegrees = std::fmod(rotationDegrees + 0.35f, 360.0f);
    }

    void setupProjection(float aspect)
    {
        constexpr float nearPlane = 0.1f;
        constexpr float farPlane = 50.0f;
        constexpr float fovDegrees = 45.0f;

        const float top = std::tan(fovDegrees * 0.5f * juce::MathConstants<float>::pi / 180.0f) * nearPlane;
        const float bottom = -top;
        const float right = top * aspect;
        const float left = -right;

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustum(left, right, bottom, top, nearPlane, farPlane);
    }

    void drawBackdrop()
    {
        glDisable(GL_TEXTURE_2D);
        glBegin(GL_QUADS);
        glColor3f(0.10f, 0.13f, 0.18f);
        glVertex3f(-4.0f, -2.5f, -2.0f);
        glVertex3f(4.0f, -2.5f, -2.0f);
        glColor3f(0.03f, 0.05f, 0.08f);
        glVertex3f(4.0f, 2.5f, -2.0f);
        glVertex3f(-4.0f, 2.5f, -2.0f);
        glEnd();
        glEnable(GL_TEXTURE_2D);
        glColor3f(1.0f, 1.0f, 1.0f);
    }

    void bindTextureOrFallback()
    {
        if (textureId != 0)
        {
            glBindTexture(GL_TEXTURE_2D, textureId);
            return;
        }

        if (fallbackTextureId == 0)
            createFallbackTexture();

        glBindTexture(GL_TEXTURE_2D, fallbackTextureId);
    }

    void uploadPendingTextureIfNeeded()
    {
        juce::Image imageToUpload;
        {
            const juce::ScopedLock lock(stateLock);
            if (! textureDirty)
                return;

            textureDirty = false;
            imageToUpload = pendingImage;
        }

        releaseTexture();

        if (imageToUpload.isNull())
            return;

        auto rgba = imageToUpload.convertedToFormat(juce::Image::ARGB);
        const auto width = rgba.getWidth();
        const auto height = rgba.getHeight();
        juce::HeapBlock<juce::uint8> pixels((size_t) width * (size_t) height * 4);

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                auto colour = rgba.getPixelAt(x, y);
                const auto pixelIndex = ((size_t) y * (size_t) width + (size_t) x) * 4;
                pixels[pixelIndex + 0] = colour.getRed();
                pixels[pixelIndex + 1] = colour.getGreen();
                pixels[pixelIndex + 2] = colour.getBlue();
                pixels[pixelIndex + 3] = colour.getAlpha();
            }
        }

        glGenTextures(1, &textureId);
        glBindTexture(GL_TEXTURE_2D, textureId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.getData());
    }

    void releaseTexture()
    {
        if (textureId != 0)
        {
            glDeleteTextures(1, &textureId);
            textureId = 0;
        }
    }

    void createFallbackTexture()
    {
        constexpr int size = 64;
        juce::uint8 pixels[size * size * 4] {};

        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                const bool light = ((x / 8) + (y / 8)) % 2 == 0;
                const auto pixelIndex = (y * size + x) * 4;
                const juce::uint8 value = light ? 210 : 70;
                pixels[pixelIndex + 0] = value;
                pixels[pixelIndex + 1] = light ? 170 : 90;
                pixels[pixelIndex + 2] = light ? 110 : 130;
                pixels[pixelIndex + 3] = 255;
            }
        }

        glGenTextures(1, &fallbackTextureId);
        glBindTexture(GL_TEXTURE_2D, fallbackTextureId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }

    void drawPlane()
    {
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.15f, -1.15f, 0.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.15f, -1.15f, 0.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.15f,  1.15f, 0.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.15f,  1.15f, 0.0f);
        glEnd();
    }

    void drawCube()
    {
        glBegin(GL_QUADS);

        glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f,  1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f,  1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f,  1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f,  1.0f);

        glTexCoord2f(0.0f, 0.0f); glVertex3f( 1.0f, -1.0f, -1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f(-1.0f, -1.0f, -1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f(-1.0f,  1.0f, -1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f( 1.0f,  1.0f, -1.0f);

        glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, -1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f(-1.0f, -1.0f,  1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f(-1.0f,  1.0f,  1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, -1.0f);

        glTexCoord2f(0.0f, 0.0f); glVertex3f( 1.0f, -1.0f,  1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f, -1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, -1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f( 1.0f,  1.0f,  1.0f);

        glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f,  1.0f,  1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f,  1.0f,  1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, -1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, -1.0f);

        glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, -1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f, -1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f, -1.0f,  1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f, -1.0f,  1.0f);

        glEnd();
    }

    void drawSphere()
    {
        constexpr int stacks = 28;
        constexpr int slices = 28;

        for (int stack = 0; stack < stacks; ++stack)
        {
            const float v0 = (float) stack / (float) stacks;
            const float v1 = (float) (stack + 1) / (float) stacks;
            const float phi0 = (v0 - 0.5f) * juce::MathConstants<float>::pi;
            const float phi1 = (v1 - 0.5f) * juce::MathConstants<float>::pi;

            glBegin(GL_QUAD_STRIP);
            for (int slice = 0; slice <= slices; ++slice)
            {
                const float u = (float) slice / (float) slices;
                const float theta = u * juce::MathConstants<float>::twoPi;

                const float x0 = std::cos(phi0) * std::cos(theta);
                const float y0 = std::sin(phi0);
                const float z0 = std::cos(phi0) * std::sin(theta);

                const float x1 = std::cos(phi1) * std::cos(theta);
                const float y1 = std::sin(phi1);
                const float z1 = std::cos(phi1) * std::sin(theta);

                glTexCoord2f(u, v0); glVertex3f(x0, y0, z0);
                glTexCoord2f(u, v1); glVertex3f(x1, y1, z1);
            }
            glEnd();
        }
    }

    void drawCylinder()
    {
        constexpr int slices = 40;
        constexpr float radius = 0.95f;
        constexpr float halfHeight = 1.15f;

        glBegin(GL_QUAD_STRIP);
        for (int i = 0; i <= slices; ++i)
        {
            const float u = (float) i / (float) slices;
            const float theta = u * juce::MathConstants<float>::twoPi;
            const float x = std::cos(theta) * radius;
            const float z = std::sin(theta) * radius;
            glTexCoord2f(u, 0.0f); glVertex3f(x, -halfHeight, z);
            glTexCoord2f(u, 1.0f); glVertex3f(x,  halfHeight, z);
        }
        glEnd();

        glBegin(GL_TRIANGLE_FAN);
        glTexCoord2f(0.5f, 0.5f); glVertex3f(0.0f, halfHeight, 0.0f);
        for (int i = 0; i <= slices; ++i)
        {
            const float u = (float) i / (float) slices;
            const float theta = u * juce::MathConstants<float>::twoPi;
            glTexCoord2f(0.5f + std::cos(theta) * 0.5f, 0.5f + std::sin(theta) * 0.5f);
            glVertex3f(std::cos(theta) * radius, halfHeight, std::sin(theta) * radius);
        }
        glEnd();

        glBegin(GL_TRIANGLE_FAN);
        glTexCoord2f(0.5f, 0.5f); glVertex3f(0.0f, -halfHeight, 0.0f);
        for (int i = slices; i >= 0; --i)
        {
            const float u = (float) i / (float) slices;
            const float theta = u * juce::MathConstants<float>::twoPi;
            glTexCoord2f(0.5f + std::cos(theta) * 0.5f, 0.5f + std::sin(theta) * 0.5f);
            glVertex3f(std::cos(theta) * radius, -halfHeight, std::sin(theta) * radius);
        }
        glEnd();
    }

    juce::OpenGLContext openGLContext;
    mutable juce::CriticalSection stateLock;
    Primitive primitive = Primitive::plane;
    juce::Image pendingImage;
    bool textureDirty = false;
    GLuint textureId = 0;
    GLuint fallbackTextureId = 0;
    float rotationDegrees = 0.0f;
};

MainComponent::PreviewWorkspacePanel::PreviewWorkspacePanel()
{
    titleLabel.setText("Preview Workspace", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(22.0f)).boldened());
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel);

    projectLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9fb1c8));
    addAndMakeVisible(projectLabel);

    configureModeButton(importTextureButton);
    importTextureButton.onClick = [this]
    {
        if (onImportTextureRequested)
            onImportTextureRequested();
    };
    addAndMakeVisible(importTextureButton);

    configureModeButton(removeTextureButton);
    removeTextureButton.onClick = [this]
    {
        if (! juce::isPositiveAndBelow(selectedTextureIndex, (int) workingTextures.size()))
            return;

        workingTextures.erase(workingTextures.begin() + selectedTextureIndex);
        if (workingTextures.empty())
            selectedTextureIndex = -1;
        else
            selectedTextureIndex = juce::jlimit(0, (int) workingTextures.size() - 1, selectedTextureIndex);

        previewDirty = true;
        refreshWorkingTextureControls();
    };
    addAndMakeVisible(removeTextureButton);

    configureModeButton(reloadPreviewButton);
    reloadPreviewButton.onClick = [this]
    {
        reloadPreview(true);
    };
    addAndMakeVisible(reloadPreviewButton);

    primitiveSelector.addItem("Plane", 1);
    primitiveSelector.addItem("Cube", 2);
    primitiveSelector.addItem("Sphere", 3);
    primitiveSelector.addItem("Cylinder", 4);
    primitiveSelector.setSelectedId(1, juce::dontSendNotification);
    primitiveSelector.onChange = [this]
    {
        setSelectedPrimitiveIndex(primitiveSelector.getSelectedItemIndex());
    };
    addAndMakeVisible(primitiveSelector);

    workingTextureSelector.onChange = [this]
    {
        selectedTextureIndex = workingTextureSelector.getSelectedItemIndex();
        refreshWorkingTextureControls();
    };
    addAndMakeVisible(workingTextureSelector);

    roleSelector.addItem(roleDisplayName(TextureRole::baseColor), 1);
    roleSelector.addItem(roleDisplayName(TextureRole::normal), 2);
    roleSelector.addItem(roleDisplayName(TextureRole::roughness), 3);
    roleSelector.addItem(roleDisplayName(TextureRole::metallic), 4);
    roleSelector.addItem(roleDisplayName(TextureRole::emissive), 5);
    roleSelector.addItem(roleDisplayName(TextureRole::mask), 6);
    roleSelector.addItem(roleDisplayName(TextureRole::auxiliary), 7);
    roleSelector.onChange = [this]
    {
        if (! juce::isPositiveAndBelow(selectedTextureIndex, (int) workingTextures.size()))
            return;

        workingTextures[(size_t) selectedTextureIndex].role = static_cast<TextureRole>(juce::jlimit(0, 6, roleSelector.getSelectedItemIndex()));
        previewDirty = true;
        refreshWorkingTextureControls();
    };
    addAndMakeVisible(roleSelector);

    auto setupAdjustment = [this](juce::Label& label,
                                  juce::Slider& slider,
                                  const juce::String& labelText,
                                  double min,
                                  double max,
                                  double value,
                                  auto&& onChange)
    {
        label.setText(labelText, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, juce::Colour(0xff9fb1c8));
        addAndMakeVisible(label);

        configureAdjustmentSlider(slider, min, max, value);
        slider.onValueChange = std::forward<decltype(onChange)>(onChange);
        addAndMakeVisible(slider);
    };

    setupAdjustment(brightnessLabel, brightnessSlider, "Brightness", -1.0, 1.0, 0.0, [this]
    {
        if (! juce::isPositiveAndBelow(selectedTextureIndex, (int) workingTextures.size()))
            return;
        workingTextures[(size_t) selectedTextureIndex].brightness = (float) brightnessSlider.getValue();
        previewDirty = true;
        refreshWorkingTextureControls();
    });

    setupAdjustment(contrastLabel, contrastSlider, "Contrast", 0.0, 2.0, 1.0, [this]
    {
        if (! juce::isPositiveAndBelow(selectedTextureIndex, (int) workingTextures.size()))
            return;
        workingTextures[(size_t) selectedTextureIndex].contrast = (float) contrastSlider.getValue();
        previewDirty = true;
        refreshWorkingTextureControls();
    });

    setupAdjustment(saturationLabel, saturationSlider, "Saturation", 0.0, 2.0, 1.0, [this]
    {
        if (! juce::isPositiveAndBelow(selectedTextureIndex, (int) workingTextures.size()))
            return;
        workingTextures[(size_t) selectedTextureIndex].saturation = (float) saturationSlider.getValue();
        previewDirty = true;
        refreshWorkingTextureControls();
    });

    setupAdjustment(gammaLabel, gammaSlider, "Gamma", 0.2, 3.0, 1.0, [this]
    {
        if (! juce::isPositiveAndBelow(selectedTextureIndex, (int) workingTextures.size()))
            return;
        workingTextures[(size_t) selectedTextureIndex].gamma = (float) gammaSlider.getValue();
        previewDirty = true;
        refreshWorkingTextureControls();
    });

    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd7e6f6));
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel);

    textureInfoLabel.setColour(juce::Label::textColourId, juce::Colour(0xff8da3c0));
    textureInfoLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(textureInfoLabel);

    viewport = std::make_unique<Viewport>();
    addAndMakeVisible(*viewport);

    setProjectName("No active texture project");
    setStatusText("Import textures into the working set, assign roles, then hit Reload Preview.");
    setTextureInfo("No working textures yet. Supported import path: PNG, JPG/JPEG, BMP, and TGA.");
    refreshWorkingTextureControls();
}

MainComponent::PreviewWorkspacePanel::~PreviewWorkspacePanel() = default;

void MainComponent::PreviewWorkspacePanel::setProjectName(const juce::String& projectName)
{
    projectLabel.setText("Project: " + projectName, juce::dontSendNotification);
}

void MainComponent::PreviewWorkspacePanel::setStatusText(const juce::String& text)
{
    statusLabel.setText(text, juce::dontSendNotification);
}

void MainComponent::PreviewWorkspacePanel::setTextureInfo(const juce::String& text)
{
    textureInfoLabel.setText(text, juce::dontSendNotification);
}

juce::String MainComponent::PreviewWorkspacePanel::roleDisplayName(TextureRole role)
{
    switch (role)
    {
        case TextureRole::baseColor: return "Base Color";
        case TextureRole::normal: return "Normal";
        case TextureRole::roughness: return "Roughness";
        case TextureRole::metallic: return "Metallic";
        case TextureRole::emissive: return "Emissive";
        case TextureRole::mask: return "Mask";
        case TextureRole::auxiliary: return "Auxiliary";
    }

    return "Texture";
}

juce::Image MainComponent::PreviewWorkspacePanel::applyAdjustments(const WorkingTextureItem& item)
{
    if (item.image.isNull())
        return {};

    juce::String error;
    const auto adjusted = texturePluginHost.applyAdjustments(item.image, item.brightness, item.contrast,
                                                              item.saturation, item.gamma, error);
    if (! adjusted.isNull())
        return adjusted;

    statusLabel.setText("FRust texture plugin: " + error, juce::dontSendNotification);
    return item.image;
}

void MainComponent::PreviewWorkspacePanel::refreshWorkingTextureControls()
{
    workingTextureSelector.clear(juce::dontSendNotification);
    for (int index = 0; index < (int) workingTextures.size(); ++index)
    {
        const auto& item = workingTextures[(size_t) index];
        workingTextureSelector.addItem(item.sourceLabel + " [" + roleDisplayName(item.role) + "]", index + 1);
    }

    const bool hasSelection = juce::isPositiveAndBelow(selectedTextureIndex, (int) workingTextures.size());
    if (hasSelection)
    {
        const auto& selectedItem = workingTextures[(size_t) selectedTextureIndex];
        workingTextureSelector.setSelectedItemIndex(selectedTextureIndex, juce::dontSendNotification);
        roleSelector.setSelectedItemIndex((int) selectedItem.role, juce::dontSendNotification);
        brightnessSlider.setValue(selectedItem.brightness, juce::dontSendNotification);
        contrastSlider.setValue(selectedItem.contrast, juce::dontSendNotification);
        saturationSlider.setValue(selectedItem.saturation, juce::dontSendNotification);
        gammaSlider.setValue(selectedItem.gamma, juce::dontSendNotification);
        removeTextureButton.setEnabled(true);
    }
    else
    {
        workingTextureSelector.setTextWhenNothingSelected("No working textures");
        roleSelector.setSelectedId(0, juce::dontSendNotification);
        brightnessSlider.setValue(0.0, juce::dontSendNotification);
        contrastSlider.setValue(1.0, juce::dontSendNotification);
        saturationSlider.setValue(1.0, juce::dontSendNotification);
        gammaSlider.setValue(1.0, juce::dontSendNotification);
        removeTextureButton.setEnabled(false);
    }

    roleSelector.setEnabled(hasSelection);
    brightnessSlider.setEnabled(hasSelection);
    contrastSlider.setEnabled(hasSelection);
    saturationSlider.setEnabled(hasSelection);
    gammaSlider.setEnabled(hasSelection);
    brightnessLabel.setEnabled(hasSelection);
    contrastLabel.setEnabled(hasSelection);
    saturationLabel.setEnabled(hasSelection);
    gammaLabel.setEnabled(hasSelection);
    textureInfoLabel.setText(getWorkingSetSummary(), juce::dontSendNotification);
}

void MainComponent::PreviewWorkspacePanel::addWorkingTexture(const juce::Image& image,
                                                             const juce::String& sourceLabel,
                                                             const juce::String& projectEntry)
{
    WorkingTextureItem item;
    item.image = image;
    item.sourceLabel = sourceLabel;
    item.projectEntry = projectEntry;
    item.role = workingTextures.empty() ? TextureRole::baseColor : TextureRole::auxiliary;

    workingTextures.push_back(std::move(item));
    selectedTextureIndex = (int) workingTextures.size() - 1;
    previewDirty = true;
    refreshWorkingTextureControls();
}

void MainComponent::PreviewWorkspacePanel::clearWorkingTextures()
{
    workingTextures.clear();
    selectedTextureIndex = -1;
    activePreviewSourceLabel.clear();
    viewport->setTextureImage({});
    viewport->refreshNow();
    previewDirty = false;
    refreshWorkingTextureControls();
}

bool MainComponent::PreviewWorkspacePanel::hasTexture() const noexcept
{
    return ! workingTextures.empty() || viewport->hasTexture();
}

juce::String MainComponent::PreviewWorkspacePanel::getTextureSourceLabel() const
{
    return activePreviewSourceLabel;
}

int MainComponent::PreviewWorkspacePanel::getWorkingTextureCount() const noexcept
{
    return (int) workingTextures.size();
}

juce::String MainComponent::PreviewWorkspacePanel::getWorkingSetSummary() const
{
    if (workingTextures.empty())
        return "No working textures yet. Supported import path: PNG, JPG/JPEG, BMP, and TGA.";

    juce::String text;
    text << "Working set: " << juce::String(workingTextures.size()) << " texture(s)";
    if (juce::isPositiveAndBelow(selectedTextureIndex, (int) workingTextures.size()))
    {
        const auto& item = workingTextures[(size_t) selectedTextureIndex];
        text << " | Selected: " << item.sourceLabel << " [" << roleDisplayName(item.role) << "]";
        text << " | B " << juce::String(item.brightness, 2)
             << " C " << juce::String(item.contrast, 2)
             << " S " << juce::String(item.saturation, 2)
             << " G " << juce::String(item.gamma, 2);
    }

    return text;
}

void MainComponent::PreviewWorkspacePanel::setSelectedPrimitiveIndex(int index)
{
    primitiveSelector.setSelectedItemIndex(index, juce::dontSendNotification);
    viewport->setPrimitive(static_cast<Viewport::Primitive>(juce::jlimit(0, 3, index)));
    previewDirty = true;
}

int MainComponent::PreviewWorkspacePanel::getSelectedPrimitiveIndex() const noexcept
{
    return primitiveSelector.getSelectedItemIndex();
}

void MainComponent::PreviewWorkspacePanel::reloadPreview(bool persistDerivedResult)
{
    juce::Image previewImage;
    juce::String processedSourceLabel;
    activePreviewSourceLabel.clear();

    for (const auto& item : workingTextures)
    {
        if (item.role == TextureRole::baseColor && ! item.image.isNull())
        {
            previewImage = applyAdjustments(item);
            activePreviewSourceLabel = item.sourceLabel;
            processedSourceLabel = item.sourceLabel;
            break;
        }
    }

    if (previewImage.isNull() && juce::isPositiveAndBelow(selectedTextureIndex, (int) workingTextures.size()))
    {
        const auto& selectedItem = workingTextures[(size_t) selectedTextureIndex];
        previewImage = applyAdjustments(selectedItem);
        activePreviewSourceLabel = selectedItem.sourceLabel;
        processedSourceLabel = selectedItem.sourceLabel;
    }

    viewport->setTextureImage(previewImage);
    viewport->refreshNow();
    previewDirty = false;
    refreshWorkingTextureControls();

    if (persistDerivedResult && ! previewImage.isNull() && texturePluginHost.isReady() && onProcessedTextureReady)
        onProcessedTextureReady(previewImage, processedSourceLabel);
}

bool MainComponent::PreviewWorkspacePanel::isPreviewDirty() const noexcept
{
    return previewDirty;
}

void MainComponent::PreviewWorkspacePanel::resized()
{
    auto area = getLocalBounds().reduced(18);
    auto topRow = area.removeFromTop(30);
    titleLabel.setBounds(topRow.removeFromLeft(220));
    projectLabel.setBounds(topRow);
    area.removeFromTop(8);

    auto controlsRow = area.removeFromTop(30);
    importTextureButton.setBounds(controlsRow.removeFromLeft(140));
    controlsRow.removeFromLeft(10);
    removeTextureButton.setBounds(controlsRow.removeFromLeft(140));
    controlsRow.removeFromLeft(10);
    reloadPreviewButton.setBounds(controlsRow.removeFromLeft(140));
    controlsRow.removeFromLeft(10);
    primitiveSelector.setBounds(controlsRow.removeFromLeft(180));
    controlsRow.removeFromLeft(12);
    workingTextureSelector.setBounds(controlsRow.removeFromLeft(260));
    controlsRow.removeFromLeft(10);
    roleSelector.setBounds(controlsRow.removeFromLeft(150));
    controlsRow.removeFromLeft(10);
    textureInfoLabel.setBounds(controlsRow);
    area.removeFromTop(10);

    statusLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);

    auto adjustmentRow1 = area.removeFromTop(24);
    brightnessLabel.setBounds(adjustmentRow1.removeFromLeft(84));
    brightnessSlider.setBounds(adjustmentRow1.removeFromLeft((adjustmentRow1.getWidth() / 2) - 42));
    adjustmentRow1.removeFromLeft(12);
    contrastLabel.setBounds(adjustmentRow1.removeFromLeft(72));
    contrastSlider.setBounds(adjustmentRow1);
    area.removeFromTop(6);

    auto adjustmentRow2 = area.removeFromTop(24);
    saturationLabel.setBounds(adjustmentRow2.removeFromLeft(84));
    saturationSlider.setBounds(adjustmentRow2.removeFromLeft((adjustmentRow2.getWidth() / 2) - 42));
    adjustmentRow2.removeFromLeft(12);
    gammaLabel.setBounds(adjustmentRow2.removeFromLeft(72));
    gammaSlider.setBounds(adjustmentRow2);
    area.removeFromTop(8);

    viewport->setBounds(area);
}

juce::ValueTree MainComponent::PreviewWorkspacePanel::createWorkingSetState() const
{
    juce::ValueTree workingSet("WorkingSet");
    workingSet.setProperty("selectedTextureIndex", selectedTextureIndex, nullptr);

    for (const auto& item : workingTextures)
    {
        juce::ValueTree child("TextureInput");
        child.setProperty("sourceLabel", item.sourceLabel, nullptr);
        child.setProperty("projectEntry", item.projectEntry, nullptr);
        child.setProperty("role", (int) item.role, nullptr);
        child.setProperty("brightness", item.brightness, nullptr);
        child.setProperty("contrast", item.contrast, nullptr);
        child.setProperty("saturation", item.saturation, nullptr);
        child.setProperty("gamma", item.gamma, nullptr);
        workingSet.addChild(child, -1, nullptr);
    }

    return workingSet;
}

void MainComponent::PreviewWorkspacePanel::restoreWorkingSetState(const juce::ValueTree& state,
                                                                  const std::function<juce::Image(const juce::String& projectEntry)>& imageLoader)
{
    clearWorkingTextures();
    if (! state.isValid())
        return;

    for (int index = 0; index < state.getNumChildren(); ++index)
    {
        auto child = state.getChild(index);
        if (! child.hasType("TextureInput"))
            continue;

        auto projectEntry = child.getProperty("projectEntry").toString();
        auto sourceLabel = child.getProperty("sourceLabel").toString();
        auto image = imageLoader(projectEntry);
        if (image.isNull())
            continue;

        addWorkingTexture(image, sourceLabel.isNotEmpty() ? sourceLabel : juce::File(projectEntry).getFileName(), projectEntry);
        auto& restored = workingTextures.back();
        restored.role = static_cast<TextureRole>(juce::jlimit(0, 6, (int) child.getProperty("role", 0)));
        restored.brightness = (float) child.getProperty("brightness", 0.0f);
        restored.contrast = (float) child.getProperty("contrast", 1.0f);
        restored.saturation = (float) child.getProperty("saturation", 1.0f);
        restored.gamma = (float) child.getProperty("gamma", 1.0f);
    }

    selectedTextureIndex = juce::jlimit(-1, (int) workingTextures.size() - 1, (int) state.getProperty("selectedTextureIndex", workingTextures.empty() ? -1 : 0));
    refreshWorkingTextureControls();
    reloadPreview();
}

MainComponent::MainComponent()
{
    configureHeader();
    configurePanels();
    loadSuiteState();
    setActiveMode(WorkspaceMode::preview);
    setSize(1380, 860);
}

MainComponent::~MainComponent() = default;

void MainComponent::configureHeader()
{
    headerBar.setAppTitle("Djehuti Texture");
    headerBar.setLogoImage(creation::ui::getSuiteLogoImage(creation::ui::SuiteLogoId::texture));
    headerBar.setProjectLabel("Project: No active texture project");
    headerBar.setTransportControlsVisible(false);
    headerBar.audioButton.setButtonText("Save");
    headerBar.tourButton.setButtonText("Refresh");
    headerBar.setStatusText("Loading shared suite state...");
    headerBar.onAudioRequested = [this]
    {
        saveProjectState(true);
    };
    headerBar.onTourRequested = [this]
    {
        loadSuiteState();
    };

    suiteShellController.attach(headerBar,
                                {
                                    "Djehuti Texture",
                                    creation::assets::SuiteAppDomain::texture,
                                    creation_texture::branding::backgroundColour()
                                },
                                [this](const juce::String& status)
                                {
                                    headerBar.setStatusText(status);
                                });
    suiteShellController.onProjectOpenRequested = [this](const juce::String& projectId)
    {
        openProject(projectId);
    };
    headerBar.onProjectMenuRequested = [this]
    {
        suiteShellController.showProjectBrowser();
    };

    addAndMakeVisible(headerBar);
}

void MainComponent::configurePanels()
{
    titleLabel.setText("Djehuti Texture", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(31.0f)).boldened());
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText("Suite VFS-backed texture workspace with dedicated creative modes.",
                          juce::dontSendNotification);
    subtitleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc9d3e3));
    addAndMakeVisible(subtitleLabel);

    runtimeLabel.setText(juce::String(creation_texture::language::getLanguageRuntimeSummary()), juce::dontSendNotification);
    runtimeLabel.setColour(juce::Label::textColourId, creation_texture::branding::accentColour());
    addAndMakeVisible(runtimeLabel);

    modeBar.onModeSelected = [this](WorkspaceMode mode) { setActiveMode(mode); };
    modeBar.onPopOutRequested = [this] { popOutActiveWorkspace(); };
    addAndMakeVisible(modeBar);

    workspaceGroup.setText("Texture Workspace");
    resourcesGroup.setText("Resources And Registry");
    aiGroup.setText("AI Agent Shell");
    configGroup.setText("Suite Configuration");

    addAndMakeVisible(workspaceGroup);
    addAndMakeVisible(resourcesGroup);
    addAndMakeVisible(aiGroup);
    addAndMakeVisible(configGroup);

    configureSummaryBox(resourcesSummary);
    configureSummaryBox(aiSummary);
    configureSummaryBox(configSummary);

    addAndMakeVisible(previewWorkspace);
    previewWorkspace.onImportTextureRequested = [this]
    {
        importPreviewTexture();
    };
    previewWorkspace.onProcessedTextureReady = [this](const juce::Image& image, const juce::String& sourceLabel)
    {
        persistProcessedTexture(image, sourceLabel);
    };
    addAndMakeVisible(proceduralWorkspace);
    addAndMakeVisible(mapsWorkspace);
    addAndMakeVisible(adjustmentsWorkspace);
    addAndMakeVisible(utilitiesWorkspace);
    poppedWorkspacePlaceholder.setJustificationType(juce::Justification::centred);
    poppedWorkspacePlaceholder.setColour(juce::Label::textColourId, juce::Colour(0xff9fb1c8));
    addAndMakeVisible(poppedWorkspacePlaceholder);
    addAndMakeVisible(resourcesSummary);
    addAndMakeVisible(aiSummary);
    addAndMakeVisible(configSummary);
}

void MainComponent::loadSuiteState()
{
    juce::String suiteError;
    suiteSettings = suiteSettingsStore.load(suiteError);

    juce::String aiError;
    suiteAiSettings = suiteAiSettingsStore.load(aiError);

    juce::String registryError;
    const auto allProjects = creation::interop::ProjectRegistry::discoverProjects(suiteSettings, registryError);
    totalProjectCount = allProjects.size();

    creation::interop::ProjectQuery domainQuery;
    domainQuery.appDomain = currentDomain();
    const auto domainProjects = creation::interop::ProjectRegistry::queryProjects(suiteSettings, domainQuery, registryError);
    domainProjectCount = domainProjects.size();
    lastRegistryError = registryError;

    juce::String projectError;
    ensureProjectSessionActive(projectError);

    if (suiteError.isNotEmpty())
        headerBar.setStatusText("Suite settings: " + suiteError);
    else if (aiError.isNotEmpty())
        headerBar.setStatusText("AI settings: " + aiError);
    else if (registryError.isNotEmpty())
        headerBar.setStatusText("Project registry: " + registryError);
    else if (projectError.isNotEmpty())
        headerBar.setStatusText(projectError);
    else
        headerBar.setStatusText("Djehuti Texture is ready in the current project context.");

    refreshShellSummary();
    loadLayoutState();
}

void MainComponent::refreshShellSummary()
{
    const auto projectName = projectSession.isValid() ? projectSession.getManifest().projectName : "No active texture project";
    previewWorkspace.setProjectName(projectName);
    previewWorkspace.setStatusText(previewWorkspace.isPreviewDirty()
                                       ? "Preview changes are staged. Hit Reload Preview to update the object."
                                       : (previewWorkspace.getWorkingTextureCount() > 0
                                              ? "Working-set textures are stored in the current project and reopen with this tool."
                                              : "Import textures and hit Reload Preview when you want to refresh the object."));
    previewWorkspace.setTextureInfo(previewWorkspace.hasTexture()
                                        ? previewWorkspace.getWorkingSetSummary()
                                        : "No working textures yet. Supported import path: PNG, JPG/JPEG, BMP, and TGA.");
    proceduralWorkspace.setSummaryText(workbenchSummaryText());
    mapsWorkspace.setSummaryText(workbenchSummaryText());
    adjustmentsWorkspace.setSummaryText(workbenchSummaryText());
    utilitiesWorkspace.setSummaryText(workbenchSummaryText());
    resourcesSummary.setText(registrySummaryText(), juce::dontSendNotification);
    aiSummary.setText(aiSummaryText(), juce::dontSendNotification);
    configSummary.setText(configSummaryText(), juce::dontSendNotification);
    refreshModeVisibility();
}

void MainComponent::setActiveMode(WorkspaceMode mode)
{
    activeMode = mode;
    updateModeButtons();
    refreshShellSummary();
    saveLayoutState();
}

void MainComponent::updateModeButtons()
{
    modeBar.setActiveMode(activeMode);
}

void MainComponent::refreshModeVisibility()
{
    previewWorkspace.setVisible(activeMode == WorkspaceMode::preview && ! isWorkspacePoppedOut(WorkspaceMode::preview));
    proceduralWorkspace.setVisible(activeMode == WorkspaceMode::procedural && ! isWorkspacePoppedOut(WorkspaceMode::procedural));
    mapsWorkspace.setVisible(activeMode == WorkspaceMode::maps && ! isWorkspacePoppedOut(WorkspaceMode::maps));
    adjustmentsWorkspace.setVisible(activeMode == WorkspaceMode::adjustments && ! isWorkspacePoppedOut(WorkspaceMode::adjustments));
    utilitiesWorkspace.setVisible(activeMode == WorkspaceMode::utilities && ! isWorkspacePoppedOut(WorkspaceMode::utilities));
    poppedWorkspacePlaceholder.setVisible(isWorkspacePoppedOut(activeMode));
}

juce::Component* MainComponent::getWorkspaceComponent(WorkspaceMode mode)
{
    switch (mode)
    {
        case WorkspaceMode::preview: return &previewWorkspace;
        case WorkspaceMode::procedural: return &proceduralWorkspace;
        case WorkspaceMode::maps: return &mapsWorkspace;
        case WorkspaceMode::adjustments: return &adjustmentsWorkspace;
        case WorkspaceMode::utilities: return &utilitiesWorkspace;
    }

    return nullptr;
}

bool MainComponent::isWorkspacePoppedOut(WorkspaceMode mode) const
{
    return workspacePopoutWindows[(size_t) workspaceModeIndex(mode)] != nullptr;
}

void MainComponent::popOutActiveWorkspace()
{
    popOutWorkspace(activeMode);
}

void MainComponent::popOutWorkspace(WorkspaceMode mode, const juce::Rectangle<int>* bounds)
{
    auto& windowSlot = workspacePopoutWindows[(size_t) workspaceModeIndex(mode)];

    if (windowSlot != nullptr)
    {
        if (bounds != nullptr && ! bounds->isEmpty())
            windowSlot->setBounds(*bounds);

        windowSlot->toFront(true);
        headerBar.setStatusText(workspaceModeName(mode) + " is already popped out.");
        return;
    }

    auto* component = getWorkspaceComponent(mode);
    if (component == nullptr)
        return;

    poppedWorkspacePlaceholder.setText(workspaceModeName(mode) + " is open in its own window.\nClose that window to dock it back here.",
                                       juce::dontSendNotification);

    auto window = std::make_unique<ManagedDocumentWindow>("Djehuti Texture - " + workspaceModeName(mode),
                                                          juce::Colour(0xff10141a),
                                                          juce::DocumentWindow::closeButton
                                                              | juce::DocumentWindow::minimiseButton
                                                              | juce::DocumentWindow::maximiseButton,
                                                          [this, mode]
                                                          {
                                                              dockWorkspace(mode);
                                                          });
    window->setUsingNativeTitleBar(true);
    window->setResizable(true, true);
    window->setContentNonOwned(component, false);

    if (bounds != nullptr && ! bounds->isEmpty())
        window->setBounds(*bounds);
    else
        window->centreWithSize(1180, 760);

    window->setVisible(true);
    window->toFront(true);
    windowSlot = std::move(window);

    refreshModeVisibility();
    resized();
    headerBar.setStatusText("Popped out " + workspaceModeName(mode) + ".");
    saveLayoutState();
}

void MainComponent::dockWorkspace(WorkspaceMode mode)
{
    auto& windowSlot = workspacePopoutWindows[(size_t) workspaceModeIndex(mode)];
    if (windowSlot == nullptr)
        return;

    auto* component = getWorkspaceComponent(mode);
    windowSlot->clearContentComponent();
    windowSlot.reset();

    if (component != nullptr)
        addAndMakeVisible(component);

    if (activeMode == mode)
        poppedWorkspacePlaceholder.setVisible(false);

    setActiveMode(mode);
    resized();
    headerBar.setStatusText("Docked " + workspaceModeName(mode) + ".");
    saveLayoutState();
}

juce::ValueTree MainComponent::createLayoutState() const
{
    juce::ValueTree layout("Layout");
    layout.setProperty("format", "creation-texture-layout", nullptr);
    layout.setProperty("formatVersion", 1, nullptr);
    layout.setProperty("activeMode", workspaceModeIndex(activeMode), nullptr);

    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
    {
        auto bounds = window->getBounds();
        layout.setProperty("mainWindowX", bounds.getX(), nullptr);
        layout.setProperty("mainWindowY", bounds.getY(), nullptr);
        layout.setProperty("mainWindowW", bounds.getWidth(), nullptr);
        layout.setProperty("mainWindowH", bounds.getHeight(), nullptr);
    }

    for (int index = 0; index < workspaceModeCount; ++index)
    {
        auto* window = workspacePopoutWindows[(size_t) index].get();
        if (window == nullptr)
            continue;

        auto bounds = window->getBounds();
        juce::ValueTree popped("PoppedWorkspace");
        popped.setProperty("mode", index, nullptr);
        popped.setProperty("x", bounds.getX(), nullptr);
        popped.setProperty("y", bounds.getY(), nullptr);
        popped.setProperty("w", bounds.getWidth(), nullptr);
        popped.setProperty("h", bounds.getHeight(), nullptr);
        layout.addChild(popped, -1, nullptr);
    }

    return layout;
}

void MainComponent::restoreLayoutState(const juce::ValueTree& state)
{
    if (! state.isValid())
        return;

    auto savedMode = static_cast<WorkspaceMode>(juce::jlimit(0,
                                                             workspaceModeCount - 1,
                                                             (int) state.getProperty("activeMode", workspaceModeIndex(WorkspaceMode::preview))));
    activeMode = savedMode;
    updateModeButtons();

    auto mainX = (int) state.getProperty("mainWindowX", -1);
    auto mainY = (int) state.getProperty("mainWindowY", -1);
    auto mainW = (int) state.getProperty("mainWindowW", -1);
    auto mainH = (int) state.getProperty("mainWindowH", -1);
    if (mainX >= 0 && mainY >= 0 && mainW > 0 && mainH > 0)
    {
        if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
            window->setBounds(mainX, mainY, mainW, mainH);
    }

    for (int index = 0; index < workspaceModeCount; ++index)
    {
        auto child = state.getChildWithProperty("mode", index);
        if (! child.isValid())
            continue;

        auto popX = (int) child.getProperty("x", -1);
        auto popY = (int) child.getProperty("y", -1);
        auto popW = (int) child.getProperty("w", -1);
        auto popH = (int) child.getProperty("h", -1);
        juce::Rectangle<int> bounds(popX, popY, popW, popH);
        auto mode = static_cast<WorkspaceMode>(index);
        popOutWorkspace(mode, bounds.isEmpty() ? nullptr : &bounds);
    }

    refreshModeVisibility();
}

void MainComponent::saveLayoutState()
{
    auto layoutState = createLayoutState();
    if (auto xml = layoutState.createXml())
    {
        auto* root = new juce::DynamicObject();
        root->setProperty("layoutXml", xml->toString());
        juce::String errorMessage;
        creation::services::SuiteVfsJsonStore::saveJson("creation-texture-layout.json", juce::var(root), errorMessage);
    }
}

void MainComponent::loadLayoutState()
{
    juce::String errorMessage;
    auto parsed = creation::services::SuiteVfsJsonStore::loadJson("creation-texture-layout.json", errorMessage);
    auto* state = parsed.getDynamicObject();
    if (state == nullptr)
        return;

    auto layoutXmlText = state->getProperty("layoutXml").toString();
    if (layoutXmlText.isEmpty())
        return;

    auto xml = juce::parseXML(layoutXmlText);
    if (xml == nullptr)
        return;

    restoreLayoutState(juce::ValueTree::fromXml(*xml));
}

void MainComponent::createNewProject()
{
    auto* prompt = new juce::AlertWindow("Create New Texture Project",
                                         "Enter a name for your new Creation Texture project:",
                                         juce::MessageBoxIconType::QuestionIcon);
    prompt->addTextEditor("projectName", "");
    prompt->addButton("Create Project", 1);
    prompt->addButton("Cancel", 0);

    auto options = juce::Component::SafePointer<MainComponent>(this);
    prompt->enterModalState(true, juce::ModalCallbackFunction::create([options, prompt](int result) mutable
    {
        std::unique_ptr<juce::AlertWindow> dialog(prompt);
        if (result != 1 || options == nullptr)
            return;

        auto name = dialog->getTextEditorContents("projectName").trim();
        if (name.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Project Error", "Project name cannot be empty.");
            return;
        }

        juce::String err;
        if (! creation::assets::ProjectWorkspaceService::createProject(options->suiteSettings,
                                                                       creation::assets::SuiteAppDomain::texture,
                                                                       name,
                                                                       "1.0.0",
                                                                       "1.0.0",
                                                                       options->projectSession,
                                                                       err))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Project Error", err);
            return;
        }

        options->previewWorkspace.clearWorkingTextures();
        options->previewWorkspace.setSelectedPrimitiveIndex(0);
        options->headerBar.setProjectLabel("Project: " + options->projectSession.getManifest().projectName);
        options->saveProjectState(true);
        options->saveAppSettings();
        options->refreshShellSummary();
        options->headerBar.setStatusText("Created project context: " + options->projectSession.getManifest().projectName);
    }), true);
}

void MainComponent::openProject(const juce::String& projectId)
{
    juce::String errorMessage;
    if (! creation::assets::ProjectWorkspaceService::openProject(suiteSettings, projectId, projectSession, errorMessage))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Project Error", errorMessage);
        return;
    }

    headerBar.setProjectLabel("Project: " + projectSession.getManifest().projectName);
    previewWorkspace.clearWorkingTextures();
    previewWorkspace.setSelectedPrimitiveIndex(0);
    loadProjectState();
    saveAppSettings();
    refreshShellSummary();
    headerBar.setStatusText("Opened project context: " + projectSession.getManifest().projectName);
}

void MainComponent::saveProjectState(bool userInitiated)
{
    if (! projectSession.isValid())
    {
        if (userInitiated)
            headerBar.setStatusText("No active project context. Open or create a project first.");
        return;
    }

    juce::ValueTree state("CreationTextureProjectState");
    state.setProperty("workspaceMode", static_cast<int>(activeMode), nullptr);
    state.setProperty("appDomain", juce::String(creation_texture::language::getAppDomainName()), nullptr);
    state.setProperty("previewPrimitiveIndex", previewWorkspace.getSelectedPrimitiveIndex(), nullptr);

    auto modeNotes = state.getOrCreateChildWithName("ModeNotes", nullptr);
    modeNotes.setProperty("preview", previewModeText(), nullptr);
    modeNotes.setProperty("procedural", proceduralModeText(), nullptr);
    modeNotes.setProperty("maps", mapsModeText(), nullptr);
    modeNotes.setProperty("adjustments", adjustmentsModeText(), nullptr);
    modeNotes.setProperty("utilities", utilitiesModeText(), nullptr);
    state.addChild(previewWorkspace.createWorkingSetState(), -1, nullptr);

    if (auto xml = state.createXml())
    {
        auto xmlString = xml->toString();
        juce::MemoryBlock xmlBlock(xmlString.toRawUTF8(), xmlString.getNumBytesAsUTF8());
        projectSession.writeEntry(textureSessionEntry, xmlBlock);
    }

    juce::String commitError;
    if (! projectSession.commit(commitError))
    {
        headerBar.setStatusText("Could not save Texture session into the project: " + commitError);
        return;
    }

    saveAppSettings();
    if (userInitiated)
        headerBar.setStatusText("Saved Texture session into project: " + projectSession.getManifest().projectName);
}

void MainComponent::loadProjectState()
{
    if (! projectSession.isValid())
        return;

    juce::MemoryBlock sessionData;
    if (! projectSession.readEntry(textureSessionEntry, sessionData))
    {
        if (! projectSession.readEntry(legacyTextureSessionEntry, sessionData))
            return;
    }

    auto xmlString = juce::String::createStringFromData(sessionData.getData(), static_cast<int>(sessionData.getSize()));
    auto xml = juce::XmlDocument::parse(xmlString);
    if (xml == nullptr)
        return;

    auto state = juce::ValueTree::fromXml(*xml);
    auto storedMode = state.getProperty("workspaceMode", static_cast<int>(WorkspaceMode::preview));
    auto modeValue = static_cast<WorkspaceMode>(juce::jlimit(0, 4, static_cast<int>(storedMode)));
    previewWorkspace.setSelectedPrimitiveIndex((int) state.getProperty("previewPrimitiveIndex", 0));
    auto workingSetState = state.getChildWithName("WorkingSet");
    if (workingSetState.isValid())
    {
        previewWorkspace.restoreWorkingSetState(workingSetState,
                                               [this](const juce::String& projectEntry)
                                               {
                                                   juce::MemoryBlock data;
                                                   juce::String readPath = projectEntry;
                                                   if (! projectSession.readEntry(readPath, data))
                                                   {
                                                       if (readPath.startsWith(texturePreviewImportRoot))
                                                           readPath = juce::String(legacyTexturePreviewImportRoot) + readPath.fromFirstOccurrenceOf(texturePreviewImportRoot, false, false);

                                                       if (! projectSession.readEntry(readPath, data))
                                                           return juce::Image {};
                                                   }

                                                   juce::String imageError;
                                                   return loadTextureImageFromMemory(data.getData(), data.getSize(), readPath, imageError);
                                               });
    }
    else
    {
        auto legacyPreviewTextureEntry = state.getProperty("previewTextureEntry").toString();
        previewWorkspace.clearWorkingTextures();
        if (legacyPreviewTextureEntry.startsWith(legacyTexturePreviewImportRoot))
            legacyPreviewTextureEntry = juce::String(texturePreviewImportRoot) + legacyPreviewTextureEntry.fromFirstOccurrenceOf(legacyTexturePreviewImportRoot, false, false);
        if (legacyPreviewTextureEntry.isNotEmpty())
            loadPreviewTextureFromProjectEntry(legacyPreviewTextureEntry);
    }
    setActiveMode(modeValue);
}

bool MainComponent::ensureProjectSessionActive(juce::String& errorMessage)
{
    if (projectSession.isValid())
        return true;

    auto settingsState = creation::services::SuiteVfsJsonStore::loadJson("creation-texture-settings.json", errorMessage);
    if (auto* settingsObject = settingsState.getDynamicObject())
    {
        auto lastProjectId = settingsObject->getProperty("lastOpenedProjectId").toString();
        if (lastProjectId.isNotEmpty())
        {
            if (creation::assets::ProjectWorkspaceService::openProject(suiteSettings, lastProjectId, projectSession, errorMessage))
            {
                headerBar.setProjectLabel("Project: " + projectSession.getManifest().projectName);
                loadProjectState();
                return true;
            }
        }
    }

    auto availableProjects = creation::assets::ProjectContainerService::listProjects(suiteSettings,
                                                                                     creation::assets::SuiteAppDomain::texture,
                                                                                     errorMessage);
    if (! availableProjects.isEmpty())
    {
        if (creation::assets::ProjectWorkspaceService::openProject(suiteSettings,
                                                                   availableProjects.getFirst().projectId,
                                                                   projectSession,
                                                                   errorMessage))
        {
            headerBar.setProjectLabel("Project: " + projectSession.getManifest().projectName);
            loadProjectState();
            return true;
        }
    }

    errorMessage = "No project context is open yet. Use the Project menu to open or create one.";
    return false;
}

void MainComponent::saveAppSettings()
{
    auto* object = new juce::DynamicObject();
    if (projectSession.isValid())
        object->setProperty("lastOpenedProjectId", projectSession.getProjectId());
    object->setProperty("activeMode", workspaceModeIndex(activeMode));

    juce::String errorMessage;
    creation::services::SuiteVfsJsonStore::saveJson("creation-texture-settings.json", juce::var(object), errorMessage);
}

void MainComponent::importPreviewTexture()
{
    previewTextureChooser = std::make_unique<juce::FileChooser>("Import Texture",
                                                                juce::File(),
                                                                "*.png;*.jpg;*.jpeg;*.bmp;*.tga");
    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    previewTextureChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        previewTextureChooser.reset();

        if (! file.existsAsFile())
            return;

        importPreviewTextureFromFile(file, true);
    });
}

bool MainComponent::importPreviewTextureFromFile(const juce::File& file, bool persistIntoProject)
{
    juce::String errorMessage;
    auto image = loadTextureImageFromFile(file, errorMessage);
    if (image.isNull())
    {
        headerBar.setStatusText(errorMessage.isNotEmpty() ? errorMessage : "Could not import that texture.");
        previewWorkspace.setStatusText("Texture import failed.");
        return false;
    }

    if (persistIntoProject)
    {
        if (! projectSession.isValid())
        {
            headerBar.setStatusText("Open or create a texture project before importing a project-backed preview texture.");
            return true;
        }

        juce::MemoryBlock fileBytes;
        if (! file.loadFileAsData(fileBytes))
        {
            headerBar.setStatusText("Could not read texture file from disk.");
            return false;
        }

        auto projectEntry = juce::String(texturePreviewImportRoot) + file.getFileName();
        projectSession.writeEntry(projectEntry, fileBytes);
        previewWorkspace.addWorkingTexture(image, file.getFileName(), projectEntry);
        saveProjectState(false);
    }
    else
    {
        previewWorkspace.addWorkingTexture(image, file.getFileName(), {});
    }

    refreshShellSummary();
    headerBar.setStatusText("Imported texture into working set: " + file.getFileName());
    return true;
}

void MainComponent::persistProcessedTexture(const juce::Image& image, const juce::String& sourceLabel)
{
    if (! projectSession.isValid() || image.isNull())
        return;

    juce::MemoryOutputStream encodedPng;
    juce::PNGImageFormat pngFormat;
    if (! pngFormat.writeImageToStream(image, encodedPng))
    {
        headerBar.setStatusText("Could not encode the FRust-processed texture as PNG.");
        return;
    }

    auto stem = juce::File::createLegalFileName(juce::File(sourceLabel).getFileNameWithoutExtension());
    if (stem.isEmpty())
        stem = "texture";

    const auto entry = juce::String(texturePreviewDerivedRoot) + stem + "-adjusted.png";
    projectSession.writeEntry(entry, encodedPng.getMemoryBlock());
    saveProjectState(false);
    headerBar.setStatusText("FRust texture plugin saved derived asset: " + entry);
}

bool MainComponent::loadPreviewTextureFromProjectEntry(const juce::String& entryPath)
{
    if (! projectSession.isValid() || entryPath.isEmpty())
        return false;

    juce::MemoryBlock data;
    if (! projectSession.readEntry(entryPath, data))
    {
        auto legacyPath = entryPath;
        if (legacyPath.startsWith(texturePreviewImportRoot))
            legacyPath = juce::String(legacyTexturePreviewImportRoot) + legacyPath.fromFirstOccurrenceOf(texturePreviewImportRoot, false, false);

        if (! projectSession.readEntry(legacyPath, data))
            return false;
    }

    juce::String errorMessage;
    auto image = loadTextureImageFromMemory(data.getData(), data.getSize(), entryPath, errorMessage);
    if (image.isNull())
    {
        headerBar.setStatusText(errorMessage.isNotEmpty() ? errorMessage : "Could not load saved preview texture.");
        return false;
    }

    previewWorkspace.addWorkingTexture(image, juce::File(entryPath).getFileName(), entryPath);
    return true;
}

juce::Image MainComponent::loadTextureImageFromFile(const juce::File& file, juce::String& errorMessage) const
{
    auto image = juce::ImageFileFormat::loadFrom(file);
    if (! image.isNull())
        return image;

    juce::MemoryBlock fileBytes;
    if (! file.loadFileAsData(fileBytes))
    {
        errorMessage = "Could not read texture file from disk.";
        return {};
    }

    return loadTextureImageFromMemory(fileBytes.getData(), fileBytes.getSize(), file.getFileName(), errorMessage);
}

juce::Image MainComponent::loadTextureImageFromMemory(const void* data, size_t size, const juce::String& filenameHint, juce::String& errorMessage) const
{
    juce::MemoryInputStream stream(data, size, false);
    auto image = juce::ImageFileFormat::loadFrom(stream);
    if (! image.isNull())
        return image;

    if (! filenameHint.endsWithIgnoreCase(".tga"))
    {
        errorMessage = "Unsupported texture format. Current preview import supports PNG, JPG/JPEG, BMP, and TGA.";
        return {};
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    auto* pixels = stbi_load_from_memory(static_cast<const stbi_uc*>(data),
                                         static_cast<int>(size),
                                         &width,
                                         &height,
                                         &channels,
                                         4);
    if (pixels == nullptr)
    {
        errorMessage = "Could not decode TGA texture.";
        return {};
    }

    juce::Image decoded(juce::Image::ARGB, width, height, true);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto pixelIndex = ((size_t) y * (size_t) width + (size_t) x) * 4;
            decoded.setPixelAt(x, y, juce::Colour(pixels[pixelIndex + 0],
                                                  pixels[pixelIndex + 1],
                                                  pixels[pixelIndex + 2],
                                                  pixels[pixelIndex + 3]));
        }
    }

    stbi_image_free(pixels);
    return decoded;
}

creation::assets::SuiteAppDomain MainComponent::currentDomain() const noexcept
{
    return creation::assets::SuiteAppDomain::texture;
}

juce::String MainComponent::domainDisplayName() const
{
    return creation::assets::toDisplayName(currentDomain());
}

juce::String MainComponent::registrySummaryText() const
{
    juce::String text;
    text << "App domain: " << domainDisplayName() << "\n";
    text << "Projects in this domain: " << domainProjectCount << "\n";
    text << "Projects across all known suite domains: " << totalProjectCount << "\n\n";
    text << "Djehuti Texture uses the shared suite project registry and VFS-backed project model.\n";
    text << "The project is the shared storage context; this tool saves its own session/assets into that project.\n";

    if (lastRegistryError.isNotEmpty())
        text << "\nRegistry message: " << lastRegistryError;

    return text;
}

juce::String MainComponent::aiSummaryText() const
{
    const auto runtime = creation::services::SuiteAiSettingsResolver::resolveRuntimeSettingsForApp(suiteAiSettings,
                                                                                                    currentDomain());

    juce::String text;
    text << "Shared AI account entries: " << suiteAiSettings.accounts.size() << "\n";
    text << "Selected app domain token: " << juce::String(creation_texture::language::getAppDomainName()) << "\n";
    text << "Resolved provider: " << runtime.providerDisplayName << "\n";
    text << "Resolved model: " << runtime.modelName << "\n";
    text << "Resolved base URL: " << runtime.baseUrl << "\n\n";
    text << "This shell is ready for Texture-specific helper workflows once the creative modes are implemented.";
    return text;
}

juce::String MainComponent::configSummaryText() const
{
    const auto configDirectory = suiteSettingsStore.getSuiteConfigDirectory().getFullPathName();
    const auto containersDirectory = creation::suite::getProjectContainerDirectory(suiteSettings).getFullPathName();
    const auto exportProjectName = projectSession.isValid() ? projectSession.getManifest().projectName : "Untitled Texture Project";
    const auto exportsDirectory = creation::suite::getExportDirectory(suiteSettings,
                                                                      currentDomain(),
                                                                      exportProjectName).getFullPathName();

    juce::String text;
    text << "Suite config directory: " << configDirectory << "\n";
    text << "Project container root: " << containersDirectory << "\n";
    text << "Suite VFS root: " << suiteSettings.suiteVfsRoot << "\n";
    text << "Project exports directory: " << exportsDirectory << "\n\n";
    text << "Canonical save/load for this tool session and its saved assets is project-backed through the suite VFS.\n";
    text << "External import/export files remain user-directed disk operations.";
    return text;
}

juce::String MainComponent::workbenchSummaryText() const
{
    juce::String text;
    if (projectSession.isValid())
        text << "Current project context: " << projectSession.getManifest().projectName << "\n";
    else
        text << "Current project context: none\n";

    text << "Current mode: ";
    switch (activeMode)
    {
        case WorkspaceMode::preview:
            text << "Preview\n\n" << previewModeText();
            break;
        case WorkspaceMode::procedural:
            text << "Procedural\n\n" << proceduralModeText();
            break;
        case WorkspaceMode::maps:
            text << "Maps\n\n" << mapsModeText();
            break;
        case WorkspaceMode::adjustments:
            text << "Adjustments\n\n" << adjustmentsModeText();
            break;
        case WorkspaceMode::utilities:
            text << "Utilities\n\n" << utilitiesModeText();
            break;
    }

    return text;
}

juce::String MainComponent::previewModeText() const
{
    return "OpenGL preview workspace.\n"
           "- preview imported textures on primitive shapes\n"
           "- preview a single loaded model with applied textures\n"
           "- foundation for the future material/texture look-dev viewport";
}

juce::String MainComponent::proceduralModeText() const
{
    return "Node-based procedural texture authoring.\n"
           "- graph-driven texture generation workflow\n"
           "- suite shell placeholder for a Blender-style procedural surface\n"
           "- intended home for reusable procedural texture graphs";
}

juce::String MainComponent::mapsModeText() const
{
    return "Map generation workspace.\n"
           "- UV-related and other derived map workflows\n"
           "- dedicated mode rather than burying map generation inside another tool\n"
           "- intended to save generated assets into the active suite project";
}

juce::String MainComponent::adjustmentsModeText() const
{
    return "Algorithmic 2D texture adjustments.\n"
           "- not a paint workflow\n"
           "- manipulation and cleanup operations such as tileability preparation\n"
           "- intended for non-destructive, repeatable texture transforms";
}

juce::String MainComponent::utilitiesModeText() const
{
    return "Derived texture utility workspace.\n"
           "- normal map preparation\n"
           "- ambient occlusion map preparation\n"
           "- light map and similar derived-output generation";
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(creation_texture::branding::backgroundColour());

    auto bounds = getLocalBounds().toFloat().reduced(18.0f);
    g.setColour(creation_texture::branding::panelColour());
    g.fillRoundedRectangle(bounds, 24.0f);

    g.setColour(creation_texture::branding::accentColour().withAlpha(0.8f));
    g.drawRoundedRectangle(bounds, 24.0f, 1.4f);
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();
    headerBar.setBounds(bounds.removeFromTop(96));

    auto area = bounds.reduced(34, 28);

    titleLabel.setBounds(area.removeFromTop(38));
    subtitleLabel.setBounds(area.removeFromTop(26));
    runtimeLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(12);

    modeBar.setBounds(area.removeFromTop(56));
    area.removeFromTop(16);

    auto topRow = area.removeFromTop(area.getHeight() / 2);
    auto leftTop = topRow.removeFromLeft(topRow.getWidth() / 2);
    leftTop.removeFromRight(8);
    topRow.removeFromLeft(8);

    workspaceGroup.setBounds(leftTop);
    resourcesGroup.setBounds(topRow);

    auto bottomRow = area;
    auto leftBottom = bottomRow.removeFromLeft(bottomRow.getWidth() / 2);
    leftBottom.removeFromRight(8);
    bottomRow.removeFromLeft(8);

    aiGroup.setBounds(leftBottom);
    configGroup.setBounds(bottomRow);

    auto workspaceBounds = workspaceGroup.getBounds().reduced(14, 26);
    previewWorkspace.setBounds(workspaceBounds);
    proceduralWorkspace.setBounds(workspaceBounds);
    mapsWorkspace.setBounds(workspaceBounds);
    adjustmentsWorkspace.setBounds(workspaceBounds);
    utilitiesWorkspace.setBounds(workspaceBounds);
    poppedWorkspacePlaceholder.setBounds(workspaceBounds);
    resourcesSummary.setBounds(resourcesGroup.getBounds().reduced(14, 26));
    aiSummary.setBounds(aiGroup.getBounds().reduced(14, 26));
    configSummary.setBounds(configGroup.getBounds().reduced(14, 26));
}
