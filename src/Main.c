#include "Utils.h"
#include "LogUtils.h"

#include <Protocol/SimpleTextIn.h>
#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/ShellCEntryLib.h>

#include <Uefi/UefiBaseType.h>
#include <tinyexpr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int snprintf(char* str, unsigned long size, const char* format, ...);

static constexpr int MAX_BUFFER_SIZE = 256;
static constexpr int MAX_HISTORY_SIZE = 100;
static constexpr int MAX_DOUBLE_BUFFER_SIZE = 64;
static UINTN g_terminalCols = 0, g_terminalRows = 0;
;

static constexpr char EMPTY_BUFFER[MAX_BUFFER_SIZE] = {[0 ... MAX_BUFFER_SIZE - 2] = ' ', [MAX_BUFFER_SIZE - 1] = '\0'};
static constexpr int PRECISION = 6;

typedef struct {
    char buffer[MAX_BUFFER_SIZE];
    int index; // Current cursor position in the buffer
    int count; // Length of the text in the buffer

    char history[MAX_HISTORY_SIZE][MAX_BUFFER_SIZE];
    int writeIndex;   // Index for writing the next history entry
    int viewIndex;    // Index of the currently viewed history entry
    int historyCount; // Total number of items in history (0 to MAX)

    bool needsRedraw;
    int promptRow;
} TerminalState;

static EFI_STATUS init(TerminalState* state);
static void handleCommandKeys(EFI_INPUT_KEY key, TerminalState* state);
static int handleTextKeys(EFI_INPUT_KEY key, TerminalState* state);
static void handleRes(double result, int errorPlace);

static int getIndexBackwards(int viewIndex, int writeIndex, int count);
static int getIndexForwards(int viewIndex, int writeIndex, int count);
static void addToTheHistoryLastCommand(TerminalState* state);

static EFI_STATUS printError(const CHAR16* str);
static bool equalsIgnoreCase(const char* a, const char* b);

int main(int Argc, char** Argv) {
    TerminalState state = {0};
    init(&state);

    state.promptRow = gST->ConOut->Mode->CursorRow;
    Print(L"> ");
    EFI_INPUT_KEY key = {0};
    UINTN eventIndex = 0;
    while (true) {
        gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &eventIndex);
        EFI_STATUS status = gST->ConIn->ReadKeyStroke(gST->ConIn, &key);
        if (status != EFI_SUCCESS) continue;

        if (key.ScanCode != 0)
            handleCommandKeys(key, &state);
        else if (key.UnicodeChar != 0)
            if (handleTextKeys(key, &state) == 1) return 0; // user typed 'exit'

        if (state.needsRedraw) {
            CHECK_FOR_ERROR(gST->ConOut->EnableCursor(gST->ConOut, false));
            CHECK_FOR_ERROR(gST->ConOut->SetCursorPosition(gST->ConOut, 0, state.promptRow));
            Print(L"> %a%a", state.buffer, EMPTY_BUFFER);
            CHECK_FOR_ERROR(gST->ConOut->SetCursorPosition(gST->ConOut, (state.index + 2) % g_terminalCols,
                                                           state.promptRow + (state.index + 2) / g_terminalCols));
            CHECK_FOR_ERROR(gST->ConOut->EnableCursor(gST->ConOut, true));
            state.needsRedraw = false;
        }
    }

    return 0;
}

static EFI_STATUS init(TerminalState* state) {
    TRACE_FUNCTION();
    CHECK_FOR_ERROR(gBS->SetWatchdogTimer(600, 0x0000, 0, NULL)); // 10 min
    CHECK_FOR_ERROR(gST->ConOut->SetAttribute(gST->ConOut, EFI_TEXT_ATTR(EFI_WHITE, EFI_BLUE)));
    CHECK_FOR_ERROR(gST->ConIn->Reset(gST->ConIn, false)); // input reset
    CHECK_FOR_ERROR(gST->ConOut->EnableCursor(gST->ConOut, true));
    CHECK_FOR_ERROR(gST->ConOut->ClearScreen(gST->ConOut));

    state->buffer[0] = '\0';
    for (INTN i = 0; i < MAX_HISTORY_SIZE; i++) state->history[i][0] = '\0';

    CHECK_FOR_ERROR(gST->ConOut->QueryMode(gST->ConOut, gST->ConOut->Mode->Mode, &g_terminalCols, &g_terminalRows));
    return EFI_SUCCESS;
}

