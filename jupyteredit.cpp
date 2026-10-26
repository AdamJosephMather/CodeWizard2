#include "jupyteredit.h"
#include "jupyterbridge.h"
#include "codeedit.h"
#include "imageview.h"
#include <array>
#include <cctype>
#include "text_renderer.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <regex>

namespace {
std::string textValue(const json& value) {
    if (value.is_string()) return value.get<std::string>();
    std::string result;
    if (value.is_array()) for (const auto& line : value) result += line.get<std::string>();
    return result;
}
// Notebook image MIME values may be strings or arrays of base64 lines.
std::vector<std::uint8_t> notebookImageBytes(const json& value) {
    const std::string encoded = textValue(value);
    if (encoded.size() > 24*1024*1024) return {};
    static const auto table = [] {
        std::array<int,256> values; values.fill(-1);
        const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i=0; i<64; ++i) values[(unsigned char)alphabet[i]]=i;
        return values;
    }();
    std::vector<std::uint8_t> bytes;
    std::uint32_t accumulator = 0;
    int bits = 0;
    bool padding = false;
    for (unsigned char ch : encoded) {
        if (std::isspace(ch)) continue;
        if (ch == '=') { padding=true; continue; }
        if (padding || table[ch] < 0) return {};
        accumulator=(accumulator<<6)|(std::uint32_t)table[ch]; bits+=6;
        if (bits>=8) { bits-=8; bytes.push_back((accumulator>>bits)&255); }
    }
    if (bits == 6) return {};
    return bytes;
}
json sourceLines(const std::string& text) {
    auto lines = json::array();
    size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string::npos) end = text.size() - 1;
        lines.push_back(text.substr(start, end - start + 1));
        start = end + 1;
    }
    return lines;
}
Cursor selectionAt(const MST::MonoString& text, size_t start, size_t length) {
    Cursor c;
    int line = 0, column = 0;
    for (size_t i = 0; i <= start + length; ++i) {
        if (i == start) { c.anchor_line = line; c.anchor_char = column; }
        if (i == start + length) { c.head_line = line; c.head_char = column; break; }
        if (text.data[i] == U'\n') { ++line; column = 0; } else ++column;
    }
    c.preffered_collumn = c.head_char;
    return c;
}
size_t cursorOffset(TextEdit* edit, bool end) {
    auto range = edit->_getCursSelec(edit->cursors.front());
    int line = end ? range.first.second : range.first.first;
    size_t result = end ? range.second.second : range.second.first;
    for (int i = 0; i < line; ++i) result += edit->lines[i].line_text.length + 1;
    return result;
}
}

JupyterEdit::JupyterEdit(Widget* parent) : Widget(parent) {
    id = MST::toMonoString("JupyterEdit");
    before_self_close = [this] { save(); stopBridge(); };
    auto button = [this](const char* label, std::function<void()> action) {
        auto* b = new Button(this, MST::toMonoString(label), [](Button*, int,int,int,int,int,int){},
            [action](Button*) { action(); });
        b->rounded = true;
        toolbar.push_back(b);
    };
    button("Run", [this] { runCell(false); });
    button("Run all", [this] { runAll(); });
    button("Interrupt", [this] { pending.clear(); if (bridge) bridge->write(json({{"command", "interrupt"}}).dump()+"\n"); });
    button("Restart", [this] { restartKernel(); });
    button("+ Code", [this] { insertCell("code"); });
    button("+ Markdown", [this] { insertCell("markdown"); });
    button("Type", [this] { changeCellType(); });
    button("Up", [this] { moveCell(-1); });
    button("Down", [this] { moveCell(1); });
    button("Delete", [this] { deleteCell(); });
    button("Clear outputs", [this] {
        if (running >= 0) return;
        for (int i = 0; i < (int)cells.size(); ++i) if (cells[i].data["cell_type"] == "code") {
            cells[i].data["outputs"] = json::array(); cells[i].data["execution_count"] = nullptr; updateOutput(i);
            cells[i].displayOutputs.clear();
        }
        dirty = true;
    });
    findEdit = new TextEdit(this, [](Widget*) {});
    replaceEdit = new TextEdit(this, [](Widget*) {});
    caseSensitive = new CheckBox(this, [](CheckBox*,int,int,int,int){}, nullptr);
    findEdit->rounded = replaceEdit->rounded = caseSensitive->rounded = true;
    caseSensitive->is_checked = false;
    findWidgets = {findEdit, replaceEdit, caseSensitive};
    auto findButton = [this](const char* label, std::function<void()> action) {
        auto* b = new Button(this, MST::toMonoString(label), [](Button*,int,int,int,int,int,int){},
            [action](Button*) { action(); });
        b->rounded = true;
        findWidgets.push_back(b);
    };
    findButton("←-", [this] { find(false); });
    findButton("-→", [this] { find(true); });
    findButton("-→", [this] { find(true, true); });
    findButton("All", [this] { replaceAll(); });
    scrollArea = new Widget(this);
    scrollbar = new Scrollbar(scrollArea);
    scrollbar->getScrollInfo = [this] {
        double total = std::max(1, contentHeight);
        return std::vector<double>{scroll/total, std::min(1.0, (scroll+viewportBottom-viewportTop)/total)};
    };
    scrollbar->scrollTo = [this](double fraction) { scroll = fraction*contentHeight; App::reclear = 3; };
    horizontalScrollbar = new Scrollbar(scrollArea);
    horizontalScrollbar->horizontal = true;
    horizontalScrollbar->getScrollInfo = [this] {
        const double width = std::max(1, contentWidth);
        return std::vector<double>{horizontalScroll/width, std::min(1.0, (horizontalScroll+std::max(1,t_w-App::text_padding*5-12))/width)};
    };
    horizontalScrollbar->scrollTo = [this](double fraction) { horizontalScroll=fraction*contentWidth; App::reclear=3; };
    conflictMenu = new BrokenStateMenu(this, "Reload", "Overwrite", "Notebook changed on disk. Reload discards edits; Overwrite keeps them.");
    conflictMenu->hide();
    conflictMenu->first_callback = [this] {
        stopBridge();
        for (auto& c : cells) {
            App::RemoveWidgetFromParent(c.editor); App::deleteWidget(c.editor);
            clearOutputWidgets(c);
            App::RemoveWidgetFromParent(c.output); App::deleteWidget(c.output);
        }
        cells.clear(); valid=false; dirty=false; conflictReported=false; selected=0; scroll=0;
        conflictMenu->hide(); pythonAttempt=0; kernelName.clear();
        openFile(file); App::reclear=3;
    };
    conflictMenu->second_callback = [this] {
        std::vector<std::uint8_t> bytes;
        std::string error;
        if (!file->backend->readFile(file->filepath,bytes,error)) { toast("Cannot overwrite notebook: "+error); return; }
        diskText.assign(bytes.begin(),bytes.end());
        conflictReported=false; conflictMenu->hide(); save(); focusCell(); App::reclear=3;
    };
}

