# UEFI Calculator

An interactive, bare-metal mathematical calculator for the UEFI environment, powered by EDK II and tinyexpr.

https://github.com/user-attachments/assets/2a6ef878-3dca-4023-9885-236fa91b8120

## ✨ Features

- **Expression Evaluation:** Integrated with the tinyexpr library to calculate complex mathematical expressions. Supports floating-point numbers, displaying results with up to 6 decimal places.
- **Visual Error Detection:** If a syntax error occurs in a mathematical expression, the shell visually points to the exact invalid character using a caret (`^`).
- **Advanced Text Editing:** Freely move the cursor across the input line using the `Left` and `Right` arrow keys. Full support for `Backspace` and `Delete` keys to remove characters anywhere in the string. Press `Ctrl+C` to instantly clear the current input line.
- **Color Theme:** Features a classic console interface (white text on a blue background), with critical and syntax errors highlighted in red for better visibility.
- **Command History:** A ring buffer implementation that stores up to 100 previous commands. Easily navigate through the history using the `Up` and `Down` arrow keys. To prevent clutter, consecutive identical commands are not saved to the history.
- **Built-in Utilities:** Supports basic shell commands such as `clear` (clears the screen) and `exit` (closes the application).

---

## 🛠️ Building

### Prerequisites
Before you begin, ensure you have the following installed and configured:
1. [EDK II](https://github.com/tianocore/edk2)
2. [EDK II LibC (EADK)](https://github.com/tianocore/edk2-libc)
3. GCC 14+
4. `mkisofs`

### Instructions

1. Configurate an EDK II enviroment with PACKAGES_PATH that includes PATH_TO_UEFI_CALCULATOR
2. Build the project with build -a YOUR_ARCH -t GCC -p UefiCalculator.dsc
3. Copy output UefiCalculator.efi to UefiCalculator directory
4. Build the shell with build -a YOUR_ARCH -p ShellPkg/ShellPkg.dsc
5. Copy output shell .efi file to UefiCalculator directory.
6. Run build_iso.sh file
7. You're done!