// Handle left, right, up, down, delete
static void handleCommandKeys(EFI_INPUT_KEY key, TerminalState* state) {
    TRACE_FUNCTION();
    if (key.ScanCode == 0x01 && (state->viewIndex != state->writeIndex ||
                                 state->count == 0)) { // UP checks if user in a view mode and go to the past command if he's
        state->viewIndex = getIndexBackwards(state->viewIndex, state->writeIndex, state->historyCount);
        strcpy(state->buffer, state->history[state->viewIndex]);
        int strSize = strlen(state->buffer);
        state->count = strSize;
        state->index = strSize;
        state->needsRedraw = true;
    } else if (key.ScanCode == 0x02) { // DOWN checks if current viewindex isn't at the end and copy to the current buffer
        if (state->viewIndex != state->writeIndex) {
            state->viewIndex = getIndexForwards(state->viewIndex, state->writeIndex, state->historyCount);
            strcpy(state->buffer, state->history[state->viewIndex]);
            int strSize = strlen(state->buffer);
            state->count = strSize;
            state->index = strSize;
            state->needsRedraw = true;
        }
    } else if (key.ScanCode == 0x04) { // LEFT CHANGES INDEX TO [0
        if (state->index > 0) {
            state->index--;
            state->needsRedraw = true;
            state->viewIndex = state->writeIndex;
        }
    } else if (key.ScanCode == 0x03) { // RIGHT CHANGES INDEX TO count-1]
        if (state->index < state->count) {
            state->index++;
            state->needsRedraw = true;
            state->viewIndex = state->writeIndex;
        }
    } else if (key.ScanCode == 0x08) { // DELETE current char; shifts buffer to the left with \0
        if (state->index < state->count) {
            for (INTN i = state->index; i < state->count; i++) // 1 2
                state->buffer[i] = state->buffer[i + 1];
            state->count--;
            state->needsRedraw = true;
            state->viewIndex = state->writeIndex;
        }
    }
}

// Handles printable characters, Backspace, Enter, and Ctrl+C
static int handleTextKeys(EFI_INPUT_KEY key, TerminalState* state) {
    TRACE_FUNCTION();
    if (key.UnicodeChar == 0x08) { // Backspace: Delete the character to the left of the cursor.
        if (state->index > 0) {
            for (INTN i = state->index - 1; i < state->count; i++) // 1 2
                state->buffer[i] = state->buffer[i + 1];
            state->count--;
            state->index--;
            state->needsRedraw = true;
        }
    } else if (key.UnicodeChar == 0x0D) { // Enter: Process the command in the buffer.
        Print(L"\r\n");
        // Handle built-in commands or evaluate the expression.
        if (equalsIgnoreCase(state->buffer, "exit"))
            return 1;
        else if (equalsIgnoreCase(state->buffer, "clear")) {
            gST->ConOut->ClearScreen(gST->ConOut);
        } else if (state->count > 0) {
            int errorPlace = 0;
            double result = te_interp(state->buffer, &errorPlace);
            handleRes(result, errorPlace);
        }
        // If the command is not empty, add it to history.
        if (state->count > 0) addToTheHistoryLastCommand(state);
        // Reset for the next command.
        state->count = 0;
        state->index = 0;
        state->buffer[0] = '\0';
        Print(L"> ");
        state->promptRow = gST->ConOut->Mode->CursorRow;
    } else if (key.UnicodeChar == 0x0003) { // ctrl+c clears current buffer
        state->count = 0;
        state->index = 0;
        state->buffer[0] = '\0';
        state->needsRedraw = true;
    } else if (state->count < MAX_BUFFER_SIZE - 1) {       // NORMAL CHAR ...;MAX_BUFFER_SIZE - 1] adds char into the buffer
        for (INTN i = state->count; i > state->index; i--) // 2 1
            state->buffer[i] = state->buffer[i - 1];
        state->buffer[state->index] = (char)key.UnicodeChar;
        state->buffer[++(state->count)] = '\0';
        state->index++;
        state->needsRedraw = true;
    }
    state->viewIndex = state->writeIndex;
    return 0;
}

