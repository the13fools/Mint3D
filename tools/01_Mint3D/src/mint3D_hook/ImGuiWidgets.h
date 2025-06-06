#ifndef IMGUIWIDGETS_H
#define IMGUIWIDGETS_H

#include "AppState.h"
// #include "FieldView.h" // Include the FieldView enum
#include <vector>

namespace ImGuiWidgets {

void ShowOptWeights(AppState& appState);

// Custom ImGui widget functions
// void ShowFileScrubber(int& fileIndex, int minIndex, int maxIndex);
void ShowFileScrubber(AppState& appState);
void ShowFieldViewCheckboxes(AppState& appState);
void ShowRunInfo(AppState& appState);
void ShowPlots(AppState& appState);
// void ShowPlot(const std::vector<double>& data, const char* label, float minY, float maxY);
void DrawFileLoader(AppState& appState);
void AddFieldViewScalarsToPolyscope(AppState& appState);
void ShowFieldViewCheckboxesWithSliders(AppState& appState);
void ShowFieldViewScrubber(AppState& appState, Field_View& currentField);
void ShowSolverParams(AppState& appState);
void ShowCombedFramesPerFacet(AppState& appState);
void SetSeabornStyle();
void ShowMainGUI(AppState& appState);   // Added this function

// Other ImGui widget functions as needed
};   // namespace ImGuiWidgets

#endif   // IMGUIWIDGETS_H
