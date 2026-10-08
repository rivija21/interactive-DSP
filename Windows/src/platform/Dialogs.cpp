#include "Dialogs.h"
#include "../util/Text.h"
#include <windows.h>
#include <shobjidl.h>

namespace {

const wchar_t* kAudioPatterns = L"*.wav;*.wave;*.mp3;*.m4a;*.aac;*.mp4;*.flac;*.wma;*.aif;*.aiff;*.aifc;*.caf;*.ogg;*.opus;*.3gp;*.adts;*.ac3";

template <class T>
void release(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

void addMessage(IFileDialog* dialog, const std::string& message) {
    if (message.empty()) return;
    IFileDialogCustomize* custom = nullptr;
    if (SUCCEEDED(dialog->QueryInterface(IID_IFileDialogCustomize, reinterpret_cast<void**>(&custom)))) {
        custom->AddText(1, widen(message).c_str());
        custom->Release();
    }
}

std::optional<std::wstring> resultPath(IFileDialog* dialog) {
    IShellItem* item = nullptr;
    std::optional<std::wstring> path;
    if (SUCCEEDED(dialog->GetResult(&item)) && item) {
        PWSTR p = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
            path = std::wstring(p);
            CoTaskMemFree(p);
        }
        item->Release();
    }
    return path;
}

} // namespace

std::optional<std::wstring> chooseAudioFile(void* owner, const std::string& message) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog,
                                reinterpret_cast<void**>(&dialog))))
        return std::nullopt;
    COMDLG_FILTERSPEC types[] = {{L"Audio", kAudioPatterns}, {L"All files", L"*.*"}};
    dialog->SetFileTypes(2, types);
    dialog->SetTitle(L"Open Audio File");
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
    addMessage(dialog, message);
    std::optional<std::wstring> path;
    if (SUCCEEDED(dialog->Show(HWND(owner)))) path = resultPath(dialog);
    release(dialog);
    return path;
}

std::optional<std::wstring> chooseSaveLocation(void* owner, const std::string& suggestedName, const std::string& typeName,
                                               const std::string& extension, const std::string& message) {
#ifdef FILTERLAB_TEST_HOOKS
    // Test builds only: save straight into a folder so exports can be checked automatically.
    if (const wchar_t* dir = _wgetenv(L"FILTERLAB_SAVE_DIR")) {
        std::wstring name = widen(suggestedName);
        for (auto& c : name)
            if (wcschr(L"\\/:*?\"<>|", c)) c = L'-';
        return std::wstring(dir) + L"\\" + name;
    }
#endif
    IFileSaveDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileSaveDialog,
                                reinterpret_cast<void**>(&dialog))))
        return std::nullopt;
    std::wstring name = widen(typeName), pattern = L"*." + widen(extension), ext = widen(extension);
    COMDLG_FILTERSPEC types[] = {{name.c_str(), pattern.c_str()}};
    dialog->SetFileTypes(1, types);
    dialog->SetDefaultExtension(ext.c_str());
    // Windows file names can't contain some characters that macOS allows.
    std::wstring suggested = widen(suggestedName);
    for (auto& c : suggested)
        if (wcschr(L"\\/:*?\"<>|", c)) c = L'-';
    dialog->SetFileName(suggested.c_str());
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT);
    addMessage(dialog, message);
    std::optional<std::wstring> path;
    if (SUCCEEDED(dialog->Show(HWND(owner)))) path = resultPath(dialog);
    release(dialog);
    return path;
}

void showAlert(void* owner, const std::string& title, const std::string& message) {
    MessageBoxW(HWND(owner), widen(message).c_str(), widen(title).c_str(), MB_OK | MB_ICONWARNING);
}

bool isAudioFileName(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = path.substr(dot);
    for (auto& c : ext) c = wchar_t(towlower(c));
    std::wstring patterns = kAudioPatterns;
    size_t start = 0;
    while (start < patterns.size()) {
        size_t end = patterns.find(L';', start);
        if (end == std::wstring::npos) end = patterns.size();
        if (patterns.substr(start + 1, end - start - 1) == ext) return true;
        start = end + 1;
    }
    return false;
}
