#pragma once
#include <GoSurvey/Survey/TraverseCalcPublic.hpp>
#include <GoSurvey/Survey/TraverseLeastSquaresPublic.hpp>
#include <string>
#include <vector>

struct AppCommandState;
bool TraverseAdjustmentHasElevations(const LsaResult& result);

// Shared by native frontends. Never accepts a failed, incomplete or nonfinite result.
bool AcceptTraverseAdjustment(TraverseData& data, const LsaResult& result, std::string& error);

// One undo step, one point per station ID, WORLD -> drawing-local before placement.
// The optional accepted result also supplies GPS-only/network station coordinates.
int CommitTraverseToDrawing(AppCommandState& state, const TraverseData& data,
                            const LsaResult* accepted, std::vector<std::string>& log);

// Exports an already computed report through the format provider; unsupported formats return false.
bool ExportTraverseReport(const std::string& format, const std::string& pathUtf8,
                          const std::string& text, std::string& error);

// Traverse editor operations use domain values; the Commands layer selects the IO provider.
std::string SerializeTraverseCommandText(const TraverseData& data, const LsaWeights& weights);
bool ParseTraverseCommandText(const std::string& text, TraverseData& data,
                             LsaWeights& weights, std::string& error);
bool SaveTraverseDataFile(const std::string& path, const TraverseData& data,
                          const LsaWeights& weights, std::string& error);
bool LoadTraverseDataFile(const std::string& path, TraverseData& data,
                          LsaWeights& weights, std::string& error);
bool ImportTraverseMeasurements(const std::string& format, const char* path,
                                TraverseData& data, std::string& error);
