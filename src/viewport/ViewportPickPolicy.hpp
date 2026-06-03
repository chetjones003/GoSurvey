#pragma once

#include "CadCommands.hpp"

/// When building a selection window (fence), picks should use **unsnapped** device-mapped world coordinates so the
/// rectangle matches the drag. Drawing commands use snapped \p outCursor world coordinates instead.
inline bool ViewportUseRawWorldForSelectionRectPick(const AppCommandState& cmd) {
  using K = AppCommandState::Kind;
  using MP = AppCommandState::ModifyPhase;
  using RP = AppCommandState::RotatePhase;
  return cmd.active == K::None || cmd.active == K::Delete || cmd.active == K::Join || cmd.active == K::Trim ||
         cmd.active == K::Zoom || (cmd.active == K::Move && cmd.modifyPhase == MP::PickSelection) ||
         (cmd.active == K::Copy && cmd.modifyPhase == MP::PickSelection) ||
         (cmd.active == K::Scale && cmd.modifyPhase == MP::PickSelection) ||
         (cmd.active == K::Rotate && cmd.rotatePhase == RP::PickSelection);
}