// Prints calculating result of an expression or points out to the error place
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void handleRes(double result, int errorPlace) {
    TRACE_FUNCTION();
    if (errorPlace == 0) {
        char doubleBuffer[MAX_DOUBLE_BUFFER_SIZE];
        snprintf(doubleBuffer, sizeof(doubleBuffer), "%.*f", PRECISION, result); // NOLINT
        Print(L"Result: %a", doubleBuffer);
        Print(L"\r\n");
    } else {
        Print(L"  "); // indent with char size == "> "
        if (errorPlace > 0 && errorPlace <= MAX_BUFFER_SIZE)
            for (INTN i = 0; i < errorPlace - 1; i++) Print(L" ");
        Print(L"^\r\nError in your expression!\r\n");
    }
}

// Function for scrolling up (to the older commands). Ring buffer. Limited with history size.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int getIndexBackwards(int viewIndex, int writeIndex, int count) {
    TRACE_FUNCTION();
    if (count == 0) return viewIndex; // History is empty
    int oldestIndex = (count == MAX_HISTORY_SIZE) ? writeIndex : 0;

    if (viewIndex == oldestIndex) return viewIndex;

    return (viewIndex - 1 + MAX_HISTORY_SIZE) % MAX_HISTORY_SIZE;
}

// Function for scrolling down (to the newer commands). Ring buffer. Limited with history size.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int getIndexForwards(int viewIndex, int writeIndex, int count) {
    TRACE_FUNCTION();
    if (count == 0) return viewIndex; // History is empty

    return (viewIndex + 1) % MAX_HISTORY_SIZE;
}

// Checks if last command in history isn't the same as current command and adds command to the history
static void addToTheHistoryLastCommand(TerminalState* state) {
    TRACE_FUNCTION();
    if (state->historyCount == 0) { // History is empty
        strcpy(state->history[state->writeIndex], state->buffer);
        state->historyCount++;
        state->writeIndex = (state->writeIndex + 1) % MAX_HISTORY_SIZE;
        return;
    }

    int lastIndex = (state->writeIndex - 1 + MAX_HISTORY_SIZE) % MAX_HISTORY_SIZE;

    // If current command isn't the same as last command, add the command to the history
    if (!equalsIgnoreCase(state->buffer, state->history[lastIndex])) {
        strcpy(state->history[state->writeIndex], state->buffer);

        state->writeIndex = (state->writeIndex + 1) % MAX_HISTORY_SIZE;
        if (state->historyCount < MAX_HISTORY_SIZE) state->historyCount++;
    }
}

static EFI_STATUS printError(const CHAR16* str) {
    TRACE_FUNCTION();
    CHECK_FOR_ERROR(gST->ConOut->SetAttribute(gST->ConOut, EFI_TEXT_ATTR(EFI_RED, EFI_BLUE)));
    Print(L"%s\r\n", str);
    CHECK_FOR_ERROR(gST->ConOut->SetAttribute(gST->ConOut, EFI_TEXT_ATTR(EFI_WHITE, EFI_BLUE)));
    return EFI_SUCCESS;
}

static bool equalsIgnoreCase(const char* a, const char* b) {
    TRACE_FUNCTION();
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;

        if (ca != cb) return false;
        a++;
        b++;
    }
    return *a == *b;
}
