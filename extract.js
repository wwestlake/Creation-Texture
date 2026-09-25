const fs = require('fs');

let mainC = fs.readFileSync('apps/CreationTexture/Source/MainComponent.cpp', 'utf8');
let mainH = fs.readFileSync('apps/CreationTexture/Source/MainComponent.h', 'utf8');

// Extract ViewerPanel implementation from cpp
const startStr = 'class MainComponent::PreviewWorkspacePanel::Viewport';
const endStr = 'void MainComponent::configureHeader()'; // changed to configureHeader since it comes before configurePanels

let startIndex = mainC.indexOf(startStr);
let endIndex = mainC.indexOf(endStr);

if (startIndex !== -1 && endIndex !== -1) {
    let viewerImpl = mainC.substring(startIndex, endIndex);
    viewerImpl = viewerImpl.replace(/MainComponent::PreviewWorkspacePanel/g, 'ViewerPanel');
    let viewerC = '#include "ViewerPanel.h"\n#include <gl/GL.h>\n\nusing namespace juce::gl;\n' + viewerImpl;
    fs.writeFileSync('apps/CreationTexture/Source/ViewerPanel.cpp', viewerC);
    
    mainC = mainC.substring(0, startIndex) + mainC.substring(endIndex);
    fs.writeFileSync('apps/CreationTexture/Source/MainComponent.cpp', mainC);
    console.log('Successfully extracted ViewerPanel.cpp');
} else {
    console.log('Failed to find bounds cpp: ' + startIndex + ' ' + endIndex);
}

// Extract ViewerPanel header from h
const hStartStr = 'class PreviewWorkspacePanel final : public juce::Component';
const hEndStr = '    void configureHeader();';

let hStartIndex = mainH.indexOf(hStartStr);
let hEndIndex = mainH.indexOf(hEndStr);

if (hStartIndex !== -1 && hEndIndex !== -1) {
    mainH = mainH.substring(0, hStartIndex) + mainH.substring(hEndIndex);
    fs.writeFileSync('apps/CreationTexture/Source/MainComponent.h', mainH);
    console.log('Successfully extracted ViewerPanel header');
} else {
    console.log('Failed to find bounds h: ' + hStartIndex + ' ' + hEndIndex);
}

// Now replace usages
mainH = mainH.replace(/ProceduralWorkspacePanel/g, 'NodeGraphPanel');
mainH = mainH.replace(/PreviewWorkspacePanel/g, 'ViewerPanel');
mainH = mainH.replace(/NodeGraphPanel proceduralWorkspace;/g, 'NodeGraphPanel nodeGraphPanel;\n    ViewerPanel viewerPanel;');
mainH = mainH.replace(/ViewerPanel previewWorkspace;/g, '');

mainC = mainC.replace(/proceduralWorkspace/g, 'nodeGraphPanel');
mainC = mainC.replace(/previewWorkspace/g, 'viewerPanel');
mainC = mainC.replace(/ProceduralWorkspacePanel/g, 'NodeGraphPanel');
mainC = mainC.replace(/PreviewWorkspacePanel/g, 'ViewerPanel');
mainC = mainC.replace(/#include "NodeGraphPanel\.h"/g, '#include "NodeGraphPanel.h"\n#include "ViewerPanel.h"');

fs.writeFileSync('apps/CreationTexture/Source/MainComponent.h', mainH);
fs.writeFileSync('apps/CreationTexture/Source/MainComponent.cpp', mainC);
