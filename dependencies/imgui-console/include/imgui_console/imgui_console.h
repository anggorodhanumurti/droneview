// Copyright (c) 2020 - present, Roland Munguia
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#ifndef IMGUI_CONSOLE_H
#define IMGUI_CONSOLE_H
#pragma once

#include "csys/system.h"
#include "imgui.h"
#include <array>
#include <functional>

struct ImGuiSettingsHandler;
class ImGuiConsole
{
public:

    /*!
     * \brief Construct an imgui console
     * \param c_name Name of the console
     * \param inputBufferSize Maximum input buffer size
     */
    explicit ImGuiConsole(std::string c_name = "imgui-console", size_t inputBufferSize = 256);

    /*!
     * \brief Render the Dear ImGui Console
     */
    void Draw();

    /*!
     * \brief Render the console contents inside an already active ImGui window.
     *        The caller owns Begin()/End(). The optional flags select the
     *        console menu bar and filter bar; the log and input field are
     *        always rendered.
     */
    void DrawContents(bool show_menu_bar = true, bool show_filter_bar = true);

    /*!
     * \brief Screen-space rectangle of the command input field from the last
     *        DrawContents() call. Returns false if it has not been drawn yet.
     */
    bool GetInputRect(ImVec2* min, ImVec2* max) const;

    /*!
     * \brief Render only the command input bar inside the active window.
     */
    void DrawInputBar();

    /*!
     * \brief Render only the scrolling log area. reserve_footer leaves space
     *        for an input bar drawn below the log.
     */
    void DrawLogWindow(bool reserve_footer = true, float fixed_height = 0.0f);

    /*!
     * \brief Focus and select the command input on the next frame.
     */
    void RequestInputFocus();

    /*!
     * \brief Deactivate the command input on the next frame while keeping it
     *        visible.
     */
    void DeactivateInput();

    /*!
     * \brief Optional handler that can consume an entered command line before
     *        it is passed to the csys command system.
     */
    using LineHandler = std::function<bool(const std::string&)>;
    void SetLineHandler(LineHandler handler);

    /*!
     * \brief Placeholder text shown inside the empty command input field.
     */
    void SetInputHint(std::string hint);

    /*!
     * \brief Console system which handles the console functionality (Logging, Commands, History, Scripts, etc).
     * \return System Obj
     */
    csys::System &System();

protected:

    // Console ////////////////////////////////////////////////////////////////

    csys::System m_ConsoleSystem;            //!< Main console system.
    size_t m_HistoryIndex;                   //!< Command history index.

    // Dear ImGui  ////////////////////////////////////////////////////////////

    // Main

    std::string m_Buffer;            //!< Input buffer.
    std::string m_ConsoleName;       //!< Console name string buffer.
    ImGuiTextFilter m_TextFilter;    //!< Logging filer.
    bool m_AutoScroll;               //!< Auto scroll flag.
    bool m_ColoredOutput;            //!< Colored output flag.
    bool m_ScrollToBottom;           //!< Scroll to bottom after is command is ran
    bool m_FilterBar;                //!< Filter bar flag.
    bool m_TimeStamps;                 //!< Display time stamps flag
    ImVec2 m_InputMin;                 //!< Last input field screen min
    ImVec2 m_InputMax;                 //!< Last input field screen max
    bool m_InputRectValid = false;     //!< Input field rect is available
    bool m_FocusInput = false;         //!< Focus input on the next frame
    bool m_DeactivateInput = false;    //!< Deactivate input on the next frame
    LineHandler m_LineHandler;         //!< Optional command-line consumer
    std::string m_InputHint;           //!< Placeholder text for the input field

    void InitIniSettings();             //!< Initialize Ini Settings handler
    void DefaultSettings();             //!< Restore console default settings
    void RegisterConsoleCommands();     //!< Register built-in console commands

    void MenuBar();                     //!< Console menu bar
    void FilterBar();                 //!< Console filter bar
    void InputBar();                 //!< Console input bar
    void LogWindow(bool reserve_footer = true, float fixed_height = 0.0f);  //!< Console log

    static void HelpMaker(const char *desc);

    // Window appearance.

    float m_WindowAlpha;             //!< Window transparency

    enum COLOR_PALETTE
    {
        // This four have to match the csys item type enum.

        COL_COMMAND = 0,    //!< Color for command logs
        COL_LOG,            //!< Color for in-command logs
        COL_WARNING,        //!< Color for warnings logs
        COL_ERROR,          //!< Color for error logs
        COL_INFO,            //!< Color for info logs

        COL_TIMESTAMP,      //!< Color for timestamps

        COL_COUNT            //!< For bookkeeping purposes
    };

    std::array<ImVec4, COL_COUNT> m_ColorPalette;                //!< Container for all available colors

    // ImGui Console Window.

    static int InputCallback(ImGuiInputTextCallbackData *data);    //!< Console input callback
    bool m_WasPrevFrameTabCompletion = false;                    //!< Flag to determine if previous input was a tab completion
    std::vector<std::string> m_CmdSuggestions;                    //!< Holds command suggestions from partial completion

    // Save data inside .ini

    bool m_LoadedFromIni = false;

    static void SettingsHandler_ClearALl(ImGuiContext *ctx, ImGuiSettingsHandler *handler);

    static void SettingsHandler_ReadInit(ImGuiContext *ctx, ImGuiSettingsHandler *handler);

    static void *SettingsHandler_ReadOpen(ImGuiContext *ctx, ImGuiSettingsHandler *handler, const char *name);

    static void SettingsHandler_ReadLine(ImGuiContext *ctx, ImGuiSettingsHandler *handler, void *entry, const char *line);

    static void SettingsHandler_ApplyAll(ImGuiContext *ctx, ImGuiSettingsHandler *handler);

    static void SettingsHandler_WriteAll(ImGuiContext *ctx, ImGuiSettingsHandler *handler, ImGuiTextBuffer *buf);

    ///////////////////////////////////////////////////////////////////////////
};

#endif //IMGUI_CONSOLE_H
