#ifndef XUNDO_EXAMPLES_H
#define XUNDO_EXAMPLES_H
#pragma once
//===========================================================================================================
// Example of how to use the undo system
//===========================================================================================================
namespace xundo::example
{
    struct fake_dbase
    {
        int m_X = 0, m_Y = 0;
    };

    // This is a command that moves the cursor
    struct MoveCursor final : command_base
    {
        MoveCursor(system& System, void* pDataBase) noexcept : command_base(System, "Move", pDataBase)
        {
            RegisterArguments();
        }

        // relevant data to backup
        struct data
        {
            int X, Y;
        };

        // general description of the command
        const char* getCommandHelp() const noexcept override
        {
            return "Move the cursor to a new position";
        }

        // Register the arguments for this command as required by the system
        void RegisterArguments() noexcept override
        {
            m_hToPos = m_Parser.addOption("T", "Translate to X, Y position in abs values", true, 2);
        }

        // Simple interface for people using C++
        std::string Move(int X, int Y, int UserID = -1) noexcept
        {
            // T is for translation...
            return m_System.Execute(*this, std::format("{} -T {} {}", m_pCommandName, X, Y), UserID);
        }

        // This is the redo function, as required by the sytem
        std::string Redo() noexcept override
        {
            if (m_Parser.hasOption(m_hToPos))
            {
                auto x = m_Parser.getOptionArgAs<int64_t>(m_hToPos, 0);
                auto y = m_Parser.getOptionArgAs<int64_t>(m_hToPos, 1);

                if (std::holds_alternative<xcmdline::parser::error>(x)) 
                    return std::format("Failed to get parameter X, {}", std::get<xcmdline::parser::error>(x).c_str());

                if (std::holds_alternative<xcmdline::parser::error>(y)) 
                    return std::format("Failed to get parameter Y, {}", std::get<xcmdline::parser::error>(y).c_str());

                auto& DB = get<fake_dbase>();
                DB.m_X = static_cast<int>(std::get<int64_t>(x));
                DB.m_Y = static_cast<int>(std::get<int64_t>(y));
            }
            else return ("Expecting -T x y but found nothing");

            return {};
        }

        // This is the undo function, as required by the system
        void Undo(undo_file& File) noexcept override
        {
            data UndoData{ 0,0 };
            File.Read(UndoData);
            auto& DB = get<fake_dbase>();

            printf("Undo: X=%d, Y=%d -> Setting X=%d, Y=%d\n", DB.m_X, DB.m_Y, UndoData.X, UndoData.Y);
            DB.m_X = UndoData.X;
            DB.m_Y = UndoData.Y;
        }

        // This is the backup function, as required by the system
        void BackupCurrenState(undo_file& File) noexcept override
        {
            auto& DB = get<fake_dbase>();
            data UndoData{ DB.m_X, DB.m_Y };
            File.Write(UndoData);
        }

        // This is the handle to the position
        xcmdline::parser::handle m_hToPos;
    };

    // This is used to test the system
    int test()
    {
        fake_dbase  DataBase;
        system      System;
        MoveCursor  MoveCommand1(System, &DataBase);
        MoveCursor  MoveCommand2(System, &DataBase);

        if (auto Err = System.Init("x64/Undo"); Err.empty() == false) 
        {
            printf("%s\n", Err.c_str());
            return 1;
        }

        MoveCommand1.Move(10, 20, 1);
        MoveCommand2.Move(20, 30, 2);
        MoveCommand1.Move(30, 40, 1);
        System.displayHistory();
        System.Undo();
        System.displayHistory();
        System.Redo();
        System.displayHistory();
        std::cout << "Suggestion for User 1: " << System.SuggestNext(1) << "\n";
        return 0;
    }

    // Stress test with save/destroy/load cycle, mid-stack, and post-undo/redo commands
    int StressTest()
    {
        fake_dbase DataBase;

        //
        // First instance—no prior history, build initial state
        //
        {
            system System;
            MoveCursor MoveCommand1(System, &DataBase);
            if (auto Err = System.Init("x64/Undo", false); Err.empty() == false)
            {
                printf("%s\n", Err.c_str());
                assert(false);
                return 1;
            }

            // Load initial 500 commands
            for (int i = 0; i < 500; ++i)
            {
                if (auto Err = MoveCommand1.Move(i, i); !Err.empty())
                {
                    printf("%s\n", Err.c_str());
                    assert(false);
                    return 1;
                }
                
            }
            std::cout << "After 500 commands:\n";
            System.displayHistory();
            assert(System.m_History.size() == 500 && System.m_UndoIndex == 500);
            assert(DataBase.m_X == 499 && DataBase.m_Y == 499);

            // Undo 100 steps
            for (int i = 0; i < 100; ++i)System.Undo();
            std::cout << "\nAfter 100 undos:\n";
            System.displayHistory();
            assert(System.m_UndoIndex == 400);
            assert(DataBase.m_X == 399 && DataBase.m_Y == 399);

            // Save since we are not auto load/saving
            if (auto Err = System.SaveTimestamps(); Err.empty() == false)
            {
                printf("%s\n", Err.c_str());
                assert(false);
                return 1;
            }
        }

        //
        // Second instance—loads prior history, continues work
        //
        {
            system System;
            MoveCursor MoveCommand1(System, &DataBase);
            MoveCursor MoveCommand2(System, &DataBase); // test multiple commands
            if (auto Err = System.Init("x64/Undo"); Err.empty() == false)
            {
                printf("%s\n", Err.c_str());
                assert(false);
                return 1;
            }

            // Verify loaded history
            std::cout << "After init with prior history:\n";
            System.displayHistory();
            assert(System.m_History.size() == 400 && System.m_UndoIndex == 400);
            assert(DataBase.m_X == 399 && DataBase.m_Y == 399);

            // Add 50 new commands
            for (int i = 0; i < 50; ++i)
            {
                if (auto Err = MoveCommand1.Move(1000 + i, 1000 + i); !Err.empty())assert(false);
            }
            std::cout << "\nAfter 50 new commands:\n";
            System.displayHistory();
            assert(System.m_History.size() == 450 && System.m_UndoIndex == 450);
            assert(DataBase.m_X == 1049 && DataBase.m_Y == 1049);

            // Undo 20 steps
            for (int i = 0; i < 20; ++i)System.Undo();
            std::cout << "\nAfter 20 undos:\n";
            System.displayHistory();
            assert(System.m_UndoIndex == 430);
            assert(DataBase.m_X == 1029 && DataBase.m_Y == 1029);

            // Insert 10 mid-stack commands at 430
            for (int i = 0; i < 10; ++i)
            {
                if (auto Err = MoveCommand1.Move(2000 + i, 2000 + i); !Err.empty())assert(false);
            }
            std::cout << "\nAfter 10 mid-stack inserts at 430:\n";
            System.displayHistory();
            assert(System.m_History.size() == 440 && System.m_UndoIndex == 440);
            assert(DataBase.m_X == 2009 && DataBase.m_Y == 2009);

            std::cout << "Suggestion for User 1: " << System.SuggestNext(1) << "\n";
            assert(System.m_LRU.size() <= System.m_MaxCachedSteps);
        }
        return 0;
    }
}
#endif