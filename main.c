#include <Library/ShellCEntryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Uefi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "libs/tinyexpr.h"


static constexpr int MAX_BUFFER_SIZE = 256;
static constexpr int MAX_HISTORY_SIZE = 100;
static constexpr int MAX_DOUBLE_BUFFER_SIZE = 64;
static UINTN TERMINAL_ROWS = 0;
static UINTN TERMINAL_COLUMNS = 0;

static constexpr char EMPTY_BUFFER[MAX_BUFFER_SIZE] = {
    [0 ... MAX_BUFFER_SIZE - 2] = ' ',
    [MAX_BUFFER_SIZE - 1] = '\0'
};
static constexpr int PRECISION = 6;

typedef struct {
    char buffer[MAX_BUFFER_SIZE];
    int index;  // Current cursor position in the buffer
    int count;  // Length of the text in the buffer

    char history[MAX_HISTORY_SIZE][MAX_BUFFER_SIZE];
    int writeIndex;    // Index for writing the next history entry
    int viewIndex;     // Index of the currently viewed history entry
    int historyCount;  // Total number of items in history (0 to MAX)

    bool needsRedraw;
    int promptRow;
} TerminalState;


extern void init(TerminalState* state);
extern void handleCommandKeys(EFI_INPUT_KEY key, TerminalState* state);
extern void handleTextKeys(EFI_INPUT_KEY key, TerminalState* state);
extern void handleRes(double result, int errorPlace);

extern int getIndexBackwards(int viewIndex, int writeIndex, int count);
extern int getIndexForwards(int viewIndex, int writeIndex, int count);
extern void addToTheHistoryLastCommand(TerminalState* state);

extern void exitWithWaiting(int exitCode);

extern void printError(const CHAR16* str);
extern bool equalsIgnoreCase(const char* a, const char* b);

int main(int Argc, char **Argv) {
    TerminalState state = {0};
    init(&state);    
  
    state.promptRow = gST->ConOut->Mode->CursorRow;
    Print(L"> ");
    EFI_INPUT_KEY key = {0};
    UINTN eventIndex = 0;
    while(true) {
        gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &eventIndex);
        EFI_STATUS status = gST->ConIn->ReadKeyStroke(gST->ConIn, &key);
        if (status != EFI_SUCCESS) continue;

        if (key.ScanCode != 0) {
            handleCommandKeys(key, &state);
        } else if (key.UnicodeChar != 0) {
            handleTextKeys(key, &state);
        }
        
        if(state.needsRedraw) {
            gST->ConOut->EnableCursor(gST->ConOut, false);
            gST->ConOut->SetCursorPosition(gST->ConOut, 0, state.promptRow);
            Print(L"> %a%a", state.buffer, EMPTY_BUFFER);
            gST->ConOut->SetCursorPosition(gST->ConOut, (state.index + 2) % TERMINAL_COLUMNS, state.promptRow + (state.index + 2) / TERMINAL_COLUMNS);
            gST->ConOut->EnableCursor(gST->ConOut, true);
            state.needsRedraw = false;
        }
    }

    return 0;
}

static void init(TerminalState* state){
    gBS->SetWatchdogTimer(600, 0x0000, 0, NULL);  // 10 min
    gST->ConOut->SetAttribute(gST->ConOut, EFI_TEXT_ATTR(EFI_WHITE, EFI_BLUE));
    gST->ConIn->Reset(gST->ConIn, false);  // input reset
    gST->ConOut->EnableCursor(gST->ConOut, true);
    gST->ConOut->ClearScreen(gST->ConOut);

    state->buffer[0] = '\0';
    for(int i = 0; i < MAX_HISTORY_SIZE; i++) state->history[i][0] = '\0';

    EFI_STATUS status = gST->ConOut->QueryMode(gST->ConOut, gST->ConOut->Mode->Mode, &TERMINAL_COLUMNS, &TERMINAL_ROWS);
    if(status != EFI_SUCCESS){ 
        printError(L"Error: Could not determine screen size in characters.");
        exitWithWaiting(1);
    }
}

// Handle left, right, up, down, delete
static void handleCommandKeys(EFI_INPUT_KEY key, TerminalState* state){
    if (key.ScanCode == 0x01 && (state->viewIndex != state->writeIndex || state->count == 0)) {  //UP checks if user in a view mode and go to the past command if he's
        state->viewIndex = getIndexBackwards(state->viewIndex, state->writeIndex, state->historyCount);
        strcpy(state->buffer, state->history[state->viewIndex]);
        int strSize = strlen(state->buffer);
        state->count = strSize;
        state->index = strSize;
        state->needsRedraw = true;
    } else if (key.ScanCode == 0x02) {  //DOWN checks if current viewindex isn't at the end and copy to the current buffer 
        if(state->viewIndex != state->writeIndex){
            state->viewIndex = getIndexForwards(state->viewIndex, state->writeIndex, state->historyCount);
            strcpy(state->buffer, state->history[state->viewIndex]);
            int strSize = strlen(state->buffer);
            state->count = strSize;
            state->index = strSize;
            state->needsRedraw = true;
        }
    } else if (key.ScanCode == 0x04) {  // LEFT CHANGES INDEX TO [0
        if (state->index > 0) {
            state->index--;
            state->needsRedraw = true;
            state->viewIndex = state->writeIndex;
        }
    } else if (key.ScanCode == 0x03) {  // RIGHT CHANGES INDEX TO count-1]
        if (state->index < state->count) {
            state->index++;
            state->needsRedraw = true;
            state->viewIndex = state->writeIndex;
        }
    } else if (key.ScanCode == 0x08) {  // DELETE current char; shifts buffer to the left with \0
        if (state->index < state->count) {
            for (int i = state->index; i < state->count; i++) {  // 1 2
                state->buffer[i] = state->buffer[i + 1];
            }
            state->count--;
            state->needsRedraw = true;
            state->viewIndex = state->writeIndex;
        }
    }
}