JupyterEdit::~JupyterEdit() { stopBridge(); }

void JupyterEdit::toast(const std::string& message) { App::displayToast(MST::toMonoString(message)); }

void JupyterEdit::addCell(const json& data, int index) {
    Cell cell;
    cell.data = data;
    cell.editor = new CodeEdit(this, -1, [](Widget*) {}, [](Widget*, FileInfo*) {}, true);
    cell.source = cell.editor->textedit;
    cell.source->POS_FUNC = [](Widget*) {};
    cell.output = new TextEdit(this, [](Widget*) {});
    cell.source->scrollbar_horizontal = false;
    cell.source->scrollbar_vertical = false;
    cell.source->DONT_SCROLL_VERT_CURS = true;
    cell.source->fit_content = true;
    cell.source->rounded = cell.output->rounded = true;
    cell.source->borderColor = App::theme.border;
    cell.source->activeBorderColor = App::theme.active_color;
    // LSP popups are painted over neighbouring cells, inside the notebook.
    auto boundPopup = [this](Widget* popup) {
        const int pad = App::text_padding;
        const int line = TextRenderer::get_text_height();
        const int maxWidth = dynamic_cast<ListBox*>(popup) ? t_w/3 : t_w/2;
        popup->t_w = std::clamp(popup->t_w, 1, std::max(1,maxWidth-pad*2));
        int contentHeight = popup->t_h;
        if (auto* text = dynamic_cast<TextEdit*>(popup)) {
            contentHeight = line*std::min(12,(int)text->lines.size())+pad*2;
            text->scrollbar_vertical = text->lines.size() > 12;
        } else if (auto* list = dynamic_cast<ListBox*>(popup)) {
            list->toshow = std::max(1,std::min({(int)list->elements.size(),8,
                std::max(1,(viewportBottom-viewportTop-pad*2)/(line+pad))}));
            contentHeight = (line+pad)*list->toshow;
        }
        popup->t_h = std::clamp(contentHeight, 1, std::max(1,viewportBottom-viewportTop-pad*2));
        if (auto* owner = dynamic_cast<CodeEdit*>(popup->const_parent)) {
            if (popup == owner->errorMenu) popup->t_y=owner->showErrorsButton->t_y-popup->t_h-pad;
            else {
                const auto& cursor = popup == owner->hoverbox ? owner->hoverCrsr : popup == owner->renamebox ? owner->renamecursor : owner->textedit->cursors.front();
                popup->t_y=owner->textedit->t_y+(cursor.head_line+1)*line+pad;
                if (popup->t_y+popup->t_h > viewportBottom) popup->t_y-=line+popup->t_h;
            }
        }
        popup->t_x = std::clamp(popup->t_x, t_x+pad*2, std::max(t_x+pad*2,t_x+t_w-pad*2-popup->t_w));
        popup->t_y = std::clamp(popup->t_y, viewportTop, std::max(viewportTop,viewportBottom-popup->t_h));
    };
    for (auto* popup : {cell.editor->completionbox,cell.editor->errorMenu}) {
        auto original = popup->pFunc;
        popup->pFunc = [original,boundPopup](Widget* widget) { original(widget); boundPopup(widget); };
        popup->exempt_from_parent_for_cursor = true;
        popup->rounded = true;
    }
    for (auto* popup : {cell.editor->hoverbox,cell.editor->renamebox}) {
        auto original = popup->POS_FUNC;
        popup->POS_FUNC = [original,boundPopup](Widget* widget) { original(widget); boundPopup(widget); };
        popup->exempt_from_parent_for_cursor = true;
        popup->rounded = true;
    }
    auto errorButtonPosition = cell.editor->showErrorsButton->POSITIONER;
    auto* source = cell.source;
    cell.editor->showErrorsButton->POSITIONER = [this,errorButtonPosition,source](Button* button,int x,int y,int w,int h,int tw,int th) {
        errorButtonPosition(button,x,y,w,h,tw,th);
        button->t_x = std::clamp(button->t_x,t_x+App::text_padding,std::max(t_x+App::text_padding,t_x+t_w-tw-App::text_padding*3));
        const int top=std::max(viewportTop,source->t_y);
        const int bottom=std::min(viewportBottom,source->t_y+source->t_h);
        // A short/clipped source may not have room for the button. Its drawing
        // and hit testing are clipped to the source below, rather than moving
        // it into the following cell.
        button->t_y = std::clamp(button->t_y,top,std::max(top,bottom-th));
    };
    cell.editor->showErrorsButton->exempt_from_parent_for_cursor = true;
    cell.source->useTabs = false;
    cell.source->getIndentationLevelAfterLine = CodeEdit::indentIdentifierAfterLine;
    cell.output->allow_edits = false;
    cell.output->scrollbar_horizontal = true;
    cell.output->scrollbar_vertical = true;
    cell.source->setFullText(MST::toMonoString(textValue(data.at("source"))));
    if (cell.source->getFullText().length == 0) cell.source->useTabs = false;
    cell.virtualFile = std::make_shared<FileInfo>();
    cell.virtualFile->filepath = file->filepath+".cw-cell-"+std::to_string(nextCellId++);
    cell.virtualFile->backend = file->backend;
    configureCellLanguage(cell);
    auto onChange = cell.source->ontextchange;
    cell.source->ontextchange = [this, onChange](Widget* source) {
        if (onChange) onChange(source);
        dirty = true; App::reclear = 3;
    };
    cells.insert(cells.begin()+index, std::move(cell));
    updateOutput(index);
}

void JupyterEdit::configureCellLanguage(Cell& cell) {
    const std::string extension = cell.data["cell_type"] == "code" ? ".py" : cell.data["cell_type"] == "markdown" ? ".md" : ".txt";
    auto info = std::make_shared<FileInfo>(*cell.virtualFile);
    std::string path = info->filepath;
    if (cell.editor->file) path = std::filesystem::u8path(path).replace_extension().u8string();
    info->filepath = path+extension;
    info->filename = std::filesystem::u8path(info->filepath).filename().u8string();
    cell.editor->openVirtualDocument(info.get(), cell.source->getFullText());
    cell.virtualFile = std::move(info);
}

