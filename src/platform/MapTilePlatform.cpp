// Windows adapter for the shared map tile service (REQ-363).

#include "MapTileService.hpp"
#include "AppPaths.hpp"
#include "HttpFetch.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

std::filesystem::path MapTileCacheDirectory() {
#ifdef _WIN32
  wchar_t local[MAX_PATH]{};
  const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
  if (length > 0 && length < MAX_PATH)
    return std::filesystem::path(local) / "GoSurvey" / "MapTiles";
#endif
  const std::filesystem::path dir = UserDataDirectory();
  return dir.empty() ? dir : dir / "MapTiles";
}

MapTileStatus FetchMapTileOverWinHttp(const std::string& url, std::string& body, std::string& error) {
  int status = 0;
  if (HttpGetString(url, 10000, body, error, std::string(), &status))
    return MapTileStatus::Ok;
  return status == 404 ? MapTileStatus::NotFound : MapTileStatus::Failed;
}
