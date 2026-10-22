#pragma once
#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "ChromeCaptionSaver/core/Types.h"
#include <algorithm>
#include <cfloat>
#include <cctype>

// Keep an active reader's snapshot fixed so arriving captions cannot destroy their selection.
struct SelectableTextView {
    std::string text, source;
    bool select_all = false;
    int scroll_frames = 0;
    float width = 0, font_size = 0;
    int selection_start = 0, selection_end = 0;
    bool held=false, search_pending=false;
    std::string search_query,search_status;
    size_t search_from=0;
    void Resume(){held=false;search_pending=false;scroll_frames=2;ImGui::ClearActiveID();}
    void Find(const std::string& query){if(search_query!=query)search_from=0;search_query=query;search_pending=true;held=true;}
    static std::string Wrap(const std::string& input, float max_width) {
        std::string result;
        float line = 0;
        for (size_t i = 0; i < input.size();) {
            size_t end = i + 1;
            while (end < input.size() && (static_cast<unsigned char>(input[end]) & 0xc0) == 0x80) ++end;
            if (input[i] == '\n') { result += '\n'; line = 0; i = end; continue; }
            const float advance = ImGui::CalcTextSize(input.data() + i, input.data() + end).x;
            if (line + advance > max_width && line > 0) { result += '\n'; line = 0; }
            result.append(input, i, end - i); line += advance; i = end;
        }
        return result;
    }
    bool Draw(const char* id, const std::string& incoming, ImVec2 size, bool follow = true) {
        const bool reading = ImGui::GetActiveID() == ImGui::GetID(id);
        const float next_width = std::max(80.0f, (size.x > 0 ? size.x : ImGui::GetContentRegionAvail().x) - 32.0f);
        if ((!reading && !held || select_all || search_pending) && (source != incoming || width != next_width || font_size != ImGui::GetFontSize())) {
            source = incoming; width = next_width; font_size = ImGui::GetFontSize(); text = Wrap(incoming, next_width); scroll_frames = follow ? 2 : 0;
        }
        if (select_all || search_pending) ImGui::SetKeyboardFocusHere();
        if (!reading && scroll_frames > 0) { ImGui::SetNextWindowScroll(ImVec2(-1, FLT_MAX)); --scroll_frames; }
        auto callback = [](ImGuiInputTextCallbackData* data) {
            auto& self = *static_cast<SelectableTextView*>(data->UserData);
            if(self.search_pending){
                self.search_pending=false;
                auto fold=[](std::string value){for(char& c:value)if(static_cast<unsigned char>(c)<128)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;};
                const auto haystack=fold(self.source),needle=fold(self.search_query);
                const auto at=needle.empty()?std::string::npos:haystack.find(needle,self.search_from);
                auto found=at==std::string::npos&&!needle.empty()?haystack.find(needle):at;
                if(found!=std::string::npos){
                    auto mapped=[&](size_t offset){size_t original=0,display=0;while(original<offset&&display<self.text.size()){if(self.text[display]=='\n'&&(original>=self.source.size()||self.source[original]!='\n')){++display;continue;}++original;++display;}return static_cast<int>(display);};
                    data->SelectionStart=mapped(found);data->SelectionEnd=mapped(found+self.search_query.size());data->CursorPos=data->SelectionEnd;
                    self.search_from=found+self.search_query.size();self.search_status="已定位";
                    if(auto* state=ImGui::GetInputTextState(ImGui::GetActiveID()))state->CursorFollow=true;
                }else self.search_status="未找到";
            }
            if (self.select_all) { data->SelectionStart = 0; data->SelectionEnd = data->BufTextLen; data->CursorPos = data->BufTextLen; self.select_all = false; }
            self.selection_start = data->SelectionStart; self.selection_end = data->SelectionEnd;
            return 0;
        };
        ImGui::InputTextMultiline(id, &text, size, ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_CallbackAlways, callback, this);
        if(ImGui::IsItemActive() || (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel!=0))held=true;
        return ImGui::IsItemActive();
    }
};
inline std::string TranscriptText(const std::vector<ccs::core::CaptionSegment>& segments) {
    std::string text;
    for (const auto& segment : segments) { if (!text.empty()) text += '\n'; text += segment.text_utf8; }
    return text;
}
inline void DrawTranscriptList(const std::vector<ccs::core::CaptionSegment>& segments, bool follow, float scale, SelectableTextView& reader) {
    reader.Draw("##transcript", TranscriptText(segments), ImVec2(-1, -110 * scale), follow);
}