bool JupyterEdit::containsFocus(Widget* widget) const {
    for (auto* ancestor=App::activeLeafNode; ancestor; ancestor=ancestor->parent) if (ancestor==widget) return true;
    return false;
}

void JupyterEdit::openFile(FileInfo* info) {
    std::lock_guard<std::recursive_mutex> guard(saveMutex);
    file = info;
    if (!file->backend) file->backend = FileBackends::current();
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!file->backend->readFile(file->filepath, bytes, error)) { status = "Cannot read notebook: "+error; toast(status); return; }
    try {
        diskText.assign(bytes.begin(), bytes.end());
        document = json::parse(diskText);
        if (!document.is_object() || document.at("nbformat") != 4 || !document.at("cells").is_array() || !document.at("metadata").is_object())
            throw std::runtime_error("Expected a Jupyter notebook in nbformat 4.");
        // Validate all cells before creating widgets; malformed files must never be autosaved.
        for (const auto& c : document["cells"]) {
            auto type = c.at("cell_type").get<std::string>();
            if (type != "code" && type != "markdown" && type != "raw") throw std::runtime_error("Unsupported cell type.");
            textValue(c.at("source"));
            if (!c.at("source").is_string() && !c.at("source").is_array()) throw std::runtime_error("Invalid cell source.");
            if (type == "code" && !c.at("outputs").is_array()) throw std::runtime_error("Invalid cell outputs.");
        }
        for (const auto& c : document["cells"]) addCell(c, (int)cells.size());
        valid = true;
        file->is_opening = false;
        dirty = false;
        focusCell();
        if (file->backend->isRemote()) { status = "Remote notebook: editing only (local kernel execution disabled)."; return; }
        startBridge();
    } catch (const std::exception& e) { status = std::string("Cannot open notebook: ")+e.what(); toast(status); }
}

std::string JupyterEdit::notebookText() {
    std::lock_guard<std::recursive_mutex> guard(saveMutex);
    json result = document;
    result["cells"] = json::array();
    for (auto& c : cells) {
        auto data = c.data;
        data["source"] = sourceLines(MST::toString(c.source->getFullText()));
        result["cells"].push_back(std::move(data));
    }
    return result.dump(1, '\t')+"\n";
}

void JupyterEdit::save() {
    std::lock_guard<std::recursive_mutex> guard(saveMutex);
    for (auto& cell : cells) cell.source->flushPendingCharInput();
    if (!valid || !dirty || !file || file->is_opening) return;
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!file->backend->readFile(file->filepath, bytes, error) || std::string(bytes.begin(), bytes.end()) != diskText) {
        if (!conflictReported) toast("Notebook changed on disk or is unavailable. Choose Reload or Overwrite to resolve.");
        conflictReported = true;
        return;
    }
    auto text = notebookText();
    bytes.assign(text.begin(), text.end());
    if (!file->backend->writeFile(file->filepath, bytes, error)) { toast("Cannot save notebook: "+error); return; }
    diskText = std::move(text);
    dirty = false;
    conflictReported = false;
}

void JupyterEdit::focusCell() {
    if (cells.empty()) App::setActiveLeafNode(this);
    else { selected = std::clamp(selected, 0, (int)cells.size()-1); App::setActiveLeafNode(cells[selected].source); }
}

void JupyterEdit::insertCell(const std::string& type) {
    if (!valid || running >= 0) return;
    json data = {{"cell_type", type}, {"metadata", json::object()}, {"source", json::array()}};
    // Notebook 4.5 cell IDs need to remain unique across insertions and reloads.
    static unsigned long serial = 0;
    data["id"] = "cw-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial);
    if (type == "code") { data["outputs"] = json::array(); data["execution_count"] = nullptr; }
    selected = cells.empty() ? 0 : selected+1;
    addCell(data, selected);
    dirty = true;
    reveal(selected);
    focusCell();
}

void JupyterEdit::deleteCell() {
    if (cells.empty() || running >= 0) return;
    auto cell = cells[selected];
    cells.erase(cells.begin()+selected);
    App::RemoveWidgetFromParent(cell.editor); App::deleteWidget(cell.editor);
    clearOutputWidgets(cell);
    App::RemoveWidgetFromParent(cell.output); App::deleteWidget(cell.output);
    dirty = true;
    focusCell();
}

void JupyterEdit::moveCell(int direction) {
    int next = selected+direction;
    if (running >= 0 || next < 0 || next >= (int)cells.size()) return;
    std::swap(cells[selected], cells[next]); selected = next; dirty = true; reveal(selected); focusCell();
}

void JupyterEdit::changeCellType() {
    if (cells.empty() || running >= 0) return;
    auto& c = cells[selected];
    bool code = c.data["cell_type"] != "code";
    c.displayOutputs.clear();
    c.data["cell_type"] = code ? "code" : "markdown";
    if (code) { c.data["outputs"] = json::array(); c.data["execution_count"] = nullptr; }
    else { c.data.erase("outputs"); c.data.erase("execution_count"); }
    configureCellLanguage(c);
    for (auto& line : c.source->lines) { line.highlightinguptodate = false; line.changed = true; }
    c.source->alreadyHighlighted = false;
    updateOutput(selected); dirty = true;
}

void JupyterEdit::clearOutputWidgets(Cell& cell) {
    for (auto* widget : cell.outputParts) if (widget != cell.output) {
        if (containsFocus(widget)) App::setActiveLeafNode(cell.source);
        App::RemoveWidgetFromParent(widget); App::deleteWidget(widget);
    }
    cell.outputParts.clear();
}

int JupyterEdit::outputHeight(Widget* widget, int width) const {
    const int pad=App::text_padding, line=TextRenderer::get_text_height();
    if (auto* image=dynamic_cast<ImageView*>(widget))
        return std::max(1,(int)(image->imgH*std::min(1.0,(double)std::max(1,width-pad*2)/image->imgW)))+pad*2;
    auto* text=static_cast<TextEdit*>(widget);
    return line*std::clamp((int)text->lines.size(),1,16)+pad*2;
}