// Handles printable characters, Backspace, Enter, and Ctrl+C
static void handleTextKeys(EFI_INPUT_KEY key, TerminalState* state){
    if (key.UnicodeChar == 0x08) {  // Backspace: Delete the character to the left of the cursor.
        if (state->index > 0) {
            for (int i = state->index - 1; i < state->count; i++) {  // 1 2
                state->buffer[i] = state->buffer[i + 1];
            }
            state->count--;
            state->index--;
            state->needsRedraw = true;
        }
    } else if (key.UnicodeChar == 0x0D) {  // Enter: Process the command in the buffer.
        Print(L"\r\n");
        // Handle built-in commands or evaluate the expression.
        if(equalsIgnoreCase(state->buffer, "exit")) exit(0);
        else if(equalsIgnoreCase(state->buffer, "clear")){
            gST->ConOut->ClearScreen(gST->ConOut);
        } else if(state->count > 0){
            int errorPlace = 0;
            double result = te_interp(state->buffer, &errorPlace);
            handleRes(result, errorPlace);
        }
        // If the command is not empty, add it to history.
        if(state->count > 0){
            addToTheHistoryLastCommand(state);
        }
        // Reset for the next command.
        state->count = 0;
        state->index = 0;
        state->buffer[0] = '\0';
        Print(L"> ");
        state->promptRow = gST->ConOut->Mode->CursorRow;
    } else if(key.UnicodeChar == 0x0003){//ctrl+c clears current buffer
        state->count = 0;
        state->index = 0;
        state->buffer[0] = '\0';
        state->needsRedraw = true;
    } else if (state->count < MAX_BUFFER_SIZE - 1) {  // NORMAL CHAR ...;MAX_BUFFER_SIZE - 1] adds char into the buffer
        for (int i = state->count; i > state->index; i--) {//2 1
            state->buffer[i] = state->buffer[i - 1];
        }
        state->buffer[state->index] = key.UnicodeChar;
        state->buffer[++(state->count)] = '\0';
        state->index++;
        state->needsRedraw = true;
    }
    state->viewIndex = state->writeIndex;
}


// Prints calculating result of an expression or points out to the error place
static void handleRes(double result, int errorPlace) {
    if (errorPlace == 0) {
        char doubleBuffer[MAX_DOUBLE_BUFFER_SIZE];
        snprintf(doubleBuffer, sizeof(doubleBuffer), "%.*f", PRECISION, result);
        Print(L"Result: %a", doubleBuffer);
        Print(L"\r\n");
    } else {
        Print(L"  ");  // indent with char size == "> "
        if (errorPlace > 0 && errorPlace <= MAX_BUFFER_SIZE) {
            for (int i = 0; i < errorPlace - 1; i++) {
                Print(L" ");
            }
        }
        Print(L"^\r\nError in your expression!\r\n");
    }
}

// Function for scrolling up (to the older commands). Ring buffer. Limited with history size.
static int getIndexBackwards(int viewIndex, int writeIndex, int count) {
    if (count == 0) return viewIndex;  // History is empty
    int oldestIndex = (count == MAX_HISTORY_SIZE) ? writeIndex : 0;
    
    if (viewIndex == oldestIndex) {
        return viewIndex;
    }
    
    return (viewIndex - 1 + MAX_HISTORY_SIZE) % MAX_HISTORY_SIZE;
}

// Function for scrolling down (to the newer commands). Ring buffer. Limited with history size.
static int getIndexForwards(int viewIndex, int writeIndex, int count) {
    if (count == 0) return viewIndex;  // History is empty
    
    return (viewIndex + 1) % MAX_HISTORY_SIZE;
}

// Checks if last command in history isn't the same as current command and adds command to the history
static void addToTheHistoryLastCommand(TerminalState* state) {
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
        if (state->historyCount < MAX_HISTORY_SIZE) {
            state->historyCount++;
        }
    }
}

static void exitWithWaiting(int exitCode){
    Print(L"Press any key to exit the app\r\n");
    EFI_INPUT_KEY key = {0};
    UINTN eventIndex = 0;
    
    while(true){
        gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &eventIndex);
        EFI_STATUS status = gST->ConIn->ReadKeyStroke(gST->ConIn, &key);
        if (status != EFI_SUCCESS) continue;
        if (key.ScanCode != 0 || key.UnicodeChar != 0) {
            gST->ConOut->ClearScreen(gST->ConOut);
            exit(exitCode);
        }
    }
}

static void printError(const CHAR16* str){
    gST->ConOut->SetAttribute(gST->ConOut, EFI_TEXT_ATTR(EFI_RED, EFI_BLUE));
    Print(L"%s\r\n", str);
    gST->ConOut->SetAttribute(gST->ConOut, EFI_TEXT_ATTR(EFI_WHITE, EFI_BLUE));
}

static bool equalsIgnoreCase(const char* a, const char* b) {
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;

        if (ca != cb) return false;
        a++;
        b++;
    }
    return *a == *b;
}
