#pragma once
#include <optional>
#include <string>

// Common file dialogs and alerts (the stand-ins for NSOpenPanel, NSSavePanel and NSAlert).

/// Asks for an audio file to open. `message` is shown inside the dialog.
std::optional<std::wstring> chooseAudioFile(void* owner, const std::string& message);

/// Asks where to save. `extension` without the dot ("png", "wav").
std::optional<std::wstring> chooseSaveLocation(void* owner, const std::string& suggestedName, const std::string& typeName,
                                               const std::string& extension, const std::string& message);

void showAlert(void* owner, const std::string& title, const std::string& message);

/// File extensions treated as audio for drag and drop and the open dialog.
bool isAudioFileName(const std::wstring& path);