void JupyterEdit::updateOutput(int index) {
    auto& c = cells[index];
    auto previous = std::move(c.outputParts);
    c.outputParts.clear();
    bool usedFirstText = false;
    std::string text;
    auto flushText = [&] {
        if (text.empty()) return;
        TextEdit* editor = nullptr;
        if (!usedFirstText) { editor=c.output; usedFirstText=true; }
        else if (c.outputParts.size()<previous.size()) editor=dynamic_cast<TextEdit*>(previous[c.outputParts.size()]);
        if (!editor || (editor==c.output &&
            std::find(c.outputParts.begin(),c.outputParts.end(),editor)!=c.outputParts.end()))
            editor=new TextEdit(this,[](Widget*) {});
        editor->allow_edits=false; editor->rounded=true;
        editor->scrollbar_horizontal=editor->scrollbar_vertical=true;
        static const std::regex ansi("\x1b\\[[0-?]*[ -/]*[@-~]");
        text=std::regex_replace(text,ansi,"");
        const double vertical=editor->scrolled_to_vert, horizontal=editor->scrolled_to_horz;
        editor->setFullText(MST::toMonoString(text));
        editor->scrolled_to_vert=std::min(vertical,(double)std::max(0,(int)editor->lines.size()-1));
        editor->scrolled_to_horz=horizontal;
        c.outputParts.push_back(editor); text.clear();
    };
    if (c.data.contains("outputs")) for (auto& out : c.data["outputs"]) {
        std::string type=out.value("output_type","");
        if (type=="stream") text+=textValue(out.value("text",json("")));
        else if (type=="error") {
            if (out.contains("traceback")) for (auto& line : out["traceback"]) text+=line.get<std::string>()+"\n";
            else text+=out.value("ename","Error")+": "+out.value("evalue","")+"\n";
        } else if (out.contains("data")) {
            bool displayed=false;
            const auto& data=out["data"];
            for (const auto* mime : {"image/png","image/jpeg","image/gif"}) if (data.contains(mime)) {
                auto bytes=notebookImageBytes(data[mime]);
                // Flush text first so stdout, plots, and later text retain order.
                flushText();
                ImageView* image=c.outputParts.size()<previous.size() ? dynamic_cast<ImageView*>(previous[c.outputParts.size()]) : nullptr;
                bool created=!image;
                if (created) image=new ImageView(this);
                if (image->setImageData(bytes)) { c.outputParts.push_back(image); displayed=true; break; }
                if (created) { App::RemoveWidgetFromParent(image); App::deleteWidget(image); }
            }
            if (!displayed) {
                if (data.contains("text/plain")) text+=textValue(data["text/plain"])+"\n";
                else text+="[Rich output preserved in notebook]\n";
            }
        }
    }
    flushText();
    c.output->is_visible=false;
    if (!usedFirstText && containsFocus(c.output)) App::setActiveLeafNode(c.source);
    for (auto* widget : previous) if (widget!=c.output && std::find(c.outputParts.begin(),c.outputParts.end(),widget)==c.outputParts.end()) {
        if (containsFocus(widget)) App::setActiveLeafNode(c.source);
        App::RemoveWidgetFromParent(widget); App::deleteWidget(widget);
    }
}

void JupyterEdit::reveal(int index) {
    // Recalculate first: newly inserted/moved cells do not yet have positions.
    position(t_x, t_y, t_w, t_h);
    if (index < 0 || index >= (int)cells.size()) return;
    revealCursor();
    App::reclear = 3;
}

void JupyterEdit::revealCursor() {
    if (cells.empty()) return;
    const auto& cell = cells[selected];
    const auto& cursor = cell.source->cursors.front();
    const int line = TextRenderer::get_text_height(), pad = App::text_padding;
    const double top = cell.top+line+pad*2+cursor.head_line*line;
    const int height = std::max(line,viewportBottom-viewportTop);
    if (top < scroll+pad) scroll=std::max(0.0,top-pad);
    else if (top+line > scroll+height-pad) scroll=top+line-height+pad;
    auto prefix = MST::substring(cell.source->lines[cursor.head_line].line_text,0,cursor.head_char);
    const double left = cell.source->getVisLen(prefix)*TextRenderer::get_text_width(1);
    const int width = std::max(1,t_w-pad*5-12);
    if (left < horizontalScroll) horizontalScroll=left;
    else if (left+pad*3 > horizontalScroll+width) horizontalScroll=left+pad*3-width;
}

void JupyterEdit::find(bool forward, bool replace) {
    if (cells.empty()) return;
    auto needle = findEdit->getFullText();
    if (!needle.length) return;
    auto* edit = cells[selected].source;
    if (replace) {
        auto have = edit->getSelectedText(edit->cursors.front());
        if (caseSensitive->is_checked ? have == needle : MST::toLower(have) == MST::toLower(needle)) {
            edit->cursors = {edit->cursors.front()};
            edit->applyInsertToAllCursors(replaceEdit->getFullText()); edit->updateUndoHistory(); dirty = true;
        }
    }
    struct Match { int cell; size_t offset; };
    std::vector<Match> matches;
    for (int i = 0; i < (int)cells.size(); ++i) {
        auto text = cells[i].source->getFullText();
        size_t offset = 0;
        while ((offset = MST::index(text, offset, needle, !caseSensitive->is_checked)) != MST::NOT_FOUND) {
            matches.push_back({i, offset}); offset += needle.length;
        }
    }
    if (matches.empty()) { toast("No notebook matches."); return; }
    size_t start = cursorOffset(edit, forward);
    Match chosen = forward ? matches.front() : matches.back();
    if (forward) {
        for (auto m : matches) if (m.cell > selected || (m.cell == selected && m.offset >= start)) { chosen = m; break; }
    } else {
        for (auto it = matches.rbegin(); it != matches.rend(); ++it)
            if (it->cell < selected || (it->cell == selected && it->offset+needle.length <= start)) { chosen = *it; break; }
    }
    selected = chosen.cell;
    edit = cells[selected].source;
    edit->cursors = {selectionAt(edit->getFullText(), chosen.offset, needle.length)};
    edit->tryingToEnsureCursorPos = true;
    reveal(selected);
}

void JupyterEdit::replaceAll() {
    auto needle = findEdit->getFullText();
    if (!needle.length) return;
    for (auto& c : cells) {
        auto text = c.source->getFullText();
        std::vector<size_t> offsets;
        size_t offset = 0;
        while ((offset = MST::index(text, offset, needle, !caseSensitive->is_checked)) != MST::NOT_FOUND) {
            offsets.push_back(offset); offset += needle.length;
        }
        // Reverse order preserves offsets and records ordinary TextEdit undo history.
        for (auto it = offsets.rbegin(); it != offsets.rend(); ++it)
            c.source->insertTextAtCursor(selectionAt(text, *it, needle.length), replaceEdit->getFullText());
        if (!offsets.empty()) { c.source->updateUndoHistory(); dirty = true; }
    }
    App::reclear = 3;
}

