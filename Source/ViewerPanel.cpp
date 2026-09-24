#include "ViewerPanel.h"
#include <gl/GL.h>
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

using namespace juce::gl;
class ViewerPanel::Viewport final : public juce::Component,
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
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
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
        glTranslatef(0.0f, 0.0f, -zoomDistance);
        glRotatef(elevationDegrees, 1.0f, 0.0f, 0.0f);
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
                const float theta = -u * juce::MathConstants<float>::twoPi;

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
            const float theta = -u * juce::MathConstants<float>::twoPi;
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
            const float theta = -u * juce::MathConstants<float>::twoPi;
            glTexCoord2f(0.5f + std::cos(theta) * 0.5f, 0.5f + std::sin(theta) * 0.5f);
            glVertex3f(std::cos(theta) * radius, halfHeight, std::sin(theta) * radius);
        }
        glEnd();

        glBegin(GL_TRIANGLE_FAN);
        glTexCoord2f(0.5f, 0.5f); glVertex3f(0.0f, -halfHeight, 0.0f);
        for (int i = slices; i >= 0; --i)
        {
            const float u = (float) i / (float) slices;
            const float theta = -u * juce::MathConstants<float>::twoPi;
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
    float elevationDegrees = 18.0f;
    juce::Point<int> lastMousePos;

    void mouseDown(const juce::MouseEvent& e) override
    {
        lastMousePos = e.getPosition();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        auto delta = e.getPosition() - lastMousePos;
        lastMousePos = e.getPosition();
        
        rotationDegrees += delta.x * 0.5f;
        elevationDegrees = juce::jlimit(-90.0f, 90.0f, elevationDegrees + delta.y * 0.5f);
        
        openGLContext.triggerRepaint();
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        const juce::ScopedLock lock(stateLock);
        zoomDistance -= wheel.deltaY * 5.0f;
        zoomDistance = juce::jlimit(1.0f, 20.0f, zoomDistance);
        openGLContext.triggerRepaint();
    }
};

ViewerPanel::ViewerPanel()
{
    titleLabel.setText("Preview Workspace", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(22.0f)).boldened());
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel);

    projectLabel.setColour(juce::Label::textColourId, juce::Colour(0xff9fb1c8));
    addAndMakeVisible(projectLabel);

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

ViewerPanel::~ViewerPanel() = default;

void ViewerPanel::setProjectName(const juce::String& projectName)
{
    projectLabel.setText("Project: " + projectName, juce::dontSendNotification);
}

void ViewerPanel::setStatusText(const juce::String& text)
{
    statusLabel.setText(text, juce::dontSendNotification);
}

void ViewerPanel::setTextureInfo(const juce::String& text)
{
    textureInfoLabel.setText(text, juce::dontSendNotification);
}

juce::String ViewerPanel::roleDisplayName(TextureRole role)
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

juce::Image ViewerPanel::applyAdjustments(const WorkingTextureItem& item)
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

void ViewerPanel::refreshWorkingTextureControls()
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

void ViewerPanel::addWorkingTexture(const juce::Image& image,
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

void ViewerPanel::clearWorkingTextures()
{
    workingTextures.clear();
    selectedTextureIndex = -1;
    activePreviewSourceLabel.clear();
    viewport->setTextureImage({});
    viewport->refreshNow();
    previewDirty = false;
    refreshWorkingTextureControls();
}

bool ViewerPanel::hasTexture() const noexcept
{
    return ! workingTextures.empty() || viewport->hasTexture();
}

juce::String ViewerPanel::getTextureSourceLabel() const
{
    return activePreviewSourceLabel;
}

int ViewerPanel::getWorkingTextureCount() const noexcept
{
    return (int) workingTextures.size();
}

juce::String ViewerPanel::getWorkingSetSummary() const
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

void ViewerPanel::setSelectedPrimitiveIndex(int index)
{
    primitiveSelector.setSelectedItemIndex(index, juce::dontSendNotification);
    viewport->setPrimitive(static_cast<Viewport::Primitive>(juce::jlimit(0, 3, index)));
    previewDirty = true;
}

int ViewerPanel::getSelectedPrimitiveIndex() const noexcept
{
    return primitiveSelector.getSelectedItemIndex();
}

void ViewerPanel::reloadPreview(bool persistDerivedResult)
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

bool ViewerPanel::isPreviewDirty() const noexcept
{
    return previewDirty;
}

void ViewerPanel::resized()
{
    auto area = getLocalBounds().reduced(18);
    auto topRow = area.removeFromTop(30);
    titleLabel.setBounds(topRow.removeFromLeft(220));
    projectLabel.setBounds(topRow);
    area.removeFromTop(8);

    auto controlsRow = area.removeFromTop(30);
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

juce::ValueTree ViewerPanel::createWorkingSetState() const
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

void ViewerPanel::restoreWorkingSetState(const juce::ValueTree& state,
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




