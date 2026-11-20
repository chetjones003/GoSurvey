#include "TraverseActions.hpp"
#include <GoSurvey/IO/FbkImportPublic.hpp>
#include <GoSurvey/IO/JobXmlImportPublic.hpp>
#include <GoSurvey/IO/Rw5ImportPublic.hpp>
#include <GoSurvey/IO/StarNetImportPublic.hpp>
#include <GoSurvey/IO/TraverseIoPublic.hpp>
#include <GoSurvey/IO/TraverseCommandLangPublic.hpp>

std::string SerializeTraverseCommandText(const TraverseData& data, const LsaWeights& weights) {
  return travcmd::Serialize(data, weights);
}

bool ParseTraverseCommandText(const std::string& text, TraverseData& data,
                             LsaWeights& weights, std::string& error) {
  return travcmd::Parse(text, data, weights, error);
}

bool SaveTraverseDataFile(const std::string& path, const TraverseData& data,
                          const LsaWeights& weights, std::string& error) {
  return traverseio::Save(path, data, weights, error);
}

bool LoadTraverseDataFile(const std::string& path, TraverseData& data,
                          LsaWeights& weights, std::string& error) {
  return traverseio::Load(path, data, weights, error);
}

bool ImportTraverseMeasurements(const std::string& format, const char* path,
                                TraverseData& data, std::string& error) {
  if (format == "fbk") return FbkImport(path, data, error);
  if (format == "dat") return StarNetImport(path, data, error);
  if (format == "rw5") return Rw5Import(path, data, error);
  if (format == "jxl" || format == "xml") return JobXmlImport(path, data, error);
  error = "Unsupported raw traverse file type.";
  return false;
}