void JupyterEdit::startBridge() {
    if (!valid || !file || file->backend->isRemote()) return;
    kernelsChecked = false;
    bridgeExited = false;
    incoming.clear();
    bridgeStarted = glfwGetTime();
    bridge = std::make_unique<Process>();
    bridge->finishedCallback = [this](int, Process::ExitStatus) { bridgeExited = true; };
    std::string configured = App::settings->getValue("jupyter_python", std::string(""));
#ifdef _WIN32
    const std::vector<std::string> candidates = {"python", "py"};
#else
    const std::vector<std::string> candidates = {"python3", "python"};
#endif
    std::string executable = configured.empty() ? candidates[std::min(pythonAttempt, (int)candidates.size()-1)] : configured;
    status = "Checking Python kernels...";
    if (!bridge->start(executable, {"-u", "-c", jupyterBridge})) bridgeExited = true;
}

void JupyterEdit::stopBridge() {
    if (!bridge) return;
    if (!bridgeExited) {
        bridge->write("{\"command\":\"shutdown\"}\n");
        bridge->waitForFinished(1500);
    }
    bridge->dispose();
    // Destruction joins its reader before callbacks can outlive this widget.
    bridge.reset();
    running = -1; pending.clear();
}

void JupyterEdit::pollBridge() {
    if (!bridge) return;
    if (!kernelsChecked && glfwGetTime()-bridgeStarted > 15) {
        status = "Python/Jupyter kernel discovery timed out. Check jupyter_python or install jupyter_client and ipykernel.";
        bridge->terminate(); bridgeExited = true;
    }
    incoming += bridge->readAll();
    size_t end, consumed = 0;
    const double deadline = glfwGetTime()+0.004;
    int messages = 0;
    while (messages++ < 64 && glfwGetTime() < deadline && (end = incoming.find('\n', consumed)) != std::string::npos) {
        std::string line = incoming.substr(consumed, end-consumed); consumed = end+1;
        auto message = json::parse(line, nullptr, false);
        if (!message.is_discarded() && message.is_object()) {
            try { handleMessage(message); }
            catch (const std::exception& error) { status = std::string("Kernel protocol error: ")+error.what(); toast(status); }
        }
    }
    incoming.erase(0, consumed);
    for (int i=0; i<(int)cells.size(); ++i) if (cells[i].outputDirty) {
        cells[i].outputDirty = false;
        updateOutput(i);
    }
    if (!incoming.empty()) App::reclear = 3;
    if (!bridgeExited || incoming.find('\n') != std::string::npos) return;
    incoming.clear();
    bool checked = kernelsChecked;
    bridge->dispose();
    bridge.reset();
    running = -1; pending.clear();
    constexpr int attempts = 2;
    if (!checked && App::settings->getValue("jupyter_python", std::string("")).empty() && ++pythonAttempt < attempts) { startBridge(); return; }
    if (checked) status = "Kernel connection closed. Use Restart to reconnect.";
    else if (status == "Checking Python kernels...") status = "Python/Jupyter kernel not found. Install Python, jupyter_client and ipykernel.";
    toast(checked ? "Jupyter connection closed (see Help)." : "Jupyter unavailable (see Help).");
}

void JupyterEdit::handleMessage(const json& message) {
    App::reclear = 3;
    std::string event = message.value("event", "");
    if (event == "fatal") { status = message.at("message").get<std::string>(); return; }
    if (event == "kernels") {
        kernelsChecked = true;
        auto names = message.at("names").get<std::vector<std::string>>();
        std::string requested = App::settings->getValue("jupyter_kernel", std::string(""));
        if (requested.empty() && document["metadata"].contains("kernelspec"))
            requested = document["metadata"]["kernelspec"].value("name", "");
        if (requested.empty()) requested = "python3";
        if (std::find(names.begin(), names.end(), requested) == names.end()) {
            status = "Python kernel '"+requested+"' is not installed. Install ipykernel/register that kernel, or set jupyter_kernel.";
            kernelName.clear(); toast("Python kernel missing (see Help).");
        } else { kernelName = requested; status = "Kernel: "+kernelName+" (starts when a cell runs)"; }
        return;
    }
    int index = message.value("cell", -1);
    if (index < 0 || index >= (int)cells.size()) return;
    auto& cell = cells[index];
    if (event == "done" || event == "error") {
        running = -1;
        if (event == "error") { pending.clear(); status = message.at("message").get<std::string>(); toast(status); }
        else { status = "Kernel: "+kernelName+" (idle)"; dispatch(); }
        return;
    }
    if (event != "output") return;
    std::string kind = message.at("kind");
    const auto& content = message.at("content");
    auto& outputs = cell.data["outputs"];
    if (kind == "execute_input") cell.data["execution_count"] = content["execution_count"];
    else if (kind == "clear_output") {
        cell.clearPending = content.value("wait", false);
        if (!cell.clearPending) { outputs = json::array(); cell.displayOutputs.clear(); }
    } else if (kind == "update_display_data") {
        const std::string displayId = content.value("transient", json::object()).value("display_id", "");
        if (!displayId.empty()) for (int i=0; i<(int)cells.size(); ++i) {
            auto found = cells[i].displayOutputs.find(displayId);
            if (found == cells[i].displayOutputs.end()) continue;
            for (auto outputIndex : found->second) {
                auto& previous = cells[i].data["outputs"][outputIndex];
                previous["data"] = content["data"]; previous["metadata"] = content["metadata"];
            }
            cells[i].outputDirty = true;
        }
    } else {
        if (cell.clearPending) { outputs = json::array(); cell.displayOutputs.clear(); cell.clearPending = false; }
        json output = content;
        output.erase("transient");
        output["output_type"] = kind;
        if (kind == "error") pending.clear();
        const std::string displayId = content.value("transient", json::object()).value("display_id", "");
        if (!displayId.empty()) cell.displayOutputs[displayId].push_back(outputs.size());
        if (kind == "stream" && !outputs.empty() && outputs.back().value("output_type", "") == "stream" &&
            outputs.back().value("name", "") == output.value("name", ""))
            outputs.back()["text"] = textValue(outputs.back()["text"])+textValue(output["text"]);
        else outputs.push_back(std::move(output));
    }
    dirty = true; cell.outputDirty = true; App::reclear = 3;
}

