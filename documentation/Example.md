# Xundo: Your Undo/Redo Playground in C++

**Date**: March 17, 2025  
**Purpose**: A thread-safe undo/redo system—built for fun, learning, and hacking.

Welcome to Xundo—a C++ undo/redo system that’s all about keeping your moves reversible, whether you’re doodling in memory or saving to disk. It’s got 4 threads, a slick history, and a vibe that’s easy to jump into. Let’s get you rocking it step-by-step—grab your code editor, and let’s roll!

## What’s Xundo?
Think of Xundo as your time machine for code actions—undo, redo, save, load, all in a neat package. It’s got:
- **Commands**: Do stuff, undo stuff—your rules.
- **History**: Tracks every move, in memory or on disk.
- **Threads**: 4 workers handle I/O—fast and smooth.
- **Flex**: Use it with or without files—your call.

## Step-by-Step: Make It Yours

### Step 1: Grab the Code
You’ve got the source—drop it in your project. It needs:
- C++17 (for `std::filesystem`, `std::string_view`).
- `xcommandline_parser.h` (dependency)— snag it from wherever you stash your libs.

```cpp
#include "xundo.h" // Or wherever you put it
```

### Step 2: Set Up Your Playground
Let’s make a simple `Point` class to move around—our toy database.

```cpp
struct Point {
    int m_X = 0, m_Y = 0;
};
```

### Step 3: Create a Command
Define a `MovePoint` command—tells Xundo how to move, undo, and backup your `Point`.

```cpp
struct MovePoint : xundo::command_base {
    MovePoint(xundo::system& System, void* DataBase) noexcept
        : command_base(System, "MovePoint", DataBase) {
        RegisterArguments();
    }

    struct Data { int X, Y; };

    const char* getCommandHelp() const noexcept override {
        return "Move the point to X,Y. Usage: MovePoint -T X Y";
    }

    void RegisterArguments() noexcept override {
        m_hToPos = m_Parser.addOption("T", "Target X,Y", true, 2);
    }

    std::string Redo() noexcept override {
        if (m_Parser.hasOption(m_hToPos)) {
            auto X = m_Parser.getOptionArgAs<int64_t>(m_hToPos, 0);
            auto Y = m_Parser.getOptionArgAs<int64_t>(m_hToPos, 1);
            if (std::holds_alternative<xcommandline::parser::error>(X))
                return "Bad X value!";
            if (std::holds_alternative<xcommandline::parser::error>(Y))
                return "Bad Y value!";
            auto& P = get<Point>();
            P.m_X = static_cast<int>(std::get<int64_t>(X));
            P.m_Y = static_cast<int>(std::get<int64_t>(Y));
            return {};
        }
        return "Need -T X Y!";
    }

    void Undo(undo_file& File) noexcept override {
        Data OldPos{0, 0};
        File.Read(OldPos);
        auto& P = get<Point>();
        P.m_X = OldPos.X;
        P.m_Y = OldPos.Y;
    }

    void BackupCurrenState(undo_file& File) noexcept override {
        auto& P = get<Point>();
        Data OldPos{P.m_X, P.m_Y};
        File.Write(OldPos);
    }

    xcommandline::parser::handle m_hToPos;
};
```

### Step 4: Spin Up the System
Create a `system` instance—decide if you want disk action or just memory fun.

```cpp
int main() {
    Point MyPoint; // Our little playground
    xundo::system UndoSys;

    // Init with disk path (or skip for memory-only)
    if (auto Err = UndoSys.Init("xundo_data", true); !Err.empty()) {
        std::cerr << "Init failed: " << Err << "\n";
        return 1;
    }

    // Hook up the command
    MovePoint Mover(UndoSys, &MyPoint);
    return 0;
}
```

- **Disk Mode**: `"xundo_data"`—saves to "xundo_data/UndoStep-*" files, auto-loads prior history if there.
- **Memory Mode**: `""`—no files, pure in-memory undo/redo.

### Step 5: Make Some Moves
Let’s move `MyPoint` around—execute commands like a boss.

```cpp
    // Move to (10, 20)
    if (auto Err = Mover.Move(10, 20); !Err.empty()) {
        std::cerr << "Move failed: " << Err << "\n";
    }
    std::cout << "X=" << MyPoint.m_X << ", Y=" << MyPoint.m_Y << "\n"; // X=10, Y=20

    // Move to (30, 40)
    Mover.Move(30, 40);
    std::cout << "X=" << MyPoint.m_X << ", Y=" << MyPoint.m_Y << "\n"; // X=30, Y=40
```

### Step 6: Undo & Redo Like a Time Traveler
Back it up, then bring it forward—Xundo’s got you.

