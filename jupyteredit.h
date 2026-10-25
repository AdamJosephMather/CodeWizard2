#pragma once

#include "textedit.h"
#include "button.h"
#include "checkbox.h"
#include "languageserverclient.h"
#include "brokenstatemenu.h"
#include "codeedit.h"
#include <deque>

class JupyterEdit : public Widget {
public:
    explicit JupyterEdit(Widget* parent);
    ~JupyterEdit() override;
    FileInfo* file = nullptr;
    bool dirty = false;
    void openFile(FileInfo* info);
    void save() override;
    std::string notebookText();
    void focusCell();
    void updateKernel() { std::lock_guard<std::recursive_mutex> guard(saveMutex); pollBridge(); }
    void position(int x, int y, int w, int h) override;
    void render() override;
    bool on_key_event(int key, int scancode, int action, int mods) override;
    bool on_char_event(unsigned int codepoint) override;
    bool on_mouse_button_event(int button, int action, int mods) override;
    bool on_mouse_move_event() override;
    bool on_scroll_event(double x, double y) override;
    void executeAction(WidgetActionType type) override;

private:
    struct Cell {
        json data;
        TextEdit* source = nullptr;
        CodeEdit* editor = nullptr;
        std::shared_ptr<FileInfo> virtualFile;
        TextEdit* output = nullptr;
        int top = 0;
        int height = 0;
        bool clearPending = false;
        bool outputDirty = false;
        std::map<std::string, std::vector<size_t>> displayOutputs;
    };
    std::vector<Cell> cells;
    json document;
    std::vector<Button*> toolbar;
    std::vector<Widget*> findWidgets;
    TextEdit* findEdit;
    TextEdit* replaceEdit;
    CheckBox* caseSensitive;
    Scrollbar* scrollbar;
    Scrollbar* horizontalScrollbar;
    Widget* scrollArea;
    BrokenStateMenu* conflictMenu;
    bool findOpen = false;
    bool valid = false;
    bool conflictReported = false;
    std::string diskText;
    std::recursive_mutex saveMutex;
    int selected = 0;
    double scroll = 0;
    double horizontalScroll = 0;
    int contentWidth = 0;
    int nextCellId = 0;
    int contentHeight = 0;
    int viewportTop = 0;
    int viewportBottom = 0;
    std::string status = "Checking Python kernels...";
    std::string kernelName;
    std::unique_ptr<Process> bridge;
    std::atomic<bool> bridgeExited{false};
    std::string incoming;
    bool kernelsChecked = false;
    int pythonAttempt = 0;
    double bridgeStarted = 0;
    int running = -1;
    std::deque<int> pending;

    void addCell(const json& data, int index);
    void insertCell(const std::string& type);
    void deleteCell();
    void moveCell(int direction);
    void changeCellType();
    void updateOutput(int index);
    void configureCellLanguage(Cell& cell);
    bool containsFocus(Widget* widget) const;
    void revealCursor();
    void reveal(int index);
    void find(bool forward, bool replace = false);
    void replaceAll();
    void startBridge();
    void stopBridge();
    void pollBridge();
    void handleMessage(const json& message);
    void runCell(bool advance);
    void runAll();
    void dispatch();
    void restartKernel();
    void toast(const std::string& message);
};