void JupyterEdit::runCell(bool advance) {
    if (cells.empty() || !valid) return;
    if (cells[selected].data["cell_type"] == "code") {
        if (!bridge || kernelName.empty() || !kernelsChecked) { toast("Kernel unavailable (see Help)."); return; }
        if (running >= 0) { toast("Kernel is busy. Interrupt it or wait for completion."); return; }
        pending.push_back(selected); dispatch();
    }
    if (advance && selected+1 < (int)cells.size()) { ++selected; reveal(selected); focusCell(); }
}

void JupyterEdit::runAll() {
    if (!bridge || kernelName.empty() || !kernelsChecked) { toast("Kernel unavailable (see Help)."); return; }
    if (running >= 0) { toast("Kernel is busy."); return; }
    pending.clear();
    for (int i = 0; i < (int)cells.size(); ++i) if (cells[i].data["cell_type"] == "code") pending.push_back(i);
    dispatch();
}

void JupyterEdit::dispatch() {
    if (!bridge || running >= 0 || pending.empty()) return;
    running = pending.front(); pending.pop_front();
    auto& cell = cells[running];
    cell.source->flushPendingCharInput();
    cell.data["outputs"] = json::array(); cell.data["execution_count"] = nullptr; cell.clearPending = false;
    cell.displayOutputs.clear();
    dirty = true; updateOutput(running);
    std::string cwd = std::filesystem::absolute(std::filesystem::u8path(file->filepath)).parent_path().u8string();
    bridge->write(json({{"command", "execute"}, {"cell", running}, {"kernel", kernelName}, {"cwd", cwd},
        {"code", MST::toString(cell.source->getFullText())}}).dump()+"\n");
    status = "Kernel: "+kernelName+" (running cell "+std::to_string(running+1)+")";
    App::reclear = 3;
}

void JupyterEdit::restartKernel() {
    stopBridge(); kernelName.clear(); pythonAttempt = 0; startBridge(); App::reclear = 3;
}

void JupyterEdit::position(int x, int y, int w, int h) {
    std::lock_guard<std::recursive_mutex> guard(saveMutex);
    t_x=x; t_y=y; t_w=w; t_h=h;
    pollBridge();
    const int pad = App::text_padding;
    const int line = TextRenderer::get_text_height();
    const int row = line+pad*2;
    conflictMenu->is_visible = conflictReported;
    if (conflictReported) { conflictMenu->show(); conflictMenu->position(x,y,w,h); }
    int bx = x+pad, by = y+pad;
    for (auto* button : toolbar) {
        button->position(x,y,w,h);
        if (bx+button->t_w > x+w-pad && bx > x+pad) { bx=x+pad; by+=row+pad; }
        button->t_x=bx; button->t_y=by;
        bx+=button->t_w+pad;
        button->is_visible = valid;
    }
    const int findHeight = (line*std::min(3,(int)findEdit->lines.size())+pad*2)
        +(line*std::min(3,(int)replaceEdit->lines.size())+pad*2)+pad*3;
    viewportTop = by+row+line+pad*2;
    viewportBottom = y+h-pad-(findOpen ? findHeight : 0);
    viewportBottom = std::max(viewportTop+1, viewportBottom);
    const int sourceWidth=std::max(1,w-pad*5-12);
    contentWidth=sourceWidth;
    int total=0;
    for (auto& c : cells) {
        c.source->flushPendingCharInput();
        c.top=total;
        int sourceHeight = pad*2+line*std::max(1,(int)c.source->lines.size());
        for (const auto& textLine : c.source->lines)
            contentWidth=std::max(contentWidth,c.source->getVisLen(textLine.line_text)*TextRenderer::get_text_width(1)+pad*3);
        c.height = line+pad+sourceHeight+pad*2;
        for (auto* output : c.outputParts) c.height+=outputHeight(output,sourceWidth)+pad;
        total+=c.height;
    }
    contentHeight=total;
    const bool horizontal = contentWidth>sourceWidth;
    if (horizontal) viewportBottom=std::max(viewportTop+1,viewportBottom-TextRenderer::get_text_width(1)*2);
    if (!cells.empty() && cells[selected].source->tryingToEnsureCursorPos) revealCursor();
    scroll=std::clamp(scroll,0.0,(double)std::max(0,total-(viewportBottom-viewportTop)));
    horizontalScroll=std::clamp(horizontalScroll,0.0,(double)std::max(0,contentWidth-sourceWidth));
    for (auto& c : cells) {
        int cy=viewportTop+c.top-(int)scroll;
        int sourceHeight=pad*2+line*std::max(1,(int)c.source->lines.size());
        bool visible=cy+c.height > viewportTop && cy < viewportBottom;
        c.source->is_visible=visible;
        c.editor->is_visible=visible;
        c.output->is_visible=false;
        c.editor->position(x+pad*2-(int)horizontalScroll,cy+line+pad,contentWidth,sourceHeight);
        int outputY=cy+line+pad+sourceHeight+pad;
        for (auto* output : c.outputParts) {
            const int height=outputHeight(output,sourceWidth);
            output->position(x+pad*2,outputY,sourceWidth,height);
            output->is_visible=visible && outputY+height>viewportTop && outputY<viewportBottom;
            output->cursor_in_this=output->is_visible && cursor_in_this && App::mouseY>=std::max(viewportTop,outputY) &&
                App::mouseY<std::min(viewportBottom,outputY+height) && App::mouseX>=output->t_x && App::mouseX<output->t_x+output->t_w;
            outputY+=height+pad;
        }
        // Cells are clipped at the notebook viewport, including hit testing.
        c.source->cursor_in_this = visible && cursor_in_this && App::mouseY >= viewportTop && App::mouseY < viewportBottom && App::mouseX >= c.source->t_x && App::mouseX < c.source->t_x+c.source->t_w && App::mouseY >= c.source->t_y && App::mouseY < c.source->t_y+c.source->t_h;
        c.editor->cursor_in_this = c.source->cursor_in_this;
        auto* errorButton=c.editor->showErrorsButton;
        errorButton->cursor_in_this=c.source->cursor_in_this && App::mouseX>=errorButton->t_x && App::mouseX<errorButton->t_x+errorButton->t_w && App::mouseY>=errorButton->t_y && App::mouseY<errorButton->t_y+errorButton->t_h;
        if (!containsFocus(c.editor) && !c.editor->cursor_in_this && !c.editor->hoveringHoverbox(App::mouseX,App::mouseY)) {
            c.editor->completionbox->is_visible_layered=false;
            if (c.editor->hoverbox->parent == c.editor) App::RemoveWidgetFromParent(c.editor->hoverbox);
        }
    }
    scrollArea->t_x=x+pad; scrollArea->t_y=viewportTop; scrollArea->t_w=w-pad*2; scrollArea->t_h=viewportBottom-viewportTop+(horizontal ? TextRenderer::get_text_width(1)*2 : 0);
    scrollbar->position(x,viewportTop,w,viewportBottom-viewportTop);
    horizontalScrollbar->is_visible=horizontal;
    if (horizontal) horizontalScrollbar->position(x,viewportTop,w,viewportBottom-viewportTop);
    for (auto* widget : findWidgets) widget->is_visible=findOpen;
    if (findOpen) {
        const int findH=line*std::min(3,(int)findEdit->lines.size())+pad*2;
        const int replaceH=line*std::min(3,(int)replaceEdit->lines.size())+pad*2;
        const int replaceY=y+h-pad-replaceH, findY=replaceY-pad-findH;
        auto* prev=static_cast<Button*>(findWidgets[3]);
        auto* next=static_cast<Button*>(findWidgets[4]);
        auto* replace=static_cast<Button*>(findWidgets[5]);
        auto* all=static_cast<Button*>(findWidgets[6]);
        for (auto* b : {prev,next,replace,all}) b->position(x,y,w,h);
        all->t_x=x+w-pad-all->t_w; all->t_y=replaceY; all->t_h=replaceH;
        replace->t_x=all->t_x-pad-replace->t_w; replace->t_y=replaceY; replace->t_h=replaceH;
        next->t_x=x+w-pad-next->t_w; next->t_y=findY; next->t_h=findH;
        prev->t_x=next->t_x-pad-prev->t_w; prev->t_y=findY; prev->t_h=findH;
        caseSensitive->position(prev->t_x-pad-findH,findY,findH,findH);
        findEdit->position(x+pad,findY,std::max(1,caseSensitive->t_x-x-pad*2),findH);
        replaceEdit->position(x+pad,replaceY,std::max(1,replace->t_x-x-pad*2),replaceH);
    }
}