```cpp
    UndoSys.Undo(); // Back to (10, 20)
    std::cout << "After undo: X=" << MyPoint.m_X << ", Y=" << MyPoint.m_Y << "\n";

    UndoSys.Redo(); // Forward to (30, 40)
    std::cout << "After redo: X=" << MyPoint.m_X << ", Y=" << MyPoint.m_Y << "\n";
```

### Step 7: Peek at History
See where you’ve been—`displayHistory()` spills the tea.

```cpp
    UndoSys.displayHistory();
    // Sample output:
    // History:
    //   [0000]-[U] User:1 Time:1742142422507000 MovePoint -T 10 20 [Cached]
    //   [0001]-[U] User:1 Time:1742142422507001 MovePoint -T 30 40 [Cached]
    // Current Index: 2
```

### Step 8: Suggest a Move
Get a hint for what’s next—`SuggestNext()` has ideas.

```cpp
    std::cout << "Next move? " << UndoSys.SuggestNext(1) << "\n";
    // Next move? -MovePoint -T 40 50
```

### Step 9: Save & Reload (Disk Mode Only)
If you used a path, Xundo saves on exit—restart and pick up where you left off.

```cpp
    // Exit—saves to "xundo_data/UndoTimestamps.bin" and "UndoStep-*" files
    return 0;
}
```

Run again with `Init("xundo_data", true)`—it’ll reload your moves!

## Tips & Tricks
- **No Disk?**: Skip `UndoPath`—`Init()` skips I/O, pure memory fun.
- **Custom Commands**: Make your own—inherit `command_base`, tweak `Redo/Undo`.
- **Thread Vibes**: 4 workers hum along—watch `SynJobQueue()` sync them up.
- **Error Check**: `[[nodiscard]]` returns—don’t ignore those strings!

## Full Example
Here’s it all together—copy, paste, play!

```cpp
#include "xundo.h"
#include <iostream>

struct Point { int m_X = 0, m_Y = 0; };

struct MovePoint : xundo::command_base {
    MovePoint(xundo::system& System, void* DataBase) noexcept : command_base(System, "MovePoint", DataBase) {
        RegisterArguments();
    }
    struct Data { int X, Y; };
    const char* getCommandHelp() const noexcept override { return "Move the point in abs coordinates"; }
    void RegisterArguments() noexcept override { m_hToPos = m_Parser.addOption("T", "Target X,Y", true, 2); }
    std::string Redo() noexcept override {
        if (m_Parser.hasOption(m_hToPos)) {
            auto X = m_Parser.getOptionArgAs<int64_t>(m_hToPos, 0);
            auto Y = m_Parser.getOptionArgAs<int64_t>(m_hToPos, 1);
            if (std::holds_alternative<xcommandline::parser::error>(X)) return "Bad X!";
            if (std::holds_alternative<xcommandline::parser::error>(Y)) return "Bad Y!";
            auto& P = get<Point>();
            P.m_X = static_cast<int>(std::get<int64_t>(X));
            P.m_Y = static_cast<int>(std::get<int64_t>(Y));
            return {};
        }
        return "Need -T X Y!";
    }
    void Undo(undo_file& File) noexcept override {
        Data OldPos{0, 0};
        File.Read(OldPos);
        auto& P = get<Point>();
        P.m_X = OldPos.X;
        P.m_Y = OldPos.Y;
    }
    void BackupCurrenState(undo_file& File) noexcept override {
        auto& P = get<Point>();
        Data OldPos{P.m_X, P.m_Y};
        File.Write(OldPos);
    }
    xcommandline::parser::handle m_hToPos;
};

int main() {
    Point MyPoint;
    xundo::system UndoSys;
    if (auto Err = UndoSys.Init("xundo_data", true); !Err.empty()) {
        std::cerr << "Init failed: " << Err << "\n";
        return 1;
    }

    MovePoint Mover(UndoSys, &MyPoint);
    Mover.Move(10, 20);
    Mover.Move(30, 40);
    std::cout << "X=" << MyPoint.m_X << ", Y=" << MyPoint.m_Y << "\n";
    UndoSys.Undo();
    std::cout << "After undo: X=" << MyPoint.m_X << ", Y=" << MyPoint.m_Y << "\n";
    UndoSys.Redo();
    std::cout << "After redo: X=" << MyPoint.m_X << ", Y=" << MyPoint.m_Y << "\n";
    UndoSys.displayHistory();
    std::cout << "Next move? " << UndoSys.SuggestNext(1) << "\n";
    return 0;
}
```

## Have Fun!
Xundo’s your sandbox—move stuff, undo it, break it, fix it. Add commands, tweak threads, make it yours. Questions? Dive into the code—it’s all there, waiting to be hacked. Enjoy the ride!

---
