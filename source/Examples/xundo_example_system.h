#ifndef XUNDO_EXAMPLE_SYSTEM_H
#define XUNDO_EXAMPLE_SYSTEM_H
#pragma once

#include "xundo_system.h"

//===========================================================================================================
// Example of how to use the undo system
//===========================================================================================================
namespace xundo::system_example
{
    // Simple database for testing
    struct fake_dbase
    {
        int m_X = 0, m_Y = 0;
    };

    // Command to move a point
    struct MovePoint : command_base
    {
        MovePoint(system& System, void* pDataBase) noexcept : command_base(System, "Move", pDataBase)
        {
            RegisterArguments();
        }

        // Undo data structure
        struct Data { int X, Y; };

        static std::string Move(int X, int Y) noexcept
        {
            return std::format("Move -T {} {}", X, Y);
        }

        // Help text for the command
        const char* getCommandHelp() const noexcept override
        {
            return "Move the point to X,Y. Usage: Move -T X Y";
        }

        // Registers command arguments
        void RegisterArguments() noexcept override
        {
            m_hToPos = m_Parser.addOption("T", "Target X,Y", true, 2);
        }

        // Reapplies the move command
        std::string Redo() noexcept override
        {
            if (m_Parser.hasOption(m_hToPos))
            {
                auto X = m_Parser.getOptionArgAs<int64_t>(m_hToPos, 0);
                auto Y = m_Parser.getOptionArgAs<int64_t>(m_hToPos, 1);
                if (std::holds_alternative<xcmdline::parser::error>(X)) return "Bad X!";
                if (std::holds_alternative<xcmdline::parser::error>(Y)) return "Bad Y!";
                auto& P = get<fake_dbase>();
                P.m_X = static_cast<int>(std::get<int64_t>(X));
                P.m_Y = static_cast<int>(std::get<int64_t>(Y));
                return {};
            }
            return "Need -T X Y!";
        }

        // Reverts the move command
        void Undo(undo_file& File) noexcept override
        {
            Data OldPos{ 0, 0 };
            File.Read(OldPos);
            auto& P = get<fake_dbase>();
            P.m_X = OldPos.X;
            P.m_Y = OldPos.Y;
        }

        // Saves current state for undo
        void BackupCurrenState(undo_file& File) noexcept override
        {
            auto& P = get<fake_dbase>();
            Data  OldPos{ P.m_X, P.m_Y };
            File.Write(OldPos);
        }

        xcmdline::parser::handle    m_hToPos;
    };

    // Stress test with groups and undo/redo
    int StressTest()
    {
        {
            fake_dbase              DataBase;
            system                  System;
            MovePoint               MoveCmd(System, &DataBase);
            if (auto Err = System.Init("x64\\xundo_data", false); !Err.empty())
            {
                std::cerr << Err << "\n";
                return 1;
            }

            // Load initial 500 commands
            for (int i = 0; i < 500; ++i)
            {
                if (auto Err = System.Execute(MoveCmd.Move(i, i)); !Err.empty()) assert(false);
            }
            std::cout << "After 500 commands:\n";
            System.displayHistory();
            assert(System.m_History.size() == 500 && System.m_UndoIndex == 500);
            assert(DataBase.m_X == 499 && DataBase.m_Y == 499);

            // Undo 100 steps
            for (int i = 0; i < 100; ++i) System.Undo();
            std::cout << "\nAfter 100 undos:\n";
            System.displayHistory();
            assert(System.m_UndoIndex == 400);
            assert(DataBase.m_X == 399 && DataBase.m_Y == 399);

            // Make sure we save all our history
            if (auto Err = System.SaveTimestamps(); !Err.empty())
            {
                std::cerr << Err << "\n";
                assert(false);
                return 1;
            }
        }

        // Second instance—loads prior history, continues work
        fake_dbase              DataBase{ 399, 399 };
        system                  System2;
        MovePoint               MoveCmd2(System2, &DataBase);
        if (auto Err = System2.Init("x64\\xundo_data", true); !Err.empty())
        {
            std::cerr << Err << "\n";
            return 1;
        }

        // Verify loaded history
        std::cout << "After init with prior history:\n";
        System2.displayHistory();
        assert(System2.m_History.size() == 400 && System2.m_UndoIndex == 400);
        assert(DataBase.m_X == 399 && DataBase.m_Y == 399);

        // Add 50 new commands
        for (int i = 0; i < 50; ++i)
        {
            if (auto Err = System2.Execute(MoveCmd2.Move(1000 + i, 1000 + i)); !Err.empty()) assert(false);
        }
        std::cout << "\nAfter 50 new commands:\n";
        System2.displayHistory();
        assert(System2.m_History.size() == 450 && System2.m_UndoIndex == 450);
        assert(DataBase.m_X == 1049 && DataBase.m_Y == 1049);

        // Undo 20 steps
        for (int i = 0; i < 20; ++i) System2.Undo();
        std::cout << "\nAfter 20 undos:\n";
        System2.displayHistory();
        assert(System2.m_UndoIndex == 430);
        assert(DataBase.m_X == 1029 && DataBase.m_Y == 1029);

        // Insert 10 mid-stack commands at 430
        for (int i = 0; i < 10; ++i)
        {
            if (auto Err = System2.Execute(MoveCmd2.Move(2000 + i, 2000 + i)); !Err.empty()) assert(false);
        }
        std::cout << "\nAfter 10 mid-stack inserts at 430:\n";
        System2.displayHistory();
        assert(System2.m_History.size() == 440 && System2.m_UndoIndex == 440);
        assert(DataBase.m_X == 2009 && DataBase.m_Y == 2009);

        // Test group commands
        std::vector<std::string> GroupCmds = { MoveCmd2.Move(3000,3000), MoveCmd2.Move(3100,3100) };
        if (auto Err = System2.Execute("My Group", GroupCmds); !Err.empty())
        {
            std::cout << Err << "\n";
            assert(false);
        }
        std::cout << "\nAfter first group:\n";
        System2.displayHistory();
        assert(System2.m_History.size() == 441 && System2.m_UndoIndex == 441);
        assert(DataBase.m_X == 3100 && DataBase.m_Y == 3100);

        GroupCmds = { "Move -T 3200 3200", "Move -T 3300 3300" };
        if (auto Err = System2.Execute("Another Group", GroupCmds); !Err.empty())
        {
            std::cout << Err << "\n";
            assert(false);
        }
        std::cout << "\nAfter second group:\n";
        System2.displayHistory();
        assert(System2.m_History.size() == 442 && System2.m_UndoIndex == 442);
        assert(DataBase.m_X == 3300 && DataBase.m_Y == 3300);

        System2.Undo();
        std::cout << "\nAfter undoing second group:\n";
        System2.displayHistory();
        assert(System2.m_UndoIndex == 441 && DataBase.m_X == 3100);

        System2.Undo();
        std::cout << "\nAfter undoing first group:\n";
        System2.displayHistory();
        assert(System2.m_UndoIndex == 440 && DataBase.m_X == 2009);

        std::cout << "Suggestion for User 1: " << System2.SuggestNext(1) << "\n";
        assert(System2.m_LRU.size() <= System2.m_MaxCachedSteps);
        return 0;
    }
}
#endif