void JupyterEdit::render() {
    if (!is_visible) return;
    if (conflictReported) { conflictMenu->render(); return; }
    App::DrawRoundedRect(t_x,t_y,t_w,t_h,App::text_padding,App::theme.extras_background_color);
    for (auto* button : toolbar) button->render();
    int line=TextRenderer::get_text_height(), pad=App::text_padding;
    TextRenderer::draw_text(t_x+pad,viewportTop-line-pad,MST::toMonoString(status),App::theme.main_text_color);
    App::runWithSKIZ(t_x+pad,viewportTop,std::max(1,t_w-pad*2),std::max(1,viewportBottom-viewportTop),[&] {
        for (int i=0;i<(int)cells.size();++i) {
            auto& c=cells[i];
            if (!c.source->is_visible) continue;
            int cy=viewportTop+c.top-(int)scroll;
            std::string label=std::to_string(i+1)+"  "+c.data["cell_type"].get<std::string>();
            if (c.data["cell_type"] == "code") label+=" ["+(running==i ? std::string("*") : c.data.value("execution_count",json()).is_null() ? std::string(" ") : c.data["execution_count"].dump())+"]";
            TextRenderer::draw_text(t_x+pad*2,cy,MST::toMonoString(label),i==selected ? App::theme.main_text_color : App::theme.lesser_text_color);
            App::runWithSKIZ(c.source->t_x,c.source->t_y,c.source->t_w,c.source->t_h,[&] { c.source->render(); });
            for (auto* output : c.outputParts) if (output->is_visible)
                App::runWithSKIZ(output->t_x,output->t_y,output->t_w,output->t_h,[&] { output->render(); });
        }
        // LSP menus must overlay the following cells, rather than being clipped
        // to the source editor's (possibly one-line) rectangle.
        for (auto& cell : cells) if (cell.editor->is_visible) cell.editor->renderExtras();
        scrollbar->render();
    });
    if (horizontalScrollbar->is_visible) horizontalScrollbar->render();
    if (findOpen) for (auto* widget : findWidgets) {
        App::runWithSKIZ(widget->t_x,widget->t_y,widget->t_w,widget->t_h,[&] { widget->render(); });
    }
    App::DrawRoundBorder(t_x,t_y,t_w,t_h,containsFocus(this) ? App::theme.active_color : App::theme.border,5,pad);
}

bool JupyterEdit::on_key_event(int key,int scancode,int action,int mods) {
    // F5 is forwarded by App while it already holds this mutex. Other input
    // must serialize cell edits/deletion with LSP response dispatch and autosave.
    std::unique_lock<std::mutex> appGuard(App::canMakeChanges,std::defer_lock);
    if (key != GLFW_KEY_F5) appGuard.lock();
    std::lock_guard<std::recursive_mutex> guard(saveMutex);
    if (!is_visible) return false;
    if (conflictReported) return conflictMenu->on_key_event(key,scancode,action,mods);
    bool control=(mods & GLFW_MOD_CONTROL)!=0, shift=(mods & GLFW_MOD_SHIFT)!=0;
    const bool vim=App::settings->getValue("use_vim",false);
    auto* activeText=dynamic_cast<TextEdit*>(App::activeLeafNode);
    bool inCell=false;
    for (const auto& cell : cells) if (activeText && (activeText==cell.source ||
        std::find(cell.outputParts.begin(),cell.outputParts.end(),activeText)!=cell.outputParts.end())) { inCell=true; break; }
    if (findOpen && key == GLFW_KEY_ENTER && (activeText == findEdit || activeText == replaceEdit)) {
        // Ctrl+Enter belongs to the text field, including repeats. Keep it
        // away from the notebook's execution shortcuts below.
        if (control) return Widget::on_key_event(key,scancode,action,mods);
        if (action == GLFW_PRESS || action == GLFW_REPEAT) find(!shift,activeText == replaceEdit);
        return true;
    }
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_F && !(mods & GLFW_MOD_ALT) && (control || (vim && inCell && activeText->mode=='n'))) {
            if (activeText) {
                activeText->flushPendingCharInput();
                const auto selection=activeText->getSelectedText(activeText->cursors.front());
                if (selection.length) findEdit->setFullText(selection);
            }
            Cursor all;
            all.head_line=(int)findEdit->lines.size()-1;
            all.head_char=(int)findEdit->lines.back().line_text.length;
            all.preffered_collumn=all.head_char;
            findEdit->cursors={all};
            findEdit->mode='i'; findEdit->wasmode='n';
            replaceEdit->mode='i';
            findEdit->tryingToEnsureCursorPos=true;
            findOpen=true; App::setActiveLeafNode(findEdit); App::reclear=3; return true;
        }
        if (findOpen && key == GLFW_KEY_ESCAPE && (!vim || !activeText || activeText->mode=='n')) {
            findOpen=false; focusCell(); App::reclear=3; return true;
        }
        if (findOpen && shift && key == GLFW_KEY_TAB && (App::activeLeafNode == findEdit || App::activeLeafNode == replaceEdit)) {
            App::setActiveLeafNode(App::activeLeafNode == findEdit ? replaceEdit : findEdit); return true;
        }
        if (key == GLFW_KEY_F5 || (key == GLFW_KEY_ENTER && (control || shift))) {
            if (control && shift) runAll(); else runCell(control && key == GLFW_KEY_ENTER); return true;
        }
        if (control && mods & GLFW_MOD_ALT) {
            if (key == GLFW_KEY_UP || key == GLFW_KEY_DOWN) { moveCell(key == GLFW_KEY_UP ? -1 : 1); return true; }
        }
    }
    return Widget::on_key_event(key,scancode,action,mods);
}

