#define UNICODE
#define _UNICODE
#include "core.h"
#include <commdlg.h>
#include <process.h>
#include <memory>
#include <stdexcept>

namespace {
enum { ID_LIST=100, ID_ADD, ID_REMOVE, ID_GENERATE, ID_SAVE, ID_CONSOLE, ID_STATUS, ID_LABEL };
const UINT WM_PROGRESS = WM_APP+1, WM_FINISHED = WM_APP+2;
HINSTANCE instance;
HWND listBox, statusLabel, mainWindow;
HFONT font;
bool busy = false;
std::wstring configPath;
std::vector<std::wstring> files;
HANDLE consoleOutput = INVALID_HANDLE_VALUE;

void error(HWND window, const std::wstring& message) {
    MessageBoxW(window, message.c_str(), L"MD5Gen — ошибка", MB_OK | MB_ICONERROR);
}
void consoleWrite(const std::wstring& value) {
    DWORD mode = 0, written = 0;
    if (GetConsoleMode(consoleOutput, &mode)) {
        size_t offset = 0;
        while (offset < value.size()) {
            DWORD count = static_cast<DWORD>((value.size()-offset > 16000) ? 16000 : value.size()-offset);
            if (!WriteConsoleW(consoleOutput, value.data()+offset, count, &written, NULL) || !written) break;
            offset += written;
        }
    } else {
        std::string bytes = md5gen::utf8(value);
        WriteFile(consoleOutput, bytes.data(), static_cast<DWORD>(bytes.size()), &written, NULL);
    }
}
void showConsole() {
    if (!GetConsoleWindow()) {
        if (!AllocConsole()) throw std::runtime_error("Cannot open console: " + md5gen::utf8(md5gen::systemError(GetLastError())));
        SetConsoleTitleW(L"MD5Gen — результаты");
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCtrlHandler(NULL, TRUE);
        HWND console = GetConsoleWindow();
        HMENU menu = GetSystemMenu(console, FALSE);
        if (menu) DeleteMenu(menu, SC_CLOSE, MF_BYCOMMAND);
    }
    consoleOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    ShowWindow(GetConsoleWindow(), SW_SHOW);
}
void refreshList() {
    SendMessageW(listBox, LB_RESETCONTENT, 0, 0);
    int maxWidth = 0;
    HDC dc = GetDC(listBox);
    HGDIOBJ old = SelectObject(dc, font);
    for (size_t i=0; i<files.size(); ++i) {
        SendMessageW(listBox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(files[i].c_str()));
        SIZE size = {};
        GetTextExtentPoint32W(dc, files[i].c_str(), static_cast<int>(files[i].size()), &size);
        if (size.cx > maxWidth) maxWidth = size.cx;
    }
    SelectObject(dc, old); ReleaseDC(listBox, dc);
    SendMessageW(listBox, LB_SETHORIZONTALEXTENT, maxWidth+20, 0);
    std::wstring text = L"Файлов в списке: " + std::to_wstring(files.size()) + L". Настройки сохранены в config.json.";
    SetWindowTextW(statusLabel, text.c_str());
}
void commitFiles(const std::vector<std::wstring>& next) {
    md5gen::writeAtomic(configPath, md5gen::encodeConfig(next));
    files = next;
    refreshList();
}
void addFiles(HWND window) {
    std::vector<wchar_t> buffer(65536, 0);
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = window;
    dialog.lpstrFilter = L"Все файлы\0*.*\0\0";
    dialog.lpstrFile = &buffer[0]; dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrTitle = L"Выберите файлы для расчёта MD5";
    dialog.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) {
        if (CommDlgExtendedError()) throw std::runtime_error("File selection failed (too many files may have been selected).");
        return;
    }
    std::vector<std::wstring> selected;
    std::wstring first(&buffer[0]);
    const wchar_t* next = &buffer[0] + first.size()+1;
    if (!*next) selected.push_back(first);
    else while (*next) {
        std::wstring name(next);
        selected.push_back(first + L"\\" + name);
        next += name.size()+1;
    }
    std::vector<std::wstring> updated = files;
    for (size_t i=0; i<selected.size(); ++i) {
        std::wstring path = md5gen::fullPath(selected[i]);
        if (!md5gen::absolutePath(path)) throw std::runtime_error("Unsupported file path");
        bool found = false;
        for (size_t j=0; j<updated.size(); ++j) if (md5gen::samePath(updated[j], path)) found = true;
        if (!found) updated.push_back(path);
    }
    commitFiles(updated);
}
void removeFiles() {
    int count = static_cast<int>(SendMessageW(listBox, LB_GETSELCOUNT, 0, 0));
    if (count <= 0) return;
    std::vector<int> selection(count);
    SendMessageW(listBox, LB_GETSELITEMS, count, reinterpret_cast<LPARAM>(&selection[0]));
    std::vector<std::wstring> updated = files;
    for (int i=count-1; i>=0; --i) updated.erase(updated.begin()+selection[i]);
    commitFiles(updated);
}
void setBusy(HWND window, bool value) {
    busy = value;
    const int controls[] = {ID_LIST, ID_ADD, ID_REMOVE, ID_GENERATE, ID_SAVE};
    for (size_t i=0; i<sizeof(controls)/sizeof(controls[0]); ++i) EnableWindow(GetDlgItem(window, controls[i]), !value);
}
struct Job {
    HWND window;
    std::vector<std::wstring> paths;
    std::wstring output;
    std::wstring message;
    bool failed;
};
unsigned __stdcall calculate(void* argument) {
    Job* job = static_cast<Job*>(argument);
    size_t errors = 0;
    try {
        std::string report;
        consoleWrite(L"\r\nИмя файла\tMD5\r\n");
        for (size_t i=0; i<job->paths.size(); ++i) {
            std::string line = md5gen::utf8(md5gen::baseName(job->paths[i])) + "\t";
            try { line += md5gen::hashFile(job->paths[i]); }
            catch (const std::exception& e) { ++errors; line += std::string("ОШИБКА: ") + e.what(); }
            line += "\r\n";
            report += line;
            consoleWrite(md5gen::wide(line));
            PostMessageW(job->window, WM_PROGRESS, i+1, job->paths.size());
        }
        if (!job->output.empty()) {
            md5gen::writeAtomic(job->output, report);
            consoleWrite(L"\r\nОтчёт сохранён: " + job->output + L"\r\n");
        }
        job->message = L"Готово. Рассчитано: " + std::to_wstring(job->paths.size()-errors) + L"; ошибок: " + std::to_wstring(errors) + L".";
        if (!job->output.empty()) job->message += L" Отчёт сохранён.";
        consoleWrite(job->message + L"\r\n");
        job->failed = false;
    } catch (const std::exception& e) {
        job->message = L"Не удалось завершить операцию: " + md5gen::wide(e.what());
        job->failed = true;
        consoleWrite(job->message + L"\r\n");
    }
    PostMessageW(job->window, WM_FINISHED, 0, reinterpret_cast<LPARAM>(job));
    return 0;
}
void generate(HWND window) {
    if (files.empty()) { MessageBoxW(window, L"Сначала добавьте файлы в список.", L"MD5Gen", MB_OK | MB_ICONINFORMATION); return; }
    std::wstring output;
    if (SendDlgItemMessageW(window, ID_SAVE, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        wchar_t path[32768] = L"md5_results.txt";
        OPENFILENAMEW dialog = {};
        dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = window;
        dialog.lpstrFilter = L"Текстовые файлы (*.txt)\0*.txt\0Все файлы\0*.*\0\0";
        dialog.lpstrFile = path; dialog.nMaxFile = 32768; dialog.lpstrDefExt = L"txt";
        dialog.lpstrTitle = L"Сохранить результаты MD5";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (!GetSaveFileNameW(&dialog)) {
            if (CommDlgExtendedError()) throw std::runtime_error("Save dialog failed");
            return;
        }
        output = md5gen::fullPath(path);
        if (md5gen::samePath(output, configPath)) throw std::runtime_error("The report cannot overwrite config.json.");
        for (size_t i=0; i<files.size(); ++i)
            if (md5gen::samePath(output, files[i])) throw std::runtime_error("The report cannot overwrite an input file.");
    }
    showConsole();
    std::unique_ptr<Job> job(new Job);
    job->window = window; job->paths = files; job->output = output; job->failed = false;
    setBusy(window, true);
    SetWindowTextW(statusLabel, L"Вычисление MD5…");
    uintptr_t thread = _beginthreadex(NULL, 0, calculate, job.get(), 0, NULL);
    if (!thread) { setBusy(window, false); SetWindowTextW(statusLabel, L"Не удалось запустить расчёт."); throw std::runtime_error("Cannot start worker thread"); }
    job.release();
    CloseHandle(reinterpret_cast<HANDLE>(thread));
}
HWND control(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style, int id) {
    HWND child = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, parent,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, NULL);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return child;
}
void layout(HWND window) {
    RECT r; GetClientRect(window, &r);
    int width = r.right, height = r.bottom;
    MoveWindow(GetDlgItem(window, ID_LABEL), 16, 14, width-32, 24, TRUE);
    MoveWindow(listBox, 16, 44, width-32, height-192, TRUE);
    MoveWindow(GetDlgItem(window, ID_ADD), 16, height-136, 180, 32, TRUE);
    MoveWindow(GetDlgItem(window, ID_REMOVE), 208, height-136, 210, 32, TRUE);
    MoveWindow(GetDlgItem(window, ID_SAVE), 16, height-94, 300, 28, TRUE);
    MoveWindow(GetDlgItem(window, ID_GENERATE), width-248, height-94, 232, 34, TRUE);
    MoveWindow(GetDlgItem(window, ID_CONSOLE), width-248, height-136, 232, 32, TRUE);
    MoveWindow(statusLabel, 16, height-44, width-32, 32, TRUE);
}
LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    try {
        switch (message) {
        case WM_CREATE: {
            font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            control(window, L"STATIC", L"Файлы для расчёта MD5 (полные пути):", 0, ID_LABEL);
            listBox = control(window, L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP | LBS_EXTENDEDSEL | LBS_NOINTEGRALHEIGHT, ID_LIST);
            control(window, L"BUTTON", L"Добавить файлы…", WS_TABSTOP, ID_ADD);
            control(window, L"BUTTON", L"Удалить выбранные", WS_TABSTOP, ID_REMOVE);
            control(window, L"BUTTON", L"Показать консоль", WS_TABSTOP, ID_CONSOLE);
            control(window, L"BUTTON", L"Сохранить результат в файл", BS_AUTOCHECKBOX | WS_TABSTOP, ID_SAVE);
            control(window, L"BUTTON", L"Сгенерировать MD5", BS_DEFPUSHBUTTON | WS_TABSTOP, ID_GENERATE);
            statusLabel = control(window, L"STATIC", L"", 0, ID_STATUS);
            refreshList(); layout(window); return 0;
        }
        case WM_SIZE: layout(window); return 0;
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize.x = 720;
            reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize.y = 400;
            return 0;
        case WM_COMMAND:
            if (HIWORD(wp) != BN_CLICKED) break;
            if (LOWORD(wp) == ID_CONSOLE) { showConsole(); return 0; }
            if (busy) return 0;
            switch (LOWORD(wp)) {
            case ID_ADD: addFiles(window); return 0;
            case ID_REMOVE: removeFiles(); return 0;
            case ID_GENERATE: generate(window); return 0;
            }
            break;
        case WM_PROGRESS: {
            std::wstring text = L"Обработано файлов: " + std::to_wstring(wp) + L" из " + std::to_wstring(lp);
            SetWindowTextW(statusLabel, text.c_str()); return 0;
        }
        case WM_FINISHED: {
            std::unique_ptr<Job> job(reinterpret_cast<Job*>(lp));
            setBusy(window, false);
            SetWindowTextW(statusLabel, job->message.c_str());
            if (job->failed) error(window, job->message);
            return 0;
        }
        case WM_CLOSE:
            if (busy) { MessageBoxW(window, L"Дождитесь завершения расчёта MD5.", L"MD5Gen", MB_OK | MB_ICONINFORMATION); return 0; }
            DestroyWindow(window); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        }
    } catch (const std::exception& e) { error(window, md5gen::wide(e.what())); return 0; }
    return DefWindowProcW(window, message, wp, lp);
}
}

