
#include "ViewerPanel.h"
#include "Branding.h"

ViewerPanel::ViewerPanel()
{
    addAndMakeVisible(viewer);
}

ViewerPanel::~ViewerPanel()
{
}

void ViewerPanel::resized()
{
    viewer.setBounds(getLocalBounds());
}