bool JupyterEdit::on_char_event(unsigned int codepoint) {
    std::lock_guard<std::mutex> appGuard(App::canMakeChanges);
    std::lock_guard<std::recursive_mutex> guard(saveMutex);
    if (!is_visible) return false;
    if (conflictReported) return conflictMenu->on_char_event(codepoint);
    return Widget::on_char_event(codepoint);
}

bool JupyterEdit::on_mouse_button_event(int button,int action,int mods) {
    std::lock_guard<std::mutex> appGuard(App::canMakeChanges);
    std::lock_guard<std::recursive_mutex> guard(saveMutex);
    if (!is_visible) return false;
    if (conflictReported) return conflictMenu->on_mouse_button_event(button,action,mods);
    if (cursor_in_this && App::mouseY >= viewportTop && App::mouseY < viewportBottom) {
        for (auto& cell : cells) if (cell.editor->is_visible &&
            (cell.editor->hoveringHoverbox(App::mouseX,App::mouseY) || cell.editor->hoveringCompletionBox(App::mouseX,App::mouseY)))
            return cell.editor->on_mouse_button_event(button,action,mods);
    }
    if (action == GLFW_PRESS && cursor_in_this && App::mouseY >= viewportTop && App::mouseY < viewportBottom) {
        for (int i=0;i<(int)cells.size();++i) {
            auto& c=cells[i]; int y=viewportTop+c.top-(int)scroll;
            if (App::mouseY >= y && App::mouseY < y+c.height) { selected=i; App::setActiveLeafNode(c.source); break; }
        }
    }
    // TextEdit's legacy mouse handler also accepts hidden widgets. Route only
    // visible controls here so collapsed find fields and offscreen cells cannot
    // steal focus, and toolbar callbacks may safely change the cell list.
    for (auto* b : toolbar) if (b->is_visible && b->on_mouse_button_event(button,action,mods)) return true;
    if (findOpen) for (auto* w : findWidgets) if (w->on_mouse_button_event(button,action,mods)) return true;
    if (scrollbar->on_mouse_button_event(button,action,mods)) return true;
    if (horizontalScrollbar->is_visible && horizontalScrollbar->on_mouse_button_event(button,action,mods)) return true;
    for (auto& c : cells) {
        if (c.editor->is_visible && c.editor->on_mouse_button_event(button,action,mods)) return true;
        for (auto* output : c.outputParts) if (output->is_visible && output->on_mouse_button_event(button,action,mods)) return true;
    }
    return false;
}

bool JupyterEdit::on_mouse_move_event() {
    std::lock_guard<std::mutex> appGuard(App::canMakeChanges);
    std::lock_guard<std::recursive_mutex> guard(saveMutex);
    if (!is_visible) return false;
    if (conflictReported) return conflictMenu->on_mouse_move_event();
    scrollArea->on_mouse_move_event();
    for (auto& cell : cells) {
        cell.editor->on_mouse_move_event();
        for (auto* output : cell.outputParts) if (output->is_visible) output->on_mouse_move_event();
    }
    if (findOpen) for (auto* widget : findWidgets) widget->on_mouse_move_event();
    return false;
}

bool JupyterEdit::on_scroll_event(double x,double y) {
    std::lock_guard<std::mutex> appGuard(App::canMakeChanges);
    std::lock_guard<std::recursive_mutex> guard(saveMutex);
    if (!is_visible || !cursor_in_this) return false;
    if (conflictReported) return true;
    for (auto& c : cells) {
        if (c.editor->is_visible && c.editor->hoveringCompletionBox(App::mouseX,App::mouseY) && c.editor->completionbox->on_scroll_event(x,y)) return true;
        if (c.editor->is_visible && c.editor->hoveringHoverbox(App::mouseX,App::mouseY) && c.editor->hoverbox->on_scroll_event(x,y)) return true;
        for (auto* output : c.outputParts) if (auto* text=dynamic_cast<TextEdit*>(output))
            if ((x!=0 || text->lines.size()>16) && text->is_visible && text->on_scroll_event(x,y)) return true;
    }
    if (App::mouseY < viewportTop || App::mouseY >= viewportBottom) return false;
    if (glfwGetKey(App::window,GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(App::window,GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
        x+=y; y=0;
    }
    horizontalScroll+=x*TextRenderer::get_text_width(1)*6;
    horizontalScroll=std::clamp(horizontalScroll,0.0,(double)std::max(0,contentWidth-std::max(1,t_w-App::text_padding*5-12)));
    scroll+=y*TextRenderer::get_text_height()*3;
    scroll=std::clamp(scroll,0.0,(double)std::max(0,contentHeight-(viewportBottom-viewportTop)));
    App::reclear=3; return true;
}

void JupyterEdit::executeAction(WidgetActionType type) {
    Widget::executeAction(type); App::reclear=3;
}