int WINAPI WinMain(HINSTANCE app, HINSTANCE, LPSTR, int show) {
    instance = app;
    try {
        wchar_t module[32768];
        DWORD n = GetModuleFileNameW(NULL, module, 32768);
        if (!n || n >= 32768) throw std::runtime_error("Cannot locate executable");
        std::wstring exe(module, n);
        configPath = exe.substr(0, exe.find_last_of(L"\\/")+1) + L"config.json";
        DWORD attributes = GetFileAttributesW(configPath.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND)
            md5gen::writeAtomic(configPath, md5gen::encodeConfig(files));
        else files = md5gen::parseConfig(md5gen::readFile(configPath));
        WNDCLASSW wc = {};
        wc.lpfnWndProc = windowProc; wc.hInstance = app; wc.lpszClassName = L"MD5GenWindow";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);
        if (!RegisterClassW(&wc)) throw std::runtime_error("RegisterClass failed");
        mainWindow = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"MD5Gen — генератор MD5", WS_OVERLAPPEDWINDOW,
                                     CW_USEDEFAULT, CW_USEDEFAULT, 900, 560, NULL, NULL, app, NULL);
        if (!mainWindow) throw std::runtime_error("CreateWindow failed");
        ShowWindow(mainWindow, show); UpdateWindow(mainWindow);
        MSG message;
        BOOL result;
        while ((result = GetMessageW(&message, NULL, 0, 0)) > 0) {
            if (!IsDialogMessageW(mainWindow, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        return result == -1 ? 1 : static_cast<int>(message.wParam);
    } catch (const std::exception& e) {
        error(NULL, L"Ошибка запуска. Проверьте config.json рядом с программой.\nИсходный файл настроек не изменён.\n\n" + md5gen::wide(e.what()));
        return 1;
    }
}